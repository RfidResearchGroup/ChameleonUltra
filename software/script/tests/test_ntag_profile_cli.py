import contextlib
import hashlib
import io
import tempfile
import unittest
from pathlib import Path

from chameleon_cli_unit import (
    HWNtagProfileBackup,
    HWNtagProfileImport,
    HWNtagProfileRestore,
)
from chameleon_utils import ArgsParserError
from ntag_profile_data import SlotSnapshot, SlotState
from ntag_profile_io import load_snapshot, save_snapshot
from tests.ntag_profile_fixtures import FakeNtagDevice, profile
from tests.test_ntag_profile import MUTATIONS


class NtagCLITest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.device = FakeNtagDevice()
        self.source = self.folder / "source.json"
        self.desired = profile(seed=61)
        self.fingerprint = hashlib.sha256(self.device.fingerprint).hexdigest()
        save_snapshot(
            SlotSnapshot(self.fingerprint, 8, SlotState(1100, True, self.desired)),
            self.source,
        )

    def run_unit(self, unit, *arguments):
        unit.device_com = self.device
        args = unit.args_parser().parse_args(list(arguments))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            unit.on_exec(args)
        return output.getvalue()

    def test_import_defaults_to_preview_without_setters(self):
        output = self.run_unit(HWNtagProfileImport(), "-s", "8", "-f", str(self.source))
        self.assertIn("Preview only", output)
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_apply_without_backup_fails_before_device_commands(self):
        with self.assertRaises(ArgsParserError):
            self.run_unit(
                HWNtagProfileImport(), "-s", "8", "-f", str(self.source), "--apply"
            )
        self.assertEqual(self.device.calls, [])

    def test_import_and_restore_roundtrip(self):
        backup = self.folder / "before.json"
        self.run_unit(
            HWNtagProfileImport(),
            "-s",
            "8",
            "-f",
            str(self.source),
            "--apply",
            "--backup",
            str(backup),
        )
        self.assertEqual(self.device.flash[7], self.desired)
        self.run_unit(
            HWNtagProfileRestore(),
            "-s",
            "8",
            "-f",
            str(backup),
            "--apply",
            "--backup",
            str(self.folder / "recovery.json"),
        )
        self.assertNotIn(7, self.device.flash)
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_restore_requires_matching_explicit_slot(self):
        with self.assertRaises(ArgsParserError):
            self.run_unit(HWNtagProfileRestore(), "-s", "7", "-f", str(self.source))
        self.assertEqual(self.device.calls, [])

    def test_backup_can_capture_empty_target(self):
        output = self.folder / "empty.json"
        self.run_unit(HWNtagProfileBackup(), "-s", "8", "-o", str(output))
        self.assertEqual(load_snapshot(output).state, SlotState(0, False))
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_console_does_not_print_image_or_opaque_configuration(self):
        text = self.run_unit(HWNtagProfileImport(), "-s", "8", "-f", str(self.source))
        self.assertNotIn(self.desired.image.hex(), text)
        self.assertNotIn(self.desired.config.raw.hex(), text)
