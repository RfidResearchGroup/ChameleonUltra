"""Real command methods and extracted real C handlers, mocked hardware API."""
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import Mock

from chameleon_cmd import ChameleonCMD
from chameleon_enum import Command, Status, TagSpecificType


class DiagnosticCommands(unittest.TestCase):
    def setUp(self):
        self.device = Mock()
        self.cmd = ChameleonCMD(self.device)
        self.cmd.get_device_capabilities = Mock(return_value=list(Command))
        self.cmd._get_active_lf_tag_type = Mock(return_value=TagSpecificType.RAW_MANCHESTER)
        self.device.send_cmd_sync.return_value = Mock(status=Status.SUCCESS, data=b'\x01')

    def test_wire_and_status(self):
        self.cmd.raw_diagnostic_start(5000)
        self.device.send_cmd_sync.assert_called_with(Command.RAW_DIAGNOSTIC_START, b'\x13\x88')
        self.cmd.raw_diagnostic_stop()
        self.device.send_cmd_sync.assert_called_with(Command.RAW_DIAGNOSTIC_STOP)
        self.assertEqual(self.cmd.raw_diagnostic_status(), 1)
        for raw in (b'', b'\x03', b'\x01\x00'):
            self.device.send_cmd_sync.return_value.data = raw
            with self.assertRaises(ValueError):
                self.cmd.raw_diagnostic_status()

    def test_reject_before_mutation(self):
        for ms in (0, 5001, -1, True, 1.5, '10'):
            with self.assertRaises(ValueError):
                self.cmd.raw_diagnostic_start(ms)
        self.device.send_cmd_sync.assert_not_called()
        self.cmd.get_device_capabilities.return_value = []
        for action in (lambda: self.cmd.raw_diagnostic_start(10), self.cmd.raw_diagnostic_stop,
                       self.cmd.raw_diagnostic_status):
            with self.assertRaises(ValueError):
                action()
        self.device.send_cmd_sync.assert_not_called()
        self.cmd.get_device_capabilities.return_value = list(Command)
        self.cmd._get_active_lf_tag_type.return_value = TagSpecificType.UNDEFINED
        with self.assertRaises(ValueError):
            self.cmd.raw_diagnostic_start(10)
        self.device.send_cmd_sync.assert_not_called()

    def test_real_c_handlers(self):
        root = pathlib.Path(__file__).resolve().parents[3]
        source = (root / 'firmware/application/src/app_cmd.c').read_text()
        handlers = source[source.index('static bool raw_diagnostic_usb_allowed'):source.index('static data_frame_tx_t *cmd_processor_raw_manchester_get_emu')]
        code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
enum { STATUS_PAR_ERR=1, STATUS_SUCCESS=0, DEVICE_MODE_READER=1,
       TAG_TYPE_RAW_MANCHESTER=190, DATA_FRAME_ORIGIN_USB=1 };
typedef struct { uint8_t *buffer; unsigned length; } tag_data_buffer_t;
typedef struct { int tag_lf; } tag_slot_specific_type_t;
typedef struct { int status, length; uint8_t value; } data_frame_tx_t;
static int origin=1, usb=1, ble=0, mode=0, kind=190, valid=1, works=1;
static int starts, stops, reads; static unsigned duration;
static uint8_t record[32]; static tag_data_buffer_t buffer={record,32};
static data_frame_tx_t response;
static int data_frame_get_origin(void) {return origin;}
static bool is_usb_working(void) {return usb;}
static bool is_nus_working(void) {return ble;}
static int get_device_mode(void) {return mode;}
static int tag_emulation_get_slot(void) {return 0;}
static void tag_emulation_get_specific_types_by_slot(int s, tag_slot_specific_type_t *t) {t->tag_lf=kind;}
static tag_data_buffer_t *get_buffer_by_tag_type(int t) {return &buffer;}
static bool lf_raw_manchester_validate(uint8_t *p, unsigned n) {return valid;}
static bool lf_tag_raw_diagnostic_start(uint32_t ms) {starts++; duration=ms; return works;}
static void lf_tag_raw_diagnostic_stop(void) {stops++;}
static uint8_t lf_tag_raw_diagnostic_status(void) {reads++; return 2;}
static data_frame_tx_t *data_frame_make(unsigned c, unsigned s, unsigned n, uint8_t *p) {
 response=(data_frame_tx_t){s,n,n ? *p : 0}; return &response;
}
'''
        code += handlers
        code += r'''
#define START(n) cmd_processor_raw_diagnostic_start(5022,0,n,data)->status
#define STOP(n) cmd_processor_raw_diagnostic_stop(5023,0,n,NULL)->status
#define STATUS(n) cmd_processor_raw_diagnostic_status(5024,0,n,NULL)->status
int main(void) {
 uint8_t data[3]={0x13,0x88,0};
 assert(!START(2) && starts==1 && duration==5000);
 assert(START(0) && START(1) && START(3));
 data[0]=0; data[1]=0; assert(START(2));
 data[0]=0x13; data[1]=0x89; assert(START(2));
 data[0]=0; data[1]=1; assert(!START(2) && duration==1);
 kind=0; assert(START(2)); kind=190;
 valid=0; assert(START(2)); valid=1;
 mode=1; assert(START(2)); mode=0;
 works=0; assert(START(2)); works=1;
 assert(STOP(1) && STATUS(1));
 assert(!STOP(0) && !STOP(0) && stops==2);
 assert(!STATUS(0) && response.length==1 && response.value==2);
 for (int i=0;i<3;i++) {
   int before=starts, oldstops=stops, oldreads=reads;
   if(i==0) origin=0; if(i==1) usb=0; if(i==2) ble=1;
   assert(START(2) && STOP(0) && STATUS(0));
   assert(starts==before && stops==oldstops && reads==oldreads);
   origin=1; usb=1; ble=0;
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src = pathlib.Path(directory) / 'handlers.c'
            exe = pathlib.Path(directory) / 'handlers'
            src.write_text(code)
            subprocess.run(['cc', '-std=c11', str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_cli_explicit_actions_no_save_or_slot_switch(self):
        from chameleon_cli_unit import LFRawDiagnostic
        unit = LFRawDiagnostic()
        unit._device_cmd = Mock()
        unit.cmd.raw_diagnostic_status.return_value = 2
        parser = unit.args_parser()
        for arguments in (['--ms', '10'], ['--stop'], ['--status']):
            unit.on_exec(parser.parse_args(arguments))
        self.assertEqual([call[0] for call in unit.cmd.mock_calls],
                         ['raw_diagnostic_start', 'raw_diagnostic_stop', 'raw_diagnostic_status'])
        for arguments in ([], ['--stop', '--ms', '10'], ['--stop', '--status'], ['--slot', '2', '--stop']):
            with self.assertRaises(Exception):
                parser.parse_args(arguments)


if __name__ == '__main__':
    unittest.main()
