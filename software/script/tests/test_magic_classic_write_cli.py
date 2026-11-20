"""Keep ordinary-data previews, apply gates and uncertain progress explicit."""

import contextlib
import hashlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest import TestProgram
from unittest.mock import Mock, call, patch

import chameleon_cli_unit as cli_module
from chameleon_cli_unit import HFMFMagicRestore, HFMFMagicWrbl
from chameleon_utils import ArgsParserError
from magic_classic import MagicCardReadError
from magic_classic_write import (
    MagicCardWriteError,
    MagicClassicTransaction,
    MagicWriteProgress,
    MagicWriteResult,
)
from magic_classic_write_io import MagicBackupReceipt, journal_path_for
from tests.test_magic_classic_cli import REQUIRED_COMMANDS, make_unit
from tests.test_magic_classic_write_io import (
    IMAGE,
    IMAGE_SHA256,
    make_plan,
    prepared_record,
    source_document,
    write_json,
)

DATA = bytes([0xA1]) * 16


def receipt_for(path):
    return MagicBackupReceipt(path.absolute(), "1" * 64, 1024, journal_path_for(path.absolute()))


def transaction_fixture(backup=None, *, changed=True):
    transaction = Mock(spec=MagicClassicTransaction)
    plan = make_plan((1, 2)) if changed else make_plan(())
    transaction.plan_block.return_value = plan
    transaction.plan_restore.return_value = plan
    transaction.progress = MagicWriteProgress()
    transaction.backup = backup
    transaction.apply.return_value = MagicWriteResult(
        MagicWriteProgress((1, 2), (1, 2), (1, 2)) if changed else MagicWriteProgress(),
        backup,
    )
    return transaction, plan


def arguments_for(unit, source, backup=None, *, apply=False, source_sha=IMAGE_SHA256):
    if isinstance(unit, HFMFMagicWrbl):
        arguments = ["-b", "1", "-d", DATA.hex()]
    else:
        arguments = ["-i", str(source)]
        if source_sha is not None:
            arguments.extend(["--sha256", source_sha])
    if apply:
        arguments.append("--apply")
    if backup is not None:
        arguments.extend(["--backup", str(backup)])
    return unit.args_parser().parse_args(arguments)


class MagicWriteParserTest(unittest.TestCase):
    def test_wrbl_allows_exactly_the_47_ordinary_data_addresses(self):
        parser = HFMFMagicWrbl().args_parser()
        allowed = [block for block in range(1, 64) if block % 4 != 3]
        self.assertEqual(len(allowed), 47)
        for block in allowed:
            with self.subTest(block=block):
                args = parser.parse_args(["-b", str(block), "-d", DATA.hex()])
                self.assertEqual(args.block, block)
                self.assertEqual(args.data, DATA)
                self.assertFalse(args.apply)
                self.assertIsNone(args.backup)
        for block in (0, -1, 64, 256, *range(3, 64, 4)):
            with self.subTest(block=block), self.assertRaises(ArgsParserError):
                parser.parse_args(["-b", str(block), "-d", DATA.hex()])

    def test_invalid_block_or_payload_fails_before_a_transaction_or_mode_change(self):
        for block, data in (
            ("0", DATA.hex()),
            ("3", DATA.hex()),
            ("1.0", DATA.hex()),
            ("true", DATA.hex()),
            ("1", ""),
            ("1", "00" * 15),
            ("1", "00" * 17),
            ("1", "GG" * 16),
            ("1", "00 " * 16),
        ):
            with self.subTest(block=block, length=len(data)):
                unit, device, command = make_unit(HFMFMagicWrbl)
                with (
                    patch.object(cli_module, "MagicClassicTransaction") as factory,
                    self.assertRaises(ArgsParserError),
                ):
                    unit.args_parser().parse_args(["-b", block, "-d", data])
                factory.assert_not_called()
                self.assertEqual(command.mock_calls, [])
                self.assertEqual(device.requests, [])

    def test_restore_defaults_to_preview_and_rejects_unknown_format(self):
        parser = HFMFMagicRestore().args_parser()
        args = parser.parse_args(["-i", "backup.bin"])
        self.assertFalse(args.apply)
        self.assertIsNone(args.backup)
        self.assertIsNone(args.format)
        self.assertIsNone(args.sha256)
        with self.assertRaises(ArgsParserError):
            parser.parse_args(["-i", "backup.bin", "--format", "hex"])
        for option in ("--uid", "--config", "--force", "--write-trailers"):
            with self.subTest(option=option), self.assertRaises(ArgsParserError):
                parser.parse_args(["-i", "backup.bin", option])


