"""Execute actual MF1/14A event callback and sensing lifecycle.

SDK registers, DMA rearm, peripheral reset, timers and TX are explicit host
boundaries. No MMIO, hardware timing or RF reception is claimed.
"""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import test_cu_sl1_native_hooks as base

HF=base.ROOT/'firmware/application/src/rfid/nfctag/hf'

EVENT_SHIM=r'''
#define NRF52840_XXAA 1
#define NRFX_SUCCESS 0
#define NRFX_NFCT_STATE_ACTIVATED 1
#define RGB_GREEN 1
#define SLEEP_DELAY_MS_FIELD_NFC_LOST 20
#define TAG_FIELD_LED_ON() do {} while(0);
#define TAG_FIELD_LED_OFF() do {} while(0);
#define NFCT_TXD_AMOUNT_TXDATABYTES_Pos 0
#define NFCT_TXD_AMOUNT_TXDATABYTES_Msk 0xFFFF
#define NRFX_NFCT_RX_BYTES do {++host_rearms;} while(0);
enum {NRFX_NFCT_EVT_FIELD_DETECTED,NRFX_NFCT_EVT_FIELD_LOST,NRFX_NFCT_EVT_TX_FRAMESTART,NRFX_NFCT_EVT_TX_FRAMEEND,NRFX_NFCT_EVT_RX_FRAMEEND,NRFX_NFCT_EVT_ERROR};
enum {NRFX_NFCT_ERROR_FRAMEDELAYTIMEOUT,NRFX_NFCT_ERROR_NUM};
typedef struct {unsigned evt_id;struct {struct {unsigned reason;} error;} params;} nrfx_nfct_evt_t;
typedef struct {uint32_t rxtx_int_mask;void (*cb)(nrfx_nfct_evt_t const *);} nrfx_nfct_config_t;
static unsigned host_rearms,host_inits,host_uninits,host_reset_calls,host_sleep_starts,host_sleep_stops,host_fdt_resets;
static unsigned host_limit_at_uninit;
static bool g_is_tag_emulating,g_usb_led_marquee_enable,reset_if_field_lost;
static bool m_is_responded;
static uint8_t m_nfc_rx_buffer[257],m_nfc_tx_buffer[512];
static nfc_tag_14a_tx_sniff_cb_t m_tx_sniff_cb;
#define DEVICE_MODE_TAG 2
#define HF_SNIFF_BUF_SIZE 4096
static uint8_t m_sniff_buf[HF_SNIFF_BUF_SIZE];
static uint16_t m_sniff_buf_len,m_sniff_cb_count;
static bool m_sniff_active;
typedef struct {unsigned status;} data_frame_tx_t;
static data_frame_tx_t host_sniff_response;
static data_frame_tx_t *data_frame_make(uint16_t command,uint16_t status,uint16_t length,uint8_t *data) {
    (void)command;(void)length;(void)data;host_sniff_response.status=status;return &host_sniff_response;
}
static unsigned get_device_mode(void) {return DEVICE_MODE_TAG;}
static void sl1_profile_poll(void) {}
typedef unsigned autotimer;
static unsigned host_sniff_timer;
static autotimer *bsp_obtain_timer(unsigned index) {(void)index;host_sniff_timer=0;return &host_sniff_timer;}
#define NO_TIMEOUT_1MS(timer,limit) (++*(timer)<=(limit))
static void bsp_return_timer(autotimer *timer) {(void)timer;}
static void bsp_wdt_feed(void) {}

static void sleep_timer_stop(void) {++host_sleep_stops;}
static void sleep_timer_start(unsigned delay) {(void)delay;++host_sleep_starts;}
static void set_slot_light_color(unsigned color) {(void)color;}
static void nrfx_nfct_autocolres_disable(void) {}
static void nrfx_nfct_state_force(unsigned state) {(void)state;}
static void nrf_nfct_reset(void) {++host_reset_calls;}
'''
TAIL_SHIM=r'''
static unsigned nrfx_nfct_init(const nrfx_nfct_config_t *config) {(void)config;++host_inits;host_sensing_available=true;return NRFX_SUCCESS;}
static void nrfx_nfct_enable(void) {}
static void nrfx_nfct_uninit(void) {++host_uninits;host_limit_at_uninit=host_frame_delay_max;host_sensing_available=false;}
static void nfc_fdt_reset(void) {++host_fdt_resets;nrf_nfct_frame_delay_max_set(4096);}
static void bsp_delay_ms(unsigned ms) {
    (void)ms;const uint8_t frame[]={0x26};
    if(m_sniff_cb)m_sniff_cb(frame,7);
    if(m_tx_sniff_cb)m_tx_sniff_cb(frame,7);
}
'''
BRIDGE=r'''
unsigned event_sniff(void) {uint8_t timeout[]={0,4};return cmd_processor_hf14a_sniff(2020,0,2,timeout)->status;}
unsigned event_key_matches(const uint8_t *key) {return m_sl1.core.key_set&&!memcmp(m_sl1.core.aes_key,key,16);}
void event_init(void) {
    test_init();host_rearms=host_inits=host_uninits=host_reset_calls=host_sleep_starts=host_sleep_stops=host_fdt_resets=0;
    host_limit_at_uninit=0;g_is_tag_emulating=g_usb_led_marquee_enable=reset_if_field_lost=m_is_responded=false;
    m_nfc_sense_state=NFC_SENSE_STATE_NONE;host_sensing_available=false;nfc_tag_14a_sense_switch(true);
}
void event_emit(unsigned id,unsigned reason) {
    nrfx_nfct_evt_t event={.evt_id=id};event.params.error.reason=reason;nfc_tag_14a_event_callback(&event);
}
unsigned event_frame(const uint8_t *data,unsigned length) {
    memset(m_nfc_rx_buffer,0,sizeof(m_nfc_rx_buffer));unsigned wire=0;host_bits=0;
    for(unsigned i=0;i<length;++i){
        unsigned parity=1;
        for(unsigned bit=0;bit<8;++bit,++wire){unsigned value=(data[i]>>bit)&1U;parity^=value;m_nfc_rx_buffer[wire/8]|=value<<(wire%8);}
        m_nfc_rx_buffer[wire/8]|=parity<<(wire%8);++wire;
    }
    NRF_NFCT->RXD.AMOUNT=wire;event_emit(NRFX_NFCT_EVT_RX_FRAMEEND,0);return host_bits;
}
void event_setting(unsigned which,unsigned value) {
    if(which==0)reset_if_field_lost=value;
    if(which==1){m_tag_handler.cb_session_reset=NULL;m_tag_handler.cb_unload=NULL;m_tag_handler.cb_plaintext_allowed=NULL;}
    if(which==2)m_tag_state_14a=NFC_TAG_STATE_14A_ACTIVE;
}
unsigned event_value(unsigned which) {
    switch(which){case 0:return host_rearms;case 1:return host_inits;case 2:return host_uninits;case 3:return host_reset_calls;
    case 4:return host_sleep_starts;case 5:return host_sleep_stops;case 6:return host_fdt_resets;case 7:return host_limit_at_uninit;
    case 8:return nfc_tag_14a_field_present();case 9:return nfc_tag_14a_sensing_enabled();default:return 0;}
}
'''

class NfctEventLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='nfct-event-host-');cls.addClassCleanup(cls.temp.cleanup)
        tmp=Path(cls.temp.name)
        source14=(HF/'nfc_14a.c').read_text()
        native=re.sub(r'^#include[^\n]*','',(HF/'nfc_mf1.c').read_text().split('// Factory data for initialization of MF1')[0],flags=re.M)
        header14=re.sub(r'^#include[^\n]*','',(HF/'nfc_14a.h').read_text(),flags=re.M)
        headermf=re.sub(r'^#include[^\n]*','',(HF/'nfc_mf1.h').read_text(),flags=re.M)
        shim=base.SHIM.replace(base.function(base.SHIM,'nfc_tag_14a_sensing_enabled'),'')
        shim=shim.replace('static struct { struct { uint32_t AMOUNT; } RXD; } host_nfct;',
            'static struct { struct { uint32_t AMOUNT; } RXD,TXD; } host_nfct;')
        for name in ('nfc_tag_14a_tx_bytes','nfc_tag_14a_tx_bits','nfc_tag_14a_tx_nbit'):
            old=base.function(shim,name);new=old.replace('{\n','{\n    m_is_responded=true;\n',1);shim=shim.replace(old,new)
        functions=[base.function(source14,name) for name in (
            'nfc_tag_14a_set_state','nfc_tag_14a_set_handler','nfc_tag_14a_set_sniff_passive','nfc_tag_14a_is_sniff_passive',
            'nfc_tag_14a_field_present','nfc_tag_14a_create_bcc','nfc_tag_14a_append_bcc','nfc_tag_14a_append_crc',
            'nfc_tag_14a_checks_crc','nfc_tag_14a_unwrap_frame','nfc_tag_14a_extended_ats_fits','nfc_tag_14a_data_process',
            'nfc_tag_14a_event_callback','nfc_tag_14a_sense_switch','nfc_tag_14a_sensing_enabled',
            'nfc_tag_14a_set_sniff_cb','nfc_tag_14a_clear_sniff_cb','nfc_tag_14a_set_tx_sniff_cb','nfc_tag_14a_clear_tx_sniff_cb')]
        functions=[re.sub(r'^inline ','',item) for item in functions]
        enum_start=source14.index('static enum  {');enum_end=source14.index(';',enum_start)+1
        preamble='#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#define PACKED\ntypedef struct {uint16_t length;uint8_t *buffer;uint16_t *crc;} tag_data_buffer_t;\n'
        command_source=(HF.parents[2]/'app_cmd.c').read_text()
        sniff_functions=[base.function(command_source,name) for name in (
            'hf14a_sniff_store','hf14a_sniff_frame_cb','hf14a_sniff_tx_frame_cb','cmd_processor_hf14a_sniff')]
        source='\n'.join([preamble,(HF.parent/'tag_base_type.h').read_text(),header14,headermf,
            (HF.parents[2]/'app_status.h').read_text(),EVENT_SHIM,shim,TAIL_SHIM,
            source14[enum_start:enum_end],native,*functions,*sniff_functions,base.BRIDGE,BRIDGE])
        (tmp/'events.c').write_text(source)
        sl1=HF/'sl1'
        run=subprocess.run(['cc','-std=c11','-shared','-fPIC','-O1','-Wall','-Wextra',
            '-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types','-I',str(sl1),str(tmp/'events.c'),
            str(sl1/'sl1_framing.c'),str(sl1/'sl1_cardauth.c'),str(sl1/'vendor/aes.c'),str(HF.parents[1]/'byte_mirror.c'),
            '-o',str(tmp/'events.dylib')],capture_output=True,text=True)
        if run.returncode:raise RuntimeError(run.stderr)
        cls.lib=C.CDLL(str(tmp/'events.dylib'))
        cls.lib.nfc_tag_mf1_sl1_configure.argtypes=[C.c_uint8,C.c_void_p,C.c_uint8,C.c_void_p]
        cls.lib.nfc_tag_mf1_sl1_configure.restype=C.c_bool
        cls.lib.event_frame.argtypes=[C.c_void_p,C.c_uint]
        cls.lib.event_key_matches.argtypes=[C.c_void_p]
        cls.lib.test_tx.restype=C.POINTER(C.c_uint8)
    def setUp(self):self.lib.event_init()
    def enable(self):
        self.assertTrue(self.lib.nfc_tag_mf1_sl1_configure(1,base.UID,7,base.KEY))
        for _ in range(4):self.lib.nfc_tag_mf1_sl1_poll()
        self.lib.event_setting(2,1)
    def aes(self):self.assertEqual(self.lib.event_frame(base.frame(b'\x76\x04\x90'),5),136)

    def test_actual_rx_response_rearms_after_tx_end_and_silent_frame_rearms_immediately(self):
        self.enable();self.lib.event_emit(0,0);self.assertEqual(self.lib.event_value(0),1)
        self.aes();self.assertEqual(self.lib.event_value(0),1)
        self.lib.event_emit(3,0);self.assertEqual(self.lib.event_value(0),2)
        self.lib.event_frame(base.frame(b'\x76\x05\x90'),5)
        self.assertEqual(self.lib.event_value(0),3);self.assertEqual(self.lib.event_value(6),1)

    def test_real_sniff_keeps_actual_native_aes_pending_and_key_unchanged(self):
        self.enable();self.aes()
        self.assertEqual(self.lib.test_pending(),1)
        challenge=bytes(self.lib.test_tx()[1:17])
        rnd_b=base.AES.new(base.KEY,base.AES.MODE_ECB).decrypt(challenge)
        before=(self.lib.event_value(1),self.lib.event_value(2),self.lib.test_counter(5))
        self.assertEqual(self.lib.event_sniff(),0x68)
        self.assertEqual(self.lib.test_pending(),1)
        self.assertEqual(self.lib.event_key_matches(base.KEY),1)
        self.assertEqual((self.lib.event_value(1),self.lib.event_value(2),self.lib.test_counter(5)),before)
        rnd_a=bytes(range(90,106))
        token=b"\x72"+base.AES.new(base.KEY,base.AES.MODE_CBC,bytes(16)).encrypt(rnd_a+rnd_b[1:]+rnd_b[:1])
        proof=base.frame(token)
        self.assertEqual(self.lib.event_frame(proof,len(proof)),136)
        self.assertEqual(self.lib.test_counter(4),1,'the pre-sniff challenge must still verify')

    def test_actual_field_lost_clears_pending_keeps_key_and_obeys_peripheral_reset_setting(self):
        for reset in (0,1):
            self.lib.event_init();self.enable();self.lib.event_setting(0,reset)
            self.lib.event_emit(0,0);self.aes();self.assertEqual(self.lib.test_pending(),1)
            self.lib.event_emit(1,0)
            self.assertEqual(self.lib.test_pending(),0);self.assertEqual(self.lib.test_secrets_empty(),0)
            self.assertEqual(self.lib.event_value(8),0);self.assertEqual(self.lib.event_value(3),reset)
            self.assertEqual(self.lib.test_window(),4096)

    def test_actual_error_resets_aes_and_classic_session_without_unloading_key(self):
        for reason in (0,1):
            self.lib.event_init();self.enable();self.aes();self.lib.event_emit(5,reason)
            self.assertEqual(self.lib.test_pending(),0);self.assertEqual(self.lib.test_secrets_empty(),0)
            self.lib.event_setting(2,1);self.lib.event_frame(base.frame(b'\x60\x01'),4)
            self.assertEqual(self.lib.test_mf_state(),1);self.lib.event_emit(5,reason)
            self.assertEqual(self.lib.test_mf_state(),0)
            self.assertEqual(self.lib.test_secrets_empty(),0)

    def test_actual_field_lost_resets_pending_and_authenticated_classic(self):
        for authenticated in (False,True):
            self.lib.event_init();self.enable();self.lib.event_emit(0,0)
            if authenticated:
                self.lib.test_authenticated()
            else:
                self.lib.event_frame(base.frame(b"\x60\x01"),4)
            self.assertEqual(self.lib.test_mf_state(),2 if authenticated else 1)
            self.lib.event_emit(1,0)
            self.assertEqual(self.lib.test_mf_state(),0)
            self.assertEqual(self.lib.test_secrets_empty(),0)

    def test_actual_stop_restores_window_wipes_key_before_uninit_and_is_idempotent(self):
        self.enable();self.aes();self.assertEqual(self.lib.test_window(),65535)
        self.lib.nfc_tag_14a_sense_switch(False)
        self.assertEqual(self.lib.event_value(7),4096);self.assertEqual(self.lib.event_value(2),1)
        self.assertEqual(self.lib.test_secrets_empty(),1);self.assertEqual(self.lib.event_value(9),0)
        self.lib.nfc_tag_14a_sense_switch(False)
        self.assertEqual(self.lib.event_value(2),1);self.assertEqual(self.lib.test_bad_mmio(),0)
        self.lib.nfc_tag_14a_sense_switch(True)
        self.assertEqual(self.lib.event_value(1),2);self.assertEqual(self.lib.event_value(9),1)

    def test_ordinary_null_callback_profile_preserves_legacy_native_state(self):
        self.lib.event_setting(1,1);self.lib.test_authenticated()
        self.lib.event_emit(5,0);self.assertEqual(self.lib.test_mf_state(),2)
        self.lib.event_emit(1,0);self.assertEqual(self.lib.test_mf_state(),2)
        self.lib.nfc_tag_14a_sense_switch(False);self.assertEqual(self.lib.test_mf_state(),2)

if __name__=='__main__':unittest.main()
