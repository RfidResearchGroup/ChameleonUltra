"""Exercise capture requests, independent read agreement and complete backups."""

import argparse
import contextlib
import hashlib
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path
from typing import Any, Union
from unittest.main import TestProgram
from unittest.mock import Mock, patch

import chameleon_cli_unit as cli_module
from chameleon_cli_unit import LFT55xxDump, LFT55xxRead
from chameleon_cmd import ChameleonCMD, T55xxDumpResult
from chameleon_com import ChameleonCom, Response
from chameleon_enum import Command, Status
from chameleon_utils import ArgsParserError, UnexpectedResponseError
from t55xx_read import T55xxReadError, config_from_word
from tests.test_t55xx_read import pack_reference, reference_capture

CONFIG = 0x00148040
DATA = (CONFIG, 0, 0xFFFFFFFF, 0x12345678, 0x89ABCDEF, 0xDEADBEEF, 7, 0xA55AA55A)


def capture(word, *, start=80):
    # This independent encoder is shared with signal tests, not the decoder.
    return pack_reference(reference_capture(word, start=start, repeats=3))


class CaptureDevice(ChameleonCom):
    def __init__(self, replies=()):
        super().__init__()
        self.commands = [Command.LF_T55XX_READ_CAPTURE]
        self.replies = list(replies)
        self.requests = []

    def isOpen(self):
        return True

    def send_cmd_sync(self, cmd, data=None, status=0, timeout=3):
        self.requests.append((cmd, data, timeout))
        expected_block, reply = self.replies.pop(0)
        assert cmd == Command.LF_T55XX_READ_CAPTURE
        assert data == bytes([expected_block])
        assert status == 0
        assert timeout == 3
        if isinstance(reply, Exception):
            raise reply
        if isinstance(reply, Status):
            return Response(cmd, reply)
        return Response(cmd, Status.SUCCESS, reply)


def repetitions(block, word, *, start=80):
    packet = capture(word, start=start)
    return [(block, packet)] * 3


def dump_replies(after=CONFIG):
    result = repetitions(0, CONFIG)
    for block in range(1, 8):
        result.extend(repetitions(block, DATA[block]))
    return result + repetitions(0, after)


def cli_unit(cls, cmd):
    unit = cls()
    unit.device_com = CaptureDevice()
    unit._device_cmd = cmd
    return unit


class T55xxCommandsTest(unittest.TestCase):
    def test_capture_request_is_one_read_only_block_byte(self):
        device = CaptureDevice([(7, capture(0x12345678))])
        samples = ChameleonCMD(device)._t55xx_capture_block(7)
        self.assertGreater(len(samples), 6144)
        self.assertEqual(device.requests, [(Command.LF_T55XX_READ_CAPTURE, b"\x07", 3)])

    def test_bad_block_is_rejected_before_any_capture(self):
        device = CaptureDevice()
        invalid_values: tuple[Any, ...] = (-1, 8, True, 1.5, "0")
        for value in invalid_values:
            with self.subTest(value=value), self.assertRaises(ValueError):
                ChameleonCMD(device).t55xx_read_block(value)
        self.assertEqual(device.requests, [])

    def test_capture_errors_do_not_produce_a_word(self):
        accepted_errors: tuple[type[Exception], ...] = (
            UnexpectedResponseError,
            T55xxReadError,
        )
        for reply in (Status.CMD_ERR, b"\x01", TimeoutError("lost transport")):
            device = CaptureDevice([(0, reply)])
            with (
                self.subTest(reply=type(reply)),
                self.assertRaises(accepted_errors),
            ):
                ChameleonCMD(device).t55xx_read_config()
            self.assertEqual(len(device.requests), 1)

    def test_config_needs_three_independent_matching_captures(self):
        device = CaptureDevice(repetitions(0, CONFIG))
        result = ChameleonCMD(device).t55xx_read_config()
        self.assertEqual(result.word, CONFIG)
        self.assertEqual(len(device.requests), 3)

    def test_changed_config_and_unstable_anchor_are_rejected(self):
        cases = (
            [(0, capture(CONFIG)), (0, capture(0x00148060)), (0, capture(CONFIG))],
            [(0, capture(CONFIG, start=start)) for start in (64, 80, 100)],
        )
        for replies in cases:
            with self.subTest(), self.assertRaises(T55xxReadError):
                ChameleonCMD(CaptureDevice(replies)).t55xx_read_config()

    def test_block_zero_returns_the_calibrated_configuration(self):
        device = CaptureDevice(repetitions(0, CONFIG))
        result = ChameleonCMD(device).t55xx_read_block(0)
        self.assertEqual((result.block, result.word), (0, CONFIG))
        self.assertEqual(len(device.requests), 3)

    def test_block_reads_are_independent_of_an_expected_data_value(self):
        device = CaptureDevice(repetitions(0, CONFIG) + repetitions(4, DATA[4]))
        result = ChameleonCMD(device).t55xx_read_block(4)
        self.assertEqual((result.block, result.word), (4, DATA[4]))
        self.assertEqual(result.independent_reads, 3)
        self.assertEqual(
            [request[1][0] for request in device.requests], [0] * 3 + [4] * 3
        )

    def test_disagreement_between_complete_reads_is_not_accepted(self):
        config = config_from_word(CONFIG, start_sample=80, inverted=False)
        replies = [(2, capture(word)) for word in (0x12345678, 0x12345679, 0x12345678)]
        with self.assertRaisesRegex(T55xxReadError, "differs"):
            ChameleonCMD(CaptureDevice(replies)).t55xx_read_block(2, config)

    def test_page0_dump_rechecks_configuration_and_sends_no_writes(self):
        device = CaptureDevice(dump_replies())
        result = ChameleonCMD(device).t55xx_dump_page0()
        self.assertEqual(result.words, DATA)
        self.assertEqual(len(device.requests), 27)
        self.assertTrue(
            all(
                command == Command.LF_T55XX_READ_CAPTURE
                for command, _, _ in device.requests
            )
        )
        self.assertEqual(
            [payload for _, payload, _ in device.requests[-3:]], [b"\x00"] * 3
        )

    def test_changed_final_config_is_a_failed_dump(self):
        with self.assertRaisesRegex(T55xxReadError, "changed during"):
            ChameleonCMD(
                CaptureDevice(dump_replies(after=0x00148060))
            ).t55xx_dump_page0()


