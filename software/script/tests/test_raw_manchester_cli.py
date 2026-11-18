import unittest
from unittest.mock import Mock

from chameleon_enum import Command, TagSpecificType
from lf_raw_manchester import RawManchesterRecord, access_slot


class RawRecordTest(unittest.TestCase):
    def test_roundtrip(self):
        for bits in (1, 8, 9, 224):
            record = RawManchesterRecord(64, bits, bytes((bits + 7) // 8))
            self.assertEqual(RawManchesterRecord.decode(record.encode()), record)

    def test_malformed(self):
        good = bytearray(RawManchesterRecord(32, 1, b'\x80').encode())
        for index, value in ((0, 2), (1, 16), (2, 3), (2, 4), (3, 0), (3, 225), (4, 0x81), (31, 1)):
            raw = good.copy()
            raw[index] = value
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                RawManchesterRecord.decode(bytes(raw))
        for raw in (b'', bytes(good[:-1]), bytes(good) + b'\0'):
            with self.assertRaises(ValueError):
                RawManchesterRecord.decode(raw)


class LifecycleTest(unittest.TestCase):
    def setUp(self):
        self.record = RawManchesterRecord(32, 8, b'\xaa')
        self.cmd = Mock()
        self.cmd.get_device_capabilities.return_value = list(Command)
        self.cmd.get_active_slot.return_value = 0
        self.cmd.get_slot_info.return_value = [{'hf': 1001, 'lf': 0}] * 8
        self.cmd.raw_manchester_get.return_value = self.record

    def test_success_preserves_hf_restores_and_saves(self):
        access_slot(self.cmd, 2, self.record)
        self.cmd.set_slot_tag_type.assert_called_once_with(2, TagSpecificType.RAW_MANCHESTER)
        self.assertEqual(self.cmd.set_active_slot.call_args.args, (1,))
        self.assertEqual(self.cmd.slot_data_config_save.call_count, 2)
        names = [call[0] for call in self.cmd.mock_calls]
        self.assertLess(names.index('raw_manchester_get'), names.index('slot_data_config_save'))

    def test_occupied_needs_force(self):
        self.cmd.get_slot_info.return_value = [{'hf': 1001, 'lf': 100}] * 8
        with self.assertRaisesRegex(ValueError, '--force'):
            access_slot(self.cmd, 2, self.record)
        self.cmd.set_active_slot.assert_not_called()
        access_slot(self.cmd, 2, self.record, force=True)

    def test_capability_checked_before_mutation(self):
        self.cmd.get_device_capabilities.return_value = []
        with self.assertRaisesRegex(ValueError, 'capabilities'):
            access_slot(self.cmd, 2, self.record)
        self.cmd.set_active_slot.assert_not_called()
        self.cmd.set_slot_tag_type.assert_not_called()

    def test_failure_partial_warning_and_restoration(self):
        self.cmd.raw_manchester_set.side_effect = IOError('synthetic failure')
        with self.assertRaisesRegex(RuntimeError, 'partial changes'):
            access_slot(self.cmd, 2, self.record)
        self.assertEqual(self.cmd.set_active_slot.call_args.args, (1,))
        self.cmd.slot_data_config_save.assert_not_called()

    def test_readback_mismatch(self):
        self.cmd.raw_manchester_get.return_value = RawManchesterRecord(64, 8, b'\xaa')
        with self.assertRaisesRegex(RuntimeError, 'readback mismatch'):
            access_slot(self.cmd, 2, self.record)
        self.cmd.slot_data_config_save.assert_not_called()


if __name__ == '__main__':
    unittest.main()