class MagicWritePreflightTest(unittest.TestCase):
    def test_missing_capability_rejects_before_mode_or_transaction(self):
        for unit_type in (HFMFMagicWrbl, HFMFMagicRestore):
            for missing in REQUIRED_COMMANDS:
                with self.subTest(unit=unit_type.__name__, missing=missing):
                    unit, device, command = make_unit(unit_type)
                    device.commands.remove(missing)
                    args = arguments_for(unit, Path("not-read.bin"))
                    with (
                        patch.object(cli_module, "MagicClassicTransaction") as factory,
                        self.assertRaises(ArgsParserError),
                    ):
                        unit.before_exec(args)
                    factory.assert_not_called()
                    self.assertEqual(command.mock_calls, [])
                    self.assertEqual(device.requests, [])

    def test_apply_requires_backup_even_for_programmatic_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(IMAGE)
            for unit_type in (HFMFMagicWrbl, HFMFMagicRestore):
                with self.subTest(unit=unit_type.__name__):
                    unit, device, command = make_unit(unit_type)
                    args = arguments_for(unit, source, apply=True)
                    with patch.object(cli_module, "MagicClassicTransaction") as factory:
                        with self.assertRaises(ArgsParserError):
                            unit.before_exec(args)
                        with self.assertRaises(ArgsParserError):
                            unit.on_exec(args)
                        factory.assert_not_called()
                    self.assertEqual(command.mock_calls, [])
                    self.assertEqual(device.requests, [])

    def test_existing_backup_or_journal_prevents_all_mode_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(IMAGE)
            backup = Path(directory) / "before.bin"
            for unit_type in (HFMFMagicWrbl, HFMFMagicRestore):
                for existing in (backup, journal_path_for(backup)):
                    with self.subTest(unit=unit_type.__name__, path=existing.name):
                        existing.write_bytes(b"keep this recovery evidence")
                        unit, device, command = make_unit(unit_type)
                        args = arguments_for(unit, source, backup, apply=True)
                        with patch.object(cli_module, "MagicClassicTransaction") as factory:
                            with self.assertRaises(ArgsParserError):
                                unit.before_exec(args)
                            with self.assertRaises(ArgsParserError):
                                unit.on_exec(args)
                            factory.assert_not_called()
                        self.assertEqual(existing.read_bytes(), b"keep this recovery evidence")
                        self.assertEqual(command.mock_calls, [])
                        self.assertEqual(device.requests, [])
                        existing.unlink()

    def test_missing_backup_directory_is_rejected_without_mode_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            unit, _, command = make_unit(HFMFMagicWrbl)
            backup = Path(directory) / "missing" / "before.bin"
            args = arguments_for(unit, Path("unused"), backup, apply=True)
            with self.assertRaises(ArgsParserError):
                unit.before_exec(args)
            self.assertEqual(command.mock_calls, [])
            self.assertFalse(backup.parent.exists())

    def test_invalid_restore_bytes_metadata_and_digest_fail_before_any_rf(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            cases = (
                b"not JSON",
                b"{}",
                bytes(1023),
                b"[]" * 513,
            )
            for content in cases:
                with self.subTest(length=len(content)):
                    source.write_bytes(content)
                    unit, device, command = make_unit(HFMFMagicRestore)
                    args = arguments_for(unit, source)
                    with (
                        patch.object(cli_module, "MagicClassicTransaction") as factory,
                        self.assertRaises(ArgsParserError),
                    ):
                        self.assertTrue(unit.before_exec(args))
                        unit.on_exec(args)
                    factory.assert_not_called()
                    self.assertEqual(command.mock_calls, [])
                    self.assertEqual(device.requests, [])
            for field, value in (
                ("read_profile", "unknown"),
                ("uid", "AABBCCDD"),
                ("sha256", "0" * 64),
                ("block_count", 63),
            ):
                with self.subTest(field=field):
                    document = source_document()
                    document[field] = value
                    write_json(source, document)
                    unit, _, command = make_unit(HFMFMagicRestore)
                    args = arguments_for(unit, source)
                    with (
                        patch.object(cli_module, "MagicClassicTransaction") as factory,
                        self.assertRaises(ArgsParserError),
                    ):
                        unit.on_exec(args)
                    factory.assert_not_called()
                    self.assertEqual(command.mock_calls, [])

    def test_binary_without_digest_or_with_damaged_journal_is_rejected_before_rf(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "before.bin"
            source.write_bytes(IMAGE)
            for bad_journal in (None, b'{"incomplete":'):
                with self.subTest(journal_present=bad_journal is not None):
                    if bad_journal is not None:
                        journal_path_for(source).write_bytes(bad_journal)
                    unit, device, command = make_unit(HFMFMagicRestore)
                    args = arguments_for(
                        unit,
                        source,
                        source_sha=None if bad_journal is None else IMAGE_SHA256,
                    )
                    with (
                        patch.object(cli_module, "MagicClassicTransaction") as factory,
                        self.assertRaises(ArgsParserError),
                    ):
                        self.assertTrue(unit.before_exec(args))
                        unit.on_exec(args)
                    factory.assert_not_called()
                    self.assertEqual(command.mock_calls, [])
                    self.assertEqual(device.requests, [])

    def test_ordinary_area_digest_mismatch_fails_before_mode_or_write(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "before.bin"
            damaged = bytearray(IMAGE)
            damaged[16] ^= 1
            source.write_bytes(damaged)
            write_json(journal_path_for(source), prepared_record())
            # Each line must be complete; write_json writes one complete JSON
            # document without a delimiter, so append the journal newline.
            with journal_path_for(source).open("ab") as stream:
                stream.write(b"\n")
            unit, device, command = make_unit(HFMFMagicRestore)
            args = arguments_for(
                unit, source, source_sha=hashlib.sha256(damaged).hexdigest()
            )
            with (
                patch.object(cli_module, "MagicClassicTransaction") as factory,
                self.assertRaises(ArgsParserError),
            ):
                unit.on_exec(args)
            factory.assert_not_called()
            self.assertEqual(command.mock_calls, [])
            self.assertEqual(device.requests, [])


class MagicWriteExecutionTest(unittest.TestCase):
    def test_default_preview_plans_but_never_applies_or_exposes_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(IMAGE)
            for unit_type in (HFMFMagicWrbl, HFMFMagicRestore):
                with self.subTest(unit=unit_type.__name__):
                    unit, _, command = make_unit(unit_type)
                    transaction, plan = transaction_fixture()
                    args = arguments_for(unit, source)
                    output = io.StringIO()
                    with (
                        patch.object(
                            cli_module, "MagicClassicTransaction", return_value=transaction
                        ),
                        contextlib.redirect_stdout(output),
                    ):
                        self.assertTrue(unit.before_exec(args))
                        unit.on_exec(args)
                    transaction.apply.assert_not_called()
                    if unit_type is HFMFMagicWrbl:
                        transaction.plan_block.assert_called_once_with(1, DATA)
                        transaction.plan_restore.assert_not_called()
                    else:
                        transaction.plan_restore.assert_called_once_with(IMAGE)
                        transaction.plan_block.assert_not_called()
                    self.assertEqual(
                        command.set_device_reader_mode.call_args_list,
                        [call(True), call(False)],
                    )
                    self.assertIn("Preview only", output.getvalue())
                    self.assertIn("[1, 2]", output.getvalue())
                    self.assertNotIn(DATA.hex().upper(), output.getvalue())
                    self.assertNotIn(plan.original_image.hex().upper(), output.getvalue())
                    self.assertEqual(list(Path(directory).iterdir()), [source])

    def test_valid_json_source_is_passed_through_without_key_reconstruction(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            write_json(source, source_document())
            unit, _, _ = make_unit(HFMFMagicRestore)
            transaction, _ = transaction_fixture()
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                unit.on_exec(arguments_for(unit, source))
            transaction.plan_restore.assert_called_once_with(IMAGE)
            transaction.apply.assert_not_called()

    def test_explicit_apply_uses_the_plan_and_reports_only_verified_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(IMAGE)
            backup = Path(directory) / "before.bin"
            receipt = receipt_for(backup)
            for unit_type in (HFMFMagicWrbl, HFMFMagicRestore):
                with self.subTest(unit=unit_type.__name__):
                    unit, _, command = make_unit(unit_type)
                    transaction, plan = transaction_fixture(receipt)
                    args = arguments_for(unit, source, backup, apply=True)
                    output = io.StringIO()
                    with (
                        patch.object(
                            cli_module, "MagicClassicTransaction", return_value=transaction
                        ),
                        contextlib.redirect_stdout(output),
                    ):
                        self.assertTrue(unit.before_exec(args))
                        unit.on_exec(args)
                    transaction.apply.assert_called_once_with(plan, backup_path=backup)
                    self.assertEqual(
                        command.set_device_reader_mode.call_args_list,
                        [call(True), call(False)],
                    )
                    self.assertIn("Independently verified 2", output.getvalue())
                    self.assertIn(str(receipt.path), output.getvalue())
                    self.assertIn(str(receipt.journal_path), output.getvalue())
                    self.assertNotIn(DATA.hex().upper(), output.getvalue())

    def test_no_change_result_reports_no_programming_and_creates_no_files(self):
        with tempfile.TemporaryDirectory() as directory:
            unit, _, command = make_unit(HFMFMagicWrbl)
            transaction, plan = transaction_fixture(changed=False)
            backup = Path(directory) / "before.bin"
            args = arguments_for(unit, Path("unused"), backup, apply=True)
            output = io.StringIO()
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(output),
            ):
                unit.on_exec(args)
            transaction.apply.assert_called_once_with(plan, backup_path=backup)
            self.assertIn("No differing ordinary data blocks", output.getvalue())
            self.assertNotIn("final complete image verified", output.getvalue())
            self.assertEqual(list(Path(directory).iterdir()), [])
            self.assertEqual(
                command.set_device_reader_mode.call_args_list, [call(True), call(False)]
            )

    def test_original_reader_mode_is_preserved(self):
        unit, _, command = make_unit(HFMFMagicWrbl, reader_mode=True)
        transaction, _ = transaction_fixture()
        with (
            patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            unit.on_exec(arguments_for(unit, Path("unused")))
        command.set_device_reader_mode.assert_not_called()
        command.is_device_reader_mode.assert_called_once_with()

    def test_protected_restore_difference_is_reported_without_calling_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(IMAGE)
            unit, _, command = make_unit(HFMFMagicRestore)
            transaction, _ = transaction_fixture()
            transaction.plan_restore.side_effect = MagicCardReadError("protected block 3 differs")
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                self.assertRaises(ArgsParserError) as raised,
            ):
                unit.on_exec(arguments_for(unit, source))
            transaction.apply.assert_not_called()
            self.assertIn("protected block 3", str(raised.exception))
            self.assertIn("attempted=[]", str(raised.exception))
            self.assertEqual(
                command.set_device_reader_mode.call_args_list, [call(True), call(False)]
            )

    def test_error_report_retains_uncertain_blocks_backup_and_original_cause(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / "before.bin"
            receipt = receipt_for(backup)
            unit, _, command = make_unit(HFMFMagicWrbl)
            transaction, _ = transaction_fixture(receipt)
            progress = MagicWriteProgress((1, 2), (1, 2), (1,), "data-frame", 2)
            transaction.progress = progress
            failure = MagicCardWriteError("data acknowledgement lost", progress=progress, backup=receipt)
            transport_error = TimeoutError("transport deadline")
            failure.__cause__ = transport_error
            transaction.apply.side_effect = failure
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(io.StringIO()),
                self.assertRaises(ArgsParserError) as raised,
            ):
                unit.on_exec(arguments_for(unit, Path("unused"), backup, apply=True))
            self.assertIs(raised.exception.__cause__, failure)
            self.assertIs(failure.__cause__, transport_error)
            self.assertEqual(failure.progress, progress)
            self.assertEqual(failure.backup, receipt)
            message = str(raised.exception)
            for fragment in (
                "attempted=[1, 2]",
                "possibly_written=[1, 2]",
                "independently_verified=[1]",
                "stage=data-frame",
                "block=2",
                str(receipt.path),
                str(receipt.journal_path),
            ):
                self.assertIn(fragment, message)
            self.assertEqual(
                command.set_device_reader_mode.call_args_list, [call(True), call(False)]
            )

    def test_mode_restore_error_cannot_erase_write_progress_or_exception_attrs(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / "before.bin"
            receipt = receipt_for(backup)
            unit, _, command = make_unit(HFMFMagicWrbl)
            transaction, _ = transaction_fixture(receipt)
            progress = MagicWriteProgress((1,), (1,), (), "verify-readback", 1)
            transaction.progress = progress
            original = MagicCardWriteError("readback mismatch", progress=progress, backup=receipt)
            cleanup = OSError("mode restore failed")
            transaction.apply.side_effect = original
            command.set_device_reader_mode.side_effect = [None, cleanup]
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(io.StringIO()),
                self.assertRaises(ArgsParserError) as raised,
            ):
                unit.on_exec(arguments_for(unit, Path("unused"), backup, apply=True))
            self.assertIs(raised.exception.__cause__, original)
            self.assertIs(original.__dict__["mode_cleanup_error"], cleanup)
            self.assertEqual(original.progress, progress)
            self.assertEqual(original.backup, receipt)
            self.assertIn("mode restore failed", str(raised.exception))
            self.assertIn("possibly_written=[1]", str(raised.exception))
            self.assertIn(str(receipt.path), str(raised.exception))

    def test_keyboard_interrupt_reports_progress_after_mode_restore_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / "before.bin"
            receipt = receipt_for(backup)
            unit, _, command = make_unit(HFMFMagicWrbl)
            transaction, _ = transaction_fixture(receipt)
            transaction.progress = MagicWriteProgress((1,), (1,), (), "data-frame", 1)
            cancellation = KeyboardInterrupt("cancelled")
            transaction.apply.side_effect = cancellation
            cleanup = OSError("mode restore failed")
            command.set_device_reader_mode.side_effect = [None, cleanup]
            output = io.StringIO()
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(output),
                self.assertRaises(KeyboardInterrupt) as raised,
            ):
                unit.on_exec(arguments_for(unit, Path("unused"), backup, apply=True))
            self.assertIs(raised.exception, cancellation)
            self.assertIs(cancellation.__dict__["mode_cleanup_error"], cleanup)
            self.assertIn("Interrupted", output.getvalue())
            self.assertIn("possibly_written=[1]", output.getvalue())
            self.assertIn(str(receipt.journal_path), output.getvalue())
            self.assertEqual(
                command.set_device_reader_mode.call_args_list, [call(True), call(False)]
            )

    def test_completed_apply_followed_by_mode_error_still_reports_verified_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / "before.bin"
            receipt = receipt_for(backup)
            unit, _, command = make_unit(HFMFMagicWrbl)
            transaction, _ = transaction_fixture(receipt)
            transaction.progress = MagicWriteProgress((1, 2), (1, 2), (1, 2))
            command.set_device_reader_mode.side_effect = [None, OSError("restore failed")]
            output = io.StringIO()
            with (
                patch.object(cli_module, "MagicClassicTransaction", return_value=transaction),
                contextlib.redirect_stdout(output),
                self.assertRaises(ArgsParserError) as raised,
            ):
                unit.on_exec(arguments_for(unit, Path("unused"), backup, apply=True))
            self.assertIn("independently_verified=[1, 2]", str(raised.exception))
            self.assertIn("restore failed", str(raised.exception))
            self.assertIn(str(receipt.path), str(raised.exception))
            self.assertNotIn("final complete image verified", output.getvalue())


if __name__ == "__main__":
    TestProgram()
