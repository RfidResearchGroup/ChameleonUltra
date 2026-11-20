"""CLI integration and storage failures must not become target mutations."""

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.main import TestProgram
from unittest.mock import patch

from chameleon_enum import Command
from chameleon_cli_unit import (
    HWSlotProfileBackup,
    HWSlotProfileImport,
    HWSlotProfileRestore,
)
from chameleon_utils import ArgsParserError
from slot_profile import SlotProfileManager
from slot_profile_data import SlotProfileError, SlotSnapshot, SlotState
from slot_profile_io import load_snapshot, save_snapshot
from tests.slot_profile_fixtures import FakeSlots, profile
from tests.test_slot_profile import card_document

ERRORS: tuple[type[Exception], ...] = (SlotProfileError, OSError)


def unit_for(cls, device):
    unit = cls()
    unit.device_com = device
    return unit


def execute(unit, arguments):
    args = unit.args_parser().parse_args(arguments)
    capture = io.StringIO()
    try:
        with contextlib.redirect_stdout(capture):
            if unit.before_exec(args):
                unit.on_exec(args)
    except BaseException as error:
        error.__dict__["cli_output"] = capture.getvalue()
        raise
    return capture.getvalue()


class BrokenTemporary:
    def __init__(self, inner, phase):
        self.inner = inner
        self.phase = phase

    def __enter__(self):
        self.inner.__enter__()
        return self

    def __exit__(self, *args):
        value = self.inner.__exit__(*args)
        if self.phase == "close":
            raise OSError("close failed")
        return value

    def __getattr__(self, key):
        return getattr(self.inner, key)

    def write(self, content):
        if self.phase == "short":
            self.inner.write(content[:8])
            return 8
        return self.inner.write(content)

    def flush(self):
        if self.phase == "flush":
            raise OSError("flush failed")
        return self.inner.flush()


class SlotStorageTest(unittest.TestCase):
    def test_storage_faults_leave_no_published_backup_and_no_data_setters(self):
        native = tempfile.NamedTemporaryFile
        for phase in ("short", "flush", "sync", "close"):
            device = FakeSlots()
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile())

            def temporary(*args, **kwargs):
                return BrokenTemporary(native(*args, **kwargs), phase)

            with tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "before.json"
                with contextlib.ExitStack() as stack:
                    stack.enter_context(
                        patch(
                            "slot_profile_io.tempfile.NamedTemporaryFile",
                            side_effect=temporary,
                        )
                    )
                    if phase == "sync":
                        stack.enter_context(
                            patch(
                                "slot_profile_io.os.fsync",
                                side_effect=OSError("sync failed"),
                            )
                        )
                    with self.subTest(phase=phase), self.assertRaises(ERRORS):
                        manager.apply(plan, target)
                self.assertFalse(target.exists())
                self.assertEqual(list(Path(directory).iterdir()), [])
            self.assertEqual(device.setters(), [])
            self.assertFalse(manager.progress.possibly_changed)
            self.assertEqual((device.active, device.reader), (2, False))

    def test_publication_race_preserves_competing_file(self):
        device = FakeSlots()
        manager = SlotProfileManager(device)
        plan = manager.plan_import(8, profile())
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.json"

            def race(source, destination):
                Path(destination).write_bytes(b"competing owner")
                raise FileExistsError("destination appeared")

            # This proxy tests Windows no-clobber publication independently of
            # the host OS, without changing pathlib's process-wide os.name.
            fake_os = SimpleNamespace(
                name="nt", fsync=os.fsync, rename=race, link=os.link
            )
            with (
                patch("slot_profile_io.os", fake_os),
                self.assertRaises(FileExistsError),
            ):
                manager.apply(plan, target)
            self.assertEqual(target.read_bytes(), b"competing owner")
            self.assertEqual(device.setters(), [])

    def test_posix_link_refuses_existing_destination(self):
        value = SlotSnapshot("ab" * 32, 8, SlotState(0, False))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.json"
            fake_os = SimpleNamespace(
                name="posix", fsync=os.fsync, rename=os.rename, link=os.link
            )
            with patch("slot_profile_io.os", fake_os):
                save_snapshot(value, target)
                first = target.read_bytes()
                with self.assertRaises(SlotProfileError):
                    save_snapshot(value, target)
            self.assertEqual(target.read_bytes(), first)
            self.assertEqual(load_snapshot(target), value)


