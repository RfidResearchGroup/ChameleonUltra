#!/usr/bin/env python3
"""Boundary tests for Paradox CRC reporting and T5577 write verification."""

import contextlib
import io
import sys
import unittest
from pathlib import Path
from typing import Optional
from unittest import main, mock


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from chameleon_cli_unit import (  # noqa: E402
    LFParadoxEconfig,
    LFParadoxRead,
    LFParadoxWriteT55xx,
    LFT55xxClone,
    paradox_crc_info,
    paradox_crc_status,
    paradox_wire_payload,
)
from chameleon_com import ChameleonCom, Response  # noqa: E402
from chameleon_enum import Command, Status  # noqa: E402
from chameleon_utils import UnexpectedResponseError  # noqa: E402


VALID_DATA = bytes.fromhex("00048D1587B0")
SECOND_VALID_DATA = bytes.fromhex("0029568F0AF0")
BAD_CRC_DATA = bytes.fromhex("00048D158030")
NONCANONICAL_DATA = bytes.fromhex("80048D1587B0")
OTHER_DATA = SECOND_VALID_DATA


class MockDevice(ChameleonCom):
    """Device boundary with configurable write and read-back responses."""

    def __init__(
        self,
        write_status: int = Status.LF_TAG_OK,
        scan_status: int = Status.LF_TAG_OK,
        scan_data: bytes = VALID_DATA,
        events: Optional[list[str]] = None,
    ):
        super().__init__()
        self.calls: list[tuple[int, Optional[bytes]]] = []
        self.write_status = write_status
        self.scan_status = scan_status
        self.scan_data = scan_data
        self.events = events if events is not None else []

    def isOpen(self) -> bool:
        return True

    def send_cmd_sync(
        self,
        cmd: int,
        data: Optional[bytes] = None,
        status: int = 0,
        timeout: int = 3,
    ) -> Response:
        self.calls.append((cmd, data))
        if cmd == Command.GET_DEVICE_MODE:
            return Response(cmd, Status.SUCCESS, b"\x01")
        if cmd == Command.GET_DEVICE_MODEL:
            return Response(cmd, Status.SUCCESS, b"\x00")
        if cmd == Command.GET_ACTIVE_SLOT:
            return Response(cmd, Status.SUCCESS, b"\x01")
        if cmd == Command.PARADOX_GET_EMU_ID:
            return Response(cmd, Status.SUCCESS, VALID_DATA)
        if cmd == Command.PARADOX_WRITE_TO_T55XX:
            self.events.append("write")
            return Response(cmd, self.write_status)
        if cmd == Command.PARADOX_SCAN:
            self.events.append("scan")
            return Response(cmd, self.scan_status, self.scan_data)
        return Response(cmd, Status.SUCCESS)


def command_calls(
    device: MockDevice, command: Command
) -> list[tuple[int, Optional[bytes]]]:
    return [call for call in device.calls if call[0] == command]


def run_write(unit_class, argv, device: MockDevice) -> str:
    unit = unit_class()
    unit.device_com = device
    args = unit.args_parser().parse_args(argv)
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        if not unit.before_exec(args):
            raise AssertionError("device setup unexpectedly failed")
        unit.on_exec(args)
    return output.getvalue()


