"""Test the raw-response API and complete, non-overwriting magic-card exports."""

import contextlib
import hashlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import TestProgram
from unittest.mock import MagicMock, Mock, call, patch

import chameleon_cli_unit as cli_module
import magic_classic_io as backup_io
from chameleon_cli_unit import HFMFMagicDump, HFMFMagicInfo
from chameleon_cmd import ChameleonCMD
from chameleon_com import ChameleonCom, Response
from chameleon_enum import Command, Status
from chameleon_utils import ArgsParserError, UnexpectedResponseError
from magic_classic import (
    MagicCardDump,
    MagicCardIdentity,
    MagicCardInfo,
    MagicCardReadError,
)

REQUIRED_COMMANDS = {
    Command.HF14A_RAW,
    Command.HF14A_SCAN,
    Command.GET_DEVICE_MODE,
    Command.CHANGE_DEVICE_MODE,
}
IDENTITY = MagicCardIdentity(b"\x01\x02\x03\x04", b"\x04\x00", b"\x08")
BLOCK_ZERO = bytes.fromhex("01020304040804001122334455667788")
IMAGE = BLOCK_ZERO + bytes((index * 37 + 11) % 256 for index in range(1008))


def dump_result() -> MagicCardDump:
    return MagicCardDump(IDENTITY, IMAGE)


class RecordingCom(ChameleonCom):
    """No serial port or thread is started by this transport fixture."""

    def __init__(self):
        super().__init__()
        self.commands = list(REQUIRED_COMMANDS)
        self.opened = True
        self.requests = []
        self.response = Response(Command.HF14A_RAW, Status.HF_TAG_OK, b"\x0a")

    def isOpen(self):
        return self.opened

    def send_cmd_sync(self, cmd, data=None, status=0, timeout=3):
        self.requests.append((cmd, data, status, timeout))
        return self.response


def make_unit(unit_type, *, reader_mode=False):
    unit = unit_type()
    device = RecordingCom()
    unit.device_com = device
    command = Mock(spec=ChameleonCMD)
    command.is_device_reader_mode.return_value = reader_mode
    unit._device_cmd = command
    return unit, device, command


def mock_reader():
    reader = Mock()
    reader.read_info.return_value = MagicCardInfo(IDENTITY, BLOCK_ZERO)
    reader.dump.return_value = dump_result()
    return reader


class RawResponseTest(unittest.TestCase):
    def test_response_api_preserves_status_and_legacy_api_returns_bytes(self):
        options = {
            "activate_rf_field": 1,
            "wait_response": 1,
            "append_crc": 0,
            "auto_select": 0,
            "keep_rf_field": 1,
            "check_response_crc": 0,
        }
        for status in (Status.HF_TAG_OK, Status.HF_ERR_CRC, Status.HF_COLLISION):
            with self.subTest(status=status):
                device = RecordingCom()
                device.response = Response(Command.HF14A_RAW, status, b"\x0a")
                command = ChameleonCMD(device)
                result = command.hf14a_raw_response(
                    options, resp_timeout_ms=500, data=b"\x40", bitlen=7
                )
                self.assertIs(result, device.response)
                self.assertEqual(result.status, status)
                self.assertEqual(
                    device.requests,
                    [(Command.HF14A_RAW, b"\xc8\x01\xf4\x00\x07\x40", 0, 1)],
                )
                legacy = command.hf14a_raw(
                    options, resp_timeout_ms=500, data=b"\x40", bitlen=7
                )
                self.assertEqual(legacy, b"\x0a")
                self.assertEqual(device.requests[0], device.requests[1])

    def test_halt_and_crc_read_use_the_existing_wire_flags(self):
        cases = (
            (
                {
                    "activate_rf_field": 0,
                    "wait_response": 0,
                    "append_crc": 1,
                    "auto_select": 1,
                    "keep_rf_field": 1,
                    "check_response_crc": 0,
                },
                b"\x50\x00",
                b"\x38\x00\x64\x00\x10\x50\x00",
            ),
            (
                {
                    "activate_rf_field": 0,
                    "wait_response": 1,
                    "append_crc": 1,
                    "auto_select": 0,
                    "keep_rf_field": 1,
                    "check_response_crc": 1,
                },
                b"\x30\x3f",
                b"\x6c\x00\x64\x00\x10\x30\x3f",
            ),
        )
        for options, data, packet in cases:
            with self.subTest(packet=packet.hex()):
                device = RecordingCom()
                ChameleonCMD(device).hf14a_raw_response(options, data=data)
                self.assertEqual(
                    device.requests, [(Command.HF14A_RAW, packet, 0, 1)]
                )

    def test_invalid_partial_frame_does_not_send_a_request(self):
        options = {
            "activate_rf_field": 1,
            "wait_response": 1,
            "append_crc": 0,
            "auto_select": 0,
            "keep_rf_field": 0,
            "check_response_crc": 0,
        }
        for data, bitlen in ((b"", 7), (b"\x40", 0), (b"\x40", 9)):
            with self.subTest(data=data, bitlen=bitlen):
                device = RecordingCom()
                with self.assertRaises(ValueError):
                    ChameleonCMD(device).hf14a_raw_response(
                        options, data=data, bitlen=bitlen
                    )
                self.assertEqual(device.requests, [])


