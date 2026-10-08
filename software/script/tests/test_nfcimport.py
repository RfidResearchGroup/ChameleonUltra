"""Offline regression tests for the Flipper NFC importer restored from #357."""

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import main
from unittest.mock import Mock, call

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from chameleon_cli_unit import HFMFUNfcImport, hf_mfu  # noqa: E402
from chameleon_cmd import ChameleonCMD  # noqa: E402
from chameleon_enum import SlotNumber, TagSenseType, TagSpecificType  # noqa: E402


class NfcImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "synthetic tag.nfc"
        self.unit = HFMFUNfcImport()
        self.unit.slot_num = SlotNumber.SLOT_8
        self.cmd = Mock(spec=ChameleonCMD)
        self.cmd.mfu_get_emu_pages_count.return_value = 135
        self.unit._device_cmd = self.cmd

    def fixture(self, device_type="NTAG215", uid="04 01 02 03 04 05 06", count=135):
        # All payload bytes are generated here, not taken from a physical tag.
        lines = [
            "Filetype: Flipper NFC device",
            "Version: 2",
            f"Device type: {device_type}",
            f"UID: {uid}",
            "ATQA: 44 00",
            "SAK: 00",
            "Mifare version: 00 04 04 02 01 00 11 03",
            "Signature: " + " ".join(["00"] * 32),
            "Counter 2: 123",
            "Tearing 2: BD",
            f"Pages total: {count}",
        ]
        lines += [f"Page {p}: " + bytes([p % 256] * 4).hex(" ") for p in range(count)]
        self.path.write_text("\n".join(lines), encoding="utf-8")

    def run_import(self, amiibo=False):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.unit.on_exec(argparse.Namespace(file=str(self.path), amiibo=amiibo))
        return output.getvalue()

    def test_command_registered_and_amiibo_flag_parses(self):
        node = next(c for c in hf_mfu.children if c.name == "nfcimport")
        self.assertIs(node.cls, HFMFUNfcImport)
        args = self.unit.args_parser().parse_args(
            ["-s", "8", "-f", str(self.path), "--amiibo"]
        )
        self.assertTrue(args.amiibo)
        self.assertEqual(args.file, str(self.path))

    def test_amiibo_overrides_pwd_and_pack_after_import(self):
        self.fixture()
        self.run_import(amiibo=True)
        writes = self.cmd.mfu_write_emu_page_data.call_args_list
        self.assertEqual(
            writes[-2:],
            [
                call(133, bytes.fromhex("a853ac57")),
                call(134, bytes.fromhex("80800000")),
            ],
        )
        self.cmd.set_slot_tag_type.assert_called_once_with(
            SlotNumber.SLOT_8, TagSpecificType.NTAG_215
        )
        self.cmd.set_slot_enable.assert_called_once_with(
            SlotNumber.SLOT_8, TagSenseType.HF, True
        )

    def test_normal_import_preserves_pages_and_batches(self):
        self.fixture()
        self.run_import()
        writes = self.cmd.mfu_write_emu_page_data.call_args_list
        self.assertEqual([c.args[0] for c in writes], list(range(0, 135, 16)))
        self.assertTrue(all(len(c.args[1]) <= 64 for c in writes))
        actual = b"".join(c.args[1] for c in writes)
        self.assertEqual(actual, b"".join(bytes([p] * 4) for p in range(135)))
        self.cmd.hf14a_set_anti_coll_data.assert_called_once_with(
            bytes.fromhex("04010203040506"), bytes.fromhex("4400"), b"\x00"
        )
        self.cmd.mf0_ntag_set_version_data.assert_called_once_with(
            bytes.fromhex("0004040201001103")
        )
        self.cmd.mf0_ntag_set_signature_data.assert_called_once_with(bytes(32))
        self.cmd.mfu_write_emu_counter_data.assert_called_once_with(0, 123, True)

    def test_amiibo_ignored_for_other_tag_type(self):
        self.fixture(device_type="NTAG213", count=45)
        self.cmd.mfu_get_emu_pages_count.return_value = 45
        self.assertIn("--amiibo flag ignored", self.run_import(amiibo=True))
        self.assertTrue(
            all(c.args[0] < 45 for c in self.cmd.mfu_write_emu_page_data.call_args_list)
        )

    def test_amiibo_ignored_for_wrong_uid_length(self):
        self.fixture(uid="01 02 03 04")
        self.assertIn("UID is not 7 bytes", self.run_import(amiibo=True))
        self.assertEqual(self.cmd.mfu_write_emu_page_data.call_count, 9)

    def test_missing_file_does_not_write_device(self):
        self.assertIn("File not found", self.run_import())
        self.assertEqual(self.cmd.mock_calls, [])

    def test_unsupported_tag_does_not_write_device(self):
        self.fixture(device_type="Unknown tag")
        self.assertIn("Unsupported Flipper device type", self.run_import())
        self.assertEqual(self.cmd.mock_calls, [])

    def test_missing_uid_does_not_write_device(self):
        self.path.write_text("Device type: NTAG215\n", encoding="utf-8")
        self.assertIn("No 'UID'", self.run_import())
        self.assertEqual(self.cmd.mock_calls, [])


if __name__ == "__main__":
    main()
