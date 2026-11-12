import copy
import json
import tempfile
import unittest
from pathlib import Path
from typing import cast
from unittest.mock import patch

from chameleon_enum import Command
from em4x05_backup import EM4x05Reader, READ_CAPTURE_COMMAND, validate_backup
from em4x05_decode import EMReadError
from tests.em4x05_fixtures import CaptureDevice


class EMBackupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "backup.json"
        self.device = CaptureDevice()
        # Transport/state tests isolate demodulation, tested with real waveforms separately.
        decoder = patch("em4x05_backup.decode_capture", side_effect=self.device.decoded)
        decoder.start()
        self.addCleanup(decoder.stop)
        self.reader = EM4x05Reader(self.device)

    def test_complete_backup_has_15_readable_words(self):
        value = self.reader.backup(self.path)
        self.assertEqual(value["words"][:2], self.device.words[:2])
        self.assertIsNone(value["words"][2])
        self.assertEqual(value["words"][3:], self.device.words[3:])
        self.assertEqual(json.loads(self.path.read_text()), value)
        self.assertEqual(self.device.mode, 0)
        self.assertNotIn(2, self.device.counts)
        self.assertTrue(all(count >= 2 for count in self.device.counts.values()))

    def test_password_is_never_serialized(self):
        value = self.reader.backup(self.path, 0xA1B2C3D4)
        self.assertIsNone(value["words"][2])
        self.assertTrue(value["login_attempted"])
        self.assertNotIn(str(0xA1B2C3D4), self.path.read_text())
        for command, payload in self.device.calls:
            if command == READ_CAPTURE_COMMAND:
                self.assertEqual(payload[1], 1)

    def test_invalid_word_does_not_touch_hardware(self):
        for word in (2, -1, 16, True):
            with self.assertRaises(EMReadError):
                self.reader.read_word(word)
        self.assertEqual(self.device.calls, [])

    def test_invalid_password_does_not_touch_hardware(self):
        for password in (-1, 0x100000000, True, "guess"):
            with self.assertRaises(EMReadError):
                self.reader.read_word(1, cast(int, password))
        self.assertEqual(self.device.calls, [])

    def test_only_known_read_mode_commands_are_sent(self):
        self.reader.backup(self.path)
        allowed = {
            Command.GET_DEVICE_MODE,
            Command.CHANGE_DEVICE_MODE,
            READ_CAPTURE_COMMAND,
        }
        self.assertTrue(all(cmd in allowed for cmd, _ in self.device.calls))

    def test_unstable_word_aborts_without_file(self):
        self.device.unstable = 5
        with self.assertRaisesRegex(EMReadError, "changed"):
            self.reader.backup(self.path)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.device.mode, 0)

    def test_incomplete_read_is_not_a_partial_success(self):
        self.device.fail_word = 8
        with self.assertRaises(EMReadError):
            self.reader.backup(self.path)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.device.mode, 0)

    def test_unknown_chip_aborts_without_file(self):
        self.device.words[0] = 4 << 1
        with self.assertRaisesRegex(EMReadError, "unsupported chip"):
            self.reader.backup(self.path)
        self.assertFalse(self.path.exists())

    def test_existing_backup_is_not_overwritten_or_probed(self):
        self.path.write_bytes(b"keep")
        with self.assertRaises(EMReadError):
            self.reader.backup(self.path)
        self.assertEqual(self.path.read_bytes(), b"keep")
        self.assertEqual(self.device.calls, [])

    def test_missing_capability_is_not_replaced_with_legacy_scan(self):
        self.device.commands.remove(READ_CAPTURE_COMMAND)
        with self.assertRaisesRegex(EMReadError, "lacks"):
            self.reader.read_word(1)
        self.assertEqual(self.device.calls, [])

    def test_reader_mode_is_retained(self):
        self.device.mode = 1
        self.reader.read_word(1)
        self.assertFalse(
            any(c == Command.CHANGE_DEVICE_MODE for c, _ in self.device.calls)
        )

    def test_restore_failure_blocks_publication(self):
        self.device.restore_fail = True
        with self.assertRaisesRegex(EMReadError, "restore"):
            self.reader.backup(self.path)
        self.assertFalse(self.path.exists())

    def test_cancellation_is_preserved_when_restore_fails(self):
        self.device.cancellation = True
        self.device.restore_fail = True
        with self.assertRaises(KeyboardInterrupt) as raised:
            self.reader.read_word(1)
        self.assertIn("restore", getattr(raised.exception, "restore_error"))

    def test_fsync_failure_is_not_success(self):
        with patch("em4x05_backup.os.fsync", side_effect=OSError("disk unavailable")):
            with self.assertRaisesRegex(EMReadError, "publication"):
                self.reader.backup(self.path)
        self.assertEqual(self.device.mode, 0)

    def test_corruption_is_detected(self):
        value = self.reader.backup(self.path)
        changed = copy.deepcopy(value)
        changed["words"][5] ^= 1
        with self.assertRaisesRegex(EMReadError, "checksum"):
            validate_backup(changed)

    def test_password_placeholder_cannot_be_supplied_as_read(self):
        value = self.reader.backup(self.path)
        value["words"][2] = 0
        with self.assertRaisesRegex(EMReadError, "unreadable"):
            validate_backup(value)