class MagicCliTest(unittest.TestCase):
    def test_missing_capability_is_rejected_without_a_device_command(self):
        for missing in REQUIRED_COMMANDS:
            with self.subTest(missing=missing):
                unit, device, command = make_unit(HFMFMagicInfo)
                device.commands.remove(missing)
                args = unit.args_parser().parse_args([])
                with self.assertRaises(ArgsParserError):
                    unit.before_exec(args)
                self.assertEqual(command.mock_calls, [])
                self.assertEqual(device.requests, [])

    def test_disconnected_device_does_not_start_a_reader_session(self):
        unit, device, command = make_unit(HFMFMagicInfo)
        device.opened = False
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(unit.before_exec(unit.args_parser().parse_args([])))
        self.assertEqual(command.mock_calls, [])

    def test_existing_target_prevents_acquisition_even_for_direct_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "existing.bin"
            target.write_bytes(b"existing backup")
            unit, device, command = make_unit(HFMFMagicDump)
            args = unit.args_parser().parse_args(["-o", str(target)])
            with patch.object(cli_module, "MagicClassicReader") as factory:
                with self.assertRaises(ArgsParserError):
                    unit.before_exec(args)
                with self.assertRaises(ArgsParserError):
                    unit.on_exec(args)
                factory.assert_not_called()
            self.assertEqual(command.mock_calls, [])
            self.assertEqual(device.requests, [])
            self.assertEqual(target.read_bytes(), b"existing backup")
            self.assertEqual(list(Path(directory).iterdir()), [target])

    def test_missing_output_directory_fails_before_mode_change(self):
        with tempfile.TemporaryDirectory() as directory:
            unit, device, command = make_unit(HFMFMagicDump)
            target = Path(directory) / "absent" / "backup.bin"
            args = unit.args_parser().parse_args(["-o", str(target)])
            with self.assertRaises(ArgsParserError):
                unit.before_exec(args)
            self.assertEqual(command.mock_calls, [])
            self.assertEqual(device.requests, [])

    def test_info_restores_original_mode_and_reports_only_observed_capability(self):
        unit, _, command = make_unit(HFMFMagicInfo)
        reader = mock_reader()
        output = io.StringIO()
        args = unit.args_parser().parse_args([])
        with (
            patch.object(cli_module, "MagicClassicReader", return_value=reader),
            contextlib.redirect_stdout(output),
        ):
            self.assertTrue(unit.before_exec(args))
            unit.on_exec(args)
        self.assertEqual(
            command.set_device_reader_mode.call_args_list, [call(True), call(False)]
        )
        reader.read_info.assert_called_once_with()
        reader.dump.assert_not_called()
        self.assertIn("Gen1A-compatible read backdoor: verified", output.getvalue())
        self.assertIn("01020304", output.getvalue())
        self.assertNotIn("Gen3", output.getvalue())

    def test_existing_reader_mode_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            for unit_type in (HFMFMagicInfo, HFMFMagicDump):
                with self.subTest(unit=unit_type.__name__):
                    unit, _, command = make_unit(unit_type, reader_mode=True)
                    target = Path(directory) / "backup.bin"
                    arguments = [] if unit_type is HFMFMagicInfo else ["-o", str(target)]
                    args = unit.args_parser().parse_args(arguments)
                    with (
                        patch.object(
                            cli_module, "MagicClassicReader", return_value=mock_reader()
                        ),
                        contextlib.redirect_stdout(io.StringIO()),
                    ):
                        unit.on_exec(args)
                    command.set_device_reader_mode.assert_not_called()
                    command.is_device_reader_mode.assert_called_once_with()

    def test_acquisition_errors_restore_mode_and_do_not_publish(self):
        failures = (
            MagicCardReadError("read disagreement"),
            TimeoutError("transport deadline"),
            UnexpectedResponseError("RF CRC error"),
        )
        with tempfile.TemporaryDirectory() as directory:
            for failure in failures:
                for unit_type in (HFMFMagicInfo, HFMFMagicDump):
                    with self.subTest(error=type(failure), unit=unit_type.__name__):
                        target = Path(directory) / "backup.bin"
                        unit, _, command = make_unit(unit_type)
                        reader = mock_reader()
                        reader.read_info.side_effect = failure
                        reader.dump.side_effect = failure
                        arguments = (
                            [] if unit_type is HFMFMagicInfo else ["-o", str(target)]
                        )
                        args = unit.args_parser().parse_args(arguments)
                        with (
                            patch.object(
                                cli_module, "MagicClassicReader", return_value=reader
                            ),
                            self.assertRaises(ArgsParserError),
                        ):
                            unit.on_exec(args)
                        self.assertEqual(
                            command.set_device_reader_mode.call_args_list,
                            [call(True), call(False)],
                        )
                        self.assertEqual(list(Path(directory).iterdir()), [])

    def test_keyboard_interrupt_also_restores_mode(self):
        unit, _, command = make_unit(HFMFMagicInfo)
        reader = mock_reader()
        reader.read_info.side_effect = KeyboardInterrupt()
        with (
            patch.object(cli_module, "MagicClassicReader", return_value=reader),
            self.assertRaises(KeyboardInterrupt),
        ):
            unit.on_exec(unit.args_parser().parse_args([]))
        self.assertEqual(
            command.set_device_reader_mode.call_args_list, [call(True), call(False)]
        )

    def test_mode_entry_failure_still_attempts_original_mode(self):
        unit, _, command = make_unit(HFMFMagicInfo)
        command.set_device_reader_mode.side_effect = [TimeoutError("mode entry"), None]
        with (
            patch.object(cli_module, "MagicClassicReader") as factory,
            self.assertRaises(ArgsParserError),
        ):
            unit.on_exec(unit.args_parser().parse_args([]))
        factory.assert_not_called()
        self.assertEqual(
            command.set_device_reader_mode.call_args_list, [call(True), call(False)]
        )

    def test_restore_failure_preserves_primary_error_in_message_and_cause(self):
        unit, _, command = make_unit(HFMFMagicInfo)
        original = MagicCardReadError("original read error")
        reader = mock_reader()
        reader.read_info.side_effect = original
        command.set_device_reader_mode.side_effect = [None, OSError("restore failure")]
        with (
            patch.object(cli_module, "MagicClassicReader", return_value=reader),
            self.assertRaises(ArgsParserError) as raised,
        ):
            unit.on_exec(unit.args_parser().parse_args([]))
        self.assertIn("original read error", str(raised.exception))
        self.assertIn("restore failure", str(raised.exception))
        self.assertIs(raised.exception.__cause__, original)

    def test_successful_read_with_failed_restore_does_not_publish(self):
        with tempfile.TemporaryDirectory() as directory:
            unit, _, command = make_unit(HFMFMagicDump)
            command.set_device_reader_mode.side_effect = [None, OSError("mode restore")]
            args = unit.args_parser().parse_args(
                ["-o", str(Path(directory) / "backup.bin")]
            )
            with (
                patch.object(
                    cli_module, "MagicClassicReader", return_value=mock_reader()
                ),
                self.assertRaises(ArgsParserError),
            ):
                unit.on_exec(args)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_bin_and_json_export_same_complete_image_after_mode_restore(self):
        with tempfile.TemporaryDirectory() as directory:
            for output_format in ("bin", "json"):
                with self.subTest(output_format=output_format):
                    target = Path(directory) / f"backup.{output_format}"
                    unit, _, command = make_unit(HFMFMagicDump)
                    args = unit.args_parser().parse_args(
                        ["-o", str(target), "--format", output_format]
                    )
                    output = io.StringIO()
                    publisher = cli_module.publish_backup

                    def publish_after_restore(path, content):
                        self.assertEqual(
                            command.set_device_reader_mode.call_args_list,
                            [call(True), call(False)],
                        )
                        publisher(path, content)

                    with (
                        patch.object(
                            cli_module, "MagicClassicReader", return_value=mock_reader()
                        ),
                        patch.object(
                            cli_module, "publish_backup", side_effect=publish_after_restore
                        ),
                        contextlib.redirect_stdout(output),
                    ):
                        self.assertTrue(unit.before_exec(args))
                        unit.on_exec(args)
                    if output_format == "bin":
                        # The complete comparison includes every raw Key A/B
                        # field: exports must not reconstruct or zero-fill it.
                        self.assertEqual(target.read_bytes(), IMAGE)
                    else:
                        document = json.loads(target.read_text(encoding="utf-8"))
                        image = bytes.fromhex("".join(document["blocks"]))
                        self.assertEqual(image, IMAGE)
                        self.assertEqual(document["uid"], "01020304")
                        self.assertEqual(document["atqa_wire_order"], "0400")
                        self.assertEqual(document["sak"], "08")
                        self.assertEqual(document["block_count"], 64)
                        self.assertEqual(document["independent_reads"], 2)
                        self.assertIs(document["key_material_verified"], False)
                        self.assertEqual(
                            document["key_handling"],
                            "raw bytes returned by card; no key reconstruction "
                            "or authentication",
                        )
                        self.assertEqual(
                            document["sha256"], hashlib.sha256(IMAGE).hexdigest()
                        )
                    self.assertIn(
                        "sector keys have not been independently authenticated",
                        output.getvalue(),
                    )
                    self.assertNotIn(IMAGE[-16:].hex().upper(), output.getvalue())

    def test_target_created_during_read_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "backup.bin"
            unit, _, command = make_unit(HFMFMagicDump)
            reader = mock_reader()

            def create_competing_backup():
                target.write_bytes(b"another complete backup")
                return dump_result()

            reader.dump.side_effect = create_competing_backup
            args = unit.args_parser().parse_args(["-o", str(target)])
            with (
                patch.object(cli_module, "MagicClassicReader", return_value=reader),
                self.assertRaises(ArgsParserError),
            ):
                self.assertTrue(unit.before_exec(args))
                unit.on_exec(args)
            self.assertEqual(target.read_bytes(), b"another complete backup")
            self.assertEqual(list(Path(directory).iterdir()), [target])
            self.assertEqual(
                command.set_device_reader_mode.call_args_list, [call(True), call(False)]
            )

    def test_parsers_expose_only_read_operations_and_supported_formats(self):
        info = HFMFMagicInfo().args_parser()
        dump = HFMFMagicDump().args_parser()
        info.parse_args([])
        self.assertEqual(dump.parse_args(["-o", "backup.bin"]).format, "bin")
        for arguments in (
            ["--write"],
            ["--uid", "01020304"],
            ["--config", "00"],
            ["--size", "4k"],
        ):
            with self.subTest(arguments=arguments), self.assertRaises(ArgsParserError):
                info.parse_args(arguments)
            with self.subTest(arguments=arguments), self.assertRaises(ArgsParserError):
                dump.parse_args(["-o", "backup.bin"] + arguments)
        with self.assertRaises(ArgsParserError):
            dump.parse_args(["-o", "backup.bin", "--format", "hex"])