class T55xxCliTest(unittest.TestCase):
    def test_unsupported_firmware_is_rejected_before_a_mode_change(self):
        command = Mock()
        unit = cli_unit(LFT55xxRead, command)
        unit.device_com.commands = []
        with self.assertRaisesRegex(ArgsParserError, "firmware does not support"):
            unit.before_exec(argparse.Namespace(block=0))
        command.set_device_reader_mode.assert_not_called()

    def test_parser_has_only_page0_read_and_dump_options(self):
        parser = LFT55xxRead().args_parser()
        self.assertEqual(parser.parse_args(["-b", "7"]).block, 7)
        for args in (["-b", "8"], ["-b", "0", "--pwd", "00000000"]):
            with self.subTest(args=args), self.assertRaises(ArgsParserError):
                parser.parse_args(args)
        dump_args = LFT55xxDump().args_parser().parse_args(["-o", "backup.json"])
        self.assertEqual(dump_args.format, "json")

    def test_existing_backup_prevents_a_read_and_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "backup.bin"
            target.write_bytes(b"existing")
            command = Mock()
            with self.assertRaisesRegex(ArgsParserError, "overwrite"):
                cli_unit(LFT55xxDump, command).on_exec(
                    argparse.Namespace(out=target, format="bin")
                )
            command.t55xx_dump_page0.assert_not_called()
            self.assertEqual(target.read_bytes(), b"existing")

    def test_failed_or_incomplete_dump_creates_no_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "backup.json"
            config = config_from_word(CONFIG, start_sample=80, inverted=False)
            cases: tuple[Union[T55xxReadError, T55xxDumpResult], ...] = (
                T55xxReadError("changed config"),
                T55xxDumpResult((CONFIG,), config),
            )
            for value in cases:
                command = Mock()
                if isinstance(value, T55xxReadError):
                    command.t55xx_dump_page0.side_effect = value
                else:
                    command.t55xx_dump_page0.return_value = value
                with self.subTest(), self.assertRaises(ArgsParserError):
                    cli_unit(LFT55xxDump, command).on_exec(
                        argparse.Namespace(out=target, format="json")
                    )
                self.assertFalse(target.exists())

    def test_binary_and_json_exports_describe_the_same_page_image(self):
        config = config_from_word(CONFIG, start_sample=80, inverted=False)
        command = Mock()
        command.t55xx_dump_page0.return_value = T55xxDumpResult(DATA, config)
        image = struct.pack("!8I", *DATA)
        with (
            tempfile.TemporaryDirectory() as directory,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            for fmt in ("bin", "json"):
                target = Path(directory) / f"backup.{fmt}"
                cli_unit(LFT55xxDump, command).on_exec(
                    argparse.Namespace(out=target, format=fmt)
                )
                if fmt == "bin":
                    self.assertEqual(target.read_bytes(), image)
                else:
                    data = json.loads(target.read_text())
                    self.assertEqual(data["words"], [f"{word:08X}" for word in DATA])
                    self.assertEqual(data["sha256"], hashlib.sha256(image).hexdigest())
                    self.assertEqual(data["acquisition"]["configuration_checks"], 2)

    def test_partial_or_short_write_leaves_no_backup_or_temporary_file(self):
        original = tempfile.NamedTemporaryFile
        config = config_from_word(CONFIG, start_sample=80, inverted=False)
        command = Mock()
        command.t55xx_dump_page0.return_value = T55xxDumpResult(DATA, config)

        for format_name in ("bin", "json"):
            for failure in ("partial", "short"):
                with (
                    self.subTest(format=format_name, failure=failure),
                    tempfile.TemporaryDirectory() as directory,
                ):
                    target = Path(directory) / f"backup.{format_name}"

                    class BrokenWrite:
                        def __init__(self, *args, _failure=failure, **kwargs):
                            self.file = original(*args, **kwargs)
                            self.name = self.file.name
                            self.failure = _failure

                        def __enter__(self):
                            return self

                        def __exit__(self, *_args):
                            self.file.close()

                        def write(self, content):
                            written = self.file.write(content[:3])
                            self.file.flush()
                            if self.failure == "partial":
                                raise OSError("simulated disk full")
                            return written

                    with (
                        patch.object(
                            cli_module.tempfile, "NamedTemporaryFile", BrokenWrite
                        ),
                        self.assertRaises(ArgsParserError),
                    ):
                        cli_unit(LFT55xxDump, command).on_exec(
                            argparse.Namespace(out=target, format=format_name)
                        )
                    self.assertFalse(target.exists())
                    self.assertEqual(list(Path(directory).iterdir()), [])

    def test_flush_sync_and_close_failure_leave_no_backup(self):
        original = tempfile.NamedTemporaryFile
        config = config_from_word(CONFIG, start_sample=80, inverted=False)
        command = Mock()
        command.t55xx_dump_page0.return_value = T55xxDumpResult(DATA, config)

        for failure in ("flush", "fsync", "close"):
            with (
                self.subTest(failure=failure),
                tempfile.TemporaryDirectory() as directory,
            ):
                target = Path(directory) / "backup.json"

                class BrokenFinalization:
                    def __init__(self, *args, _failure=failure, **kwargs):
                        self.file = original(*args, **kwargs)
                        self.name = self.file.name
                        self.failure = _failure

                    def __enter__(self):
                        return self

                    def __exit__(self, *_args):
                        # Real buffered close also closes its descriptor when
                        # flushing fails. Release it before simulating failure.
                        self.file.close()
                        if self.failure == "close":
                            raise OSError("simulated close failure")

                    def write(self, content):
                        return self.file.write(content)

                    def flush(self):
                        if self.failure == "flush":
                            raise OSError("simulated flush failure")
                        self.file.flush()

                    def fileno(self):
                        return self.file.fileno()

                def sync(_descriptor, stage=failure):
                    if stage == "fsync":
                        raise OSError("simulated sync failure")

                with (
                    patch.object(
                        cli_module.tempfile, "NamedTemporaryFile", BrokenFinalization
                    ),
                    patch.object(cli_module.os, "fsync", sync),
                    self.assertRaises(ArgsParserError),
                ):
                    cli_unit(LFT55xxDump, command).on_exec(
                        argparse.Namespace(out=target, format="json")
                    )
                self.assertFalse(target.exists())
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_backup_appearing_during_acquisition_is_not_overwritten(self):
        config = config_from_word(CONFIG, start_sample=80, inverted=False)
        for format_name in ("bin", "json"):
            with (
                self.subTest(format=format_name),
                tempfile.TemporaryDirectory() as directory,
            ):
                target = Path(directory) / f"backup.{format_name}"
                command = Mock()

                def finish_read(path=target):
                    path.write_bytes(b"another backup")
                    return T55xxDumpResult(DATA, config)

                command.t55xx_dump_page0.side_effect = finish_read
                with self.assertRaises(ArgsParserError):
                    cli_unit(LFT55xxDump, command).on_exec(
                        argparse.Namespace(out=target, format=format_name)
                    )
                self.assertEqual(target.read_bytes(), b"another backup")
                self.assertEqual(list(Path(directory).iterdir()), [target])


if __name__ == "__main__":
    TestProgram()
