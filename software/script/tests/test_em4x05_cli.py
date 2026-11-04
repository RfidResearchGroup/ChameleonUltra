import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from chameleon_cli_unit import LFEM4x05Backup, LFEM4x05ReadWord
from chameleon_utils import ArgsParserError
from tests.em4x05_fixtures import CaptureDevice


class EMCLITest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "backup.json"
        self.device = CaptureDevice()
        decoder = patch("em4x05_backup.decode_capture", side_effect=self.device.decoded)
        decoder.start()
        self.addCleanup(decoder.stop)

    def run_unit(self, unit, *args):
        unit.device_com = self.device
        arguments = unit.args_parser().parse_args(list(args))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            unit.on_exec(arguments)
        return output.getvalue()

    def test_read_word_uses_verified_path(self):
        text = self.run_unit(LFEM4x05ReadWord(), "-b", "4")
        self.assertIn("Word 4:", text)
        self.assertEqual(self.device.counts[4], 2)
        self.assertEqual(self.device.mode, 0)

    def test_unreadable_password_word_fails_before_rf(self):
        with self.assertRaises(ArgsParserError):
            self.run_unit(LFEM4x05ReadWord(), "-b", "2")
        self.assertEqual(self.device.calls, [])

    def test_backup_publishes_only_summary(self):
        text = self.run_unit(LFEM4x05Backup(), "-o", str(self.path))
        self.assertIn("15 readable", text)
        self.assertTrue(self.path.exists())
        self.assertNotIn(str(self.device.words[1]), text)

    def test_explicit_zero_password_uses_login(self):
        self.run_unit(LFEM4x05ReadWord(), "-b", "1", "--pwd", "00000000")
        for cmd, payload in self.device.calls:
            if cmd == 3043:
                self.assertEqual(payload[1:], b"\x01\x00\x00\x00\x00")

    def test_existing_path_is_not_touched(self):
        self.path.write_bytes(b"keep")
        with self.assertRaises(ArgsParserError):
            self.run_unit(LFEM4x05Backup(), "-o", str(self.path))
        self.assertEqual(self.device.calls, [])
        self.assertEqual(self.path.read_bytes(), b"keep")