class SlotCliTest(unittest.TestCase):
    def test_preview_apply_backup_and_restore_use_same_device_and_slot(self):
        device = FakeSlots()
        originals = dict(device.flash)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "image.json"
            source.write_text(json.dumps(card_document()), encoding="utf-8")
            before = root / "before.json"
            importer = unit_for(HWSlotProfileImport, device)
            output = execute(importer, ["-s", "8", "-f", str(source)])
            self.assertIn("Preview only", output)
            self.assertEqual(device.setters(), [])
            output = execute(
                importer,
                ["-s", "8", "-f", str(source), "--apply", "--backup", str(before)],
            )
            self.assertIn("verified after slot reload", output)
            self.assertNotIn(profile().image.hex(), output)
            self.assertEqual(device.flash[7], profile())
            restorer = unit_for(HWSlotProfileRestore, device)
            execute(
                restorer,
                [
                    "-s",
                    "8",
                    "-f",
                    str(before),
                    "--apply",
                    "--backup",
                    str(root / "before-restore.json"),
                ],
            )
            self.assertEqual(device.flash, originals)
            self.assertEqual((device.active, device.reader), (2, False))

    def test_invalid_source_digest_or_snapshot_slot_fails_before_any_device_call(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            source.write_text(
                json.dumps({**card_document(), "sha256": "00" * 32}), encoding="utf-8"
            )
            device = FakeSlots()
            unit = unit_for(HWSlotProfileImport, device)
            with self.assertRaises(ArgsParserError):
                execute(unit, ["-s", "8", "-f", str(source)])
            self.assertEqual(device.calls, [])
            save_snapshot(
                SlotSnapshot("ab" * 32, 7, SlotState(0, False)),
                Path(directory) / "snapshot.json",
            )
            with self.assertRaises(ArgsParserError):
                execute(
                    unit_for(HWSlotProfileRestore, device),
                    ["-s", "8", "-f", str(Path(directory) / "snapshot.json")],
                )
            self.assertEqual(device.calls, [])

    def test_apply_missing_or_existing_backup_refuses_before_any_call(self):
        device = FakeSlots()
        unit = unit_for(HWSlotProfileImport, device)
        with self.assertRaises(ArgsParserError):
            execute(unit, ["-s", "8", "-f", "unused.json", "--apply"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "existing.json"
            path.write_text("existing", encoding="utf-8")
            with self.assertRaises(ArgsParserError):
                execute(
                    unit,
                    ["-s", "8", "-f", "unused.json", "--apply", "--backup", str(path)],
                )
        self.assertEqual(device.calls, [])

    def test_backup_command_refuses_existing_file_and_does_not_change_profile(self):
        device = FakeSlots()
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "empty.json"
            unit = unit_for(HWSlotProfileBackup, device)
            execute(unit, ["-s", "8", "-o", str(target)])
            self.assertEqual(load_snapshot(target).state, SlotState(0, False))
            count = len(device.calls)
            with self.assertRaises(ArgsParserError):
                execute(unit, ["-s", "8", "-o", str(target)])
            self.assertEqual(len(device.calls), count)
        self.assertEqual(device.setters(), [])

    def test_parser_scope_and_missing_capability(self):
        unit = HWSlotProfileImport()
        for args in (
            ["-s", "0", "-f", "x"],
            ["-s", "9", "-f", "x"],
            ["-s", "8", "-f", "x", "--uid", "11223344"],
            ["-s", "8", "-f", "x", "--format", "hex"],
        ):
            with self.subTest(args=args), self.assertRaises(ArgsParserError):
                unit.args_parser().parse_args(args)
        device = FakeSlots()
        device.commands = []
        with self.assertRaises(ArgsParserError):
            execute(unit_for(HWSlotProfileImport, device), ["-s", "8", "-f", "unused"])
        self.assertEqual(device.calls, [])

    def test_mode_cleanup_failure_reports_verified_data_and_backup(self):
        device = FakeSlots()

        def fail(fake, cmd, data):
            if cmd == Command.CHANGE_DEVICE_MODE and data == b"\x00" and fake.saves:
                return TimeoutError("mode restore failed")
            return None

        device.fault = fail
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            source.write_text(json.dumps(card_document()), encoding="utf-8")
            with self.assertRaises(ArgsParserError) as caught:
                execute(
                    unit_for(HWSlotProfileImport, device),
                    [
                        "-s",
                        "8",
                        "-f",
                        str(source),
                        "--apply",
                        "--backup",
                        str(root / "before.json"),
                    ],
                )
            self.assertIn("data_verified=True", str(caught.exception))
            self.assertIn("before.json", str(caught.exception))
            self.assertIn("mode restore failed", str(caught.exception))
        self.assertEqual(device.active, 2)
        self.assertTrue(device.reader)

    def test_cancel_keeps_original_type_reports_progress_and_restores_context(self):
        device = FakeSlots()
        device.fault = (
            lambda fake, cmd, data: KeyboardInterrupt()
            if cmd == Command.MF1_WRITE_EMU_BLOCK_DATA
            else None
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            source.write_text(json.dumps(card_document()), encoding="utf-8")
            output = io.StringIO()
            with (
                contextlib.redirect_stdout(output),
                self.assertRaises(KeyboardInterrupt) as caught,
            ):
                execute(
                    unit_for(HWSlotProfileImport, device),
                    [
                        "-s",
                        "8",
                        "-f",
                        str(source),
                        "--apply",
                        "--backup",
                        str(root / "before.json"),
                    ],
                )
            self.assertIn("Interrupted", getattr(caught.exception, "cli_output"))
            self.assertIn(
                "target_may_have_changed=True", getattr(caught.exception, "cli_output")
            )
            self.assertIn("before.json", getattr(caught.exception, "cli_output"))
        self.assertEqual((device.active, device.reader), (2, False))


if __name__ == "__main__":
    TestProgram()