class TestParadoxValidation(unittest.TestCase):
    def test_known_crc_vector_and_raw_preservation(self):
        raw = bytearray(VALID_DATA)
        before = bytes(raw)

        stored, expected, matches, canonical = paradox_crc_info(raw)

        self.assertEqual(stored, 0x1E)
        self.assertEqual(expected, 0x1E)
        self.assertTrue(matches)
        self.assertTrue(canonical)
        self.assertEqual(bytes(raw), before)
        self.assertIn("valid", paradox_crc_status(raw))

        second_stored, second_expected, second_matches, second_canonical = (
            paradox_crc_info(SECOND_VALID_DATA)
        )
        self.assertEqual(second_stored, 0x2B)
        self.assertEqual(second_expected, 0x2B)
        self.assertTrue(second_matches)
        self.assertTrue(second_canonical)

    def test_bad_crc_is_reported_without_rewriting_payload(self):
        stored, expected, matches, canonical = paradox_crc_info(BAD_CRC_DATA)

        self.assertEqual(stored, 0)
        self.assertEqual(expected, 0x1E)
        self.assertFalse(matches)
        self.assertTrue(canonical)
        self.assertIn("mismatch", paradox_crc_status(BAD_CRC_DATA))

    def test_noncanonical_leading_bits_are_not_called_valid(self):
        stored, expected, matches, canonical = paradox_crc_info(NONCANONICAL_DATA)

        self.assertEqual(stored, expected)
        self.assertTrue(matches)
        self.assertFalse(canonical)
        status = paradox_crc_status(NONCANONICAL_DATA)
        self.assertIn("noncanonical", status)
        self.assertNotIn("valid", status)

    def test_read_displays_crc_verdict_and_expected_value(self):
        device = MockDevice(scan_data=VALID_DATA)
        unit = LFParadoxRead()
        unit.device_com = device
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertTrue(unit.before_exec(unit.args_parser().parse_args([])))
            unit.on_exec(unit.args_parser().parse_args([]))

        text = output.getvalue()
        self.assertIn("CRC:", text)
        self.assertIn("valid", text)
        self.assertIn("expected 30", text)

    def test_econfig_displays_crc_verdict_and_expected_value(self):
        device = MockDevice()
        unit = LFParadoxEconfig()
        unit.device_com = device
        args = unit.args_parser().parse_args([])
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            self.assertTrue(unit.before_exec(args))
            unit.on_exec(args)

        text = output.getvalue()
        self.assertIn("CRC:", text)
        self.assertIn("valid", text)
        self.assertIn("expected 30", text)

    def test_direct_write_warns_before_one_write_and_one_read(self):
        events: list[str] = []
        device = MockDevice(events=events)
        warning_output = []

        def record_print(*args, **kwargs):
            text = " ".join(str(arg) for arg in args)
            if "password mode" in text:
                events.append("warning")
            warning_output.append(text)

        with mock.patch("builtins.print", side_effect=record_print):
            run_write(LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()], device)

        self.assertEqual(events, ["warning", "write", "scan"])
        self.assertEqual(len(command_calls(device, Command.PARADOX_WRITE_TO_T55XX)), 1)
        self.assertEqual(len(command_calls(device, Command.PARADOX_SCAN)), 1)
        self.assertTrue(any("20206666" in line for line in warning_output))

    def test_padding_difference_still_verifies_the_same_44_bits(self):
        input_data = VALID_DATA[:-1] + bytes([VALID_DATA[-1] | 0x0F])
        device = MockDevice(scan_data=VALID_DATA)

        output = run_write(LFParadoxWriteT55xx, ["--id", input_data.hex()], device)

        self.assertIn("verified", output)
        write_payload = command_calls(device, Command.PARADOX_WRITE_TO_T55XX)[0][1]
        self.assertIsNotNone(write_payload)
        assert write_payload is not None
        self.assertEqual(write_payload[:6], input_data)
        self.assertEqual(
            paradox_wire_payload(input_data), paradox_wire_payload(VALID_DATA)
        )

    def test_missing_read_back_is_distinct_and_does_not_retry_write(self):
        device = MockDevice(scan_status=Status.LF_TAG_NO_FOUND)

        output = run_write(LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()], device)

        self.assertIn("read-back failed: tag not found", output)
        self.assertEqual(len(command_calls(device, Command.PARADOX_WRITE_TO_T55XX)), 1)
        self.assertEqual(len(command_calls(device, Command.PARADOX_SCAN)), 1)

    def test_mismatched_read_back_is_distinct(self):
        device = MockDevice(scan_data=OTHER_DATA)

        output = run_write(LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()], device)

        self.assertIn("read-back mismatch", output)
        self.assertIn(VALID_DATA.hex().upper(), output)
        self.assertIn(OTHER_DATA.hex().upper(), output)

    def test_write_failure_does_not_scan(self):
        device = MockDevice(write_status=Status.PAR_ERR)

        with self.assertRaises(UnexpectedResponseError):
            run_write(LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()], device)

        self.assertEqual(len(command_calls(device, Command.PARADOX_WRITE_TO_T55XX)), 1)
        self.assertEqual(len(command_calls(device, Command.PARADOX_SCAN)), 0)

    def test_unexpected_read_back_status_is_not_reported_as_missing(self):
        device = MockDevice(scan_status=Status.PAR_ERR)

        with self.assertRaises(UnexpectedResponseError):
            run_write(LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()], device)

        self.assertEqual(len(command_calls(device, Command.PARADOX_WRITE_TO_T55XX)), 1)
        self.assertEqual(len(command_calls(device, Command.PARADOX_SCAN)), 1)

    def test_generic_and_direct_paths_share_verification_behavior(self):
        for unit_class, argv in (
            (LFParadoxWriteT55xx, ["--id", VALID_DATA.hex()]),
            (LFT55xxClone, ["--type", "paradox", "--id", VALID_DATA.hex()]),
        ):
            with self.subTest(unit=unit_class.__name__):
                events: list[str] = []
                device = MockDevice(events=events)

                def record_print(*args, **kwargs):
                    text = " ".join(str(arg) for arg in args)
                    if "password mode" in text:
                        events.append("warning")

                with mock.patch("builtins.print", side_effect=record_print):
                    run_write(unit_class, argv, device)

                self.assertEqual(events, ["warning", "write", "scan"])
                self.assertEqual(
                    len(command_calls(device, Command.PARADOX_WRITE_TO_T55XX)), 1
                )
                self.assertEqual(len(command_calls(device, Command.PARADOX_SCAN)), 1)


if __name__ == "__main__":
    main()