class BackupPublicationTest(unittest.TestCase):
    def test_incomplete_image_or_unknown_format_is_not_encoded(self):
        incomplete = MagicCardDump(IDENTITY, IMAGE[:-1])
        for output_format in ("bin", "json"):
            with self.subTest(output_format=output_format):
                with self.assertRaises(MagicCardReadError):
                    backup_io.encode_backup(incomplete, output_format)
        with self.assertRaises(ValueError):
            backup_io.encode_backup(dump_result(), "hex")

    def test_native_publication_refuses_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "existing.bin"
            target.write_bytes(b"older backup")
            with self.assertRaises(FileExistsError):
                backup_io.publish_backup(target, IMAGE)
            self.assertEqual(target.read_bytes(), b"older backup")
            self.assertEqual(list(Path(directory).iterdir()), [target])

    def test_posix_link_publication_refuses_existing_destination(self):
        # Patch only this module's os reference. Patching global os.name would
        # make pathlib select an unsupported path flavour on Windows.
        posix = SimpleNamespace(name="posix", fsync=os.fsync, link=os.link, rename=os.rename)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "backup.bin"
            with patch.object(backup_io, "os", posix):
                backup_io.publish_backup(target, IMAGE)
                with self.assertRaises(FileExistsError):
                    backup_io.publish_backup(target, b"competing data")
            self.assertEqual(target.read_bytes(), IMAGE)
            self.assertEqual(list(Path(directory).iterdir()), [target])

    def test_partial_write_flush_sync_and_close_errors_leave_no_destination(self):
        real_factory = backup_io.tempfile.NamedTemporaryFile
        for failure in ("partial", "short", "flush", "sync", "close"):
            for output_format in ("bin", "json"):
                with self.subTest(failure=failure, output_format=output_format):
                    with tempfile.TemporaryDirectory() as directory:
                        target = Path(directory) / f"backup.{output_format}"
                        unit, _, command = make_unit(HFMFMagicDump)
                        args = unit.args_parser().parse_args(
                            ["-o", str(target), "--format", output_format]
                        )

                        def temporary_file(*args, **kwargs):
                            actual = real_factory(*args, **kwargs)
                            replacement = MagicMock()
                            replacement.name = actual.name
                            replacement.__enter__.return_value = replacement
                            replacement.fileno.side_effect = actual.fileno

                            def write(content):
                                if failure == "partial":
                                    actual.write(content[:7])
                                    raise OSError("storage failed after partial write")
                                if failure == "short":
                                    return actual.write(content[:7])
                                return actual.write(content)

                            def flush():
                                if failure == "flush":
                                    raise OSError("flush failed")
                                actual.flush()

                            def finish(*_):
                                actual.close()
                                if failure == "close":
                                    raise OSError("close failed")
                                return False

                            replacement.write.side_effect = write
                            replacement.flush.side_effect = flush
                            replacement.__exit__.side_effect = finish
                            return replacement

                        sync_effect = (
                            OSError("sync failed") if failure == "sync" else None
                        )
                        with (
                            patch.object(
                                cli_module,
                                "MagicClassicReader",
                                return_value=mock_reader(),
                            ),
                            patch.object(
                                backup_io.tempfile,
                                "NamedTemporaryFile",
                                side_effect=temporary_file,
                            ),
                            patch.object(backup_io.os, "fsync", side_effect=sync_effect),
                            self.assertRaises(ArgsParserError),
                        ):
                            unit.on_exec(args)
                        self.assertEqual(list(Path(directory).iterdir()), [])
                        self.assertEqual(
                            command.set_device_reader_mode.call_args_list,
                            [call(True), call(False)],
                        )


if __name__ == "__main__":
    TestProgram()
