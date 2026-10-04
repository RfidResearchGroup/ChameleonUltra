#!/usr/bin/env python3
"""Boundary tests for Paradox T5577 writing command and CLI paths."""

import contextlib
import io
import re
import sys
import unittest
from pathlib import Path
from typing import Optional
from unittest import main


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from chameleon_cmd import ChameleonCMD  # noqa: E402
from chameleon_cli_unit import LFParadoxWriteT55xx, LFT55xxClone  # noqa: E402
from chameleon_com import ChameleonCom, Response  # noqa: E402
from chameleon_enum import Command, Status  # noqa: E402
from chameleon_utils import ArgsParserError  # noqa: E402


TEST_ID = bytes.fromhex("123456789AB0")
EXPECTED_WRITE_PAYLOAD = TEST_ID + bytes.fromhex("20206666 51243648 19920427")


class MockDevice(ChameleonCom):
    """Minimal device boundary used to test command serialization."""

    def __init__(self):
        super().__init__()
        self.calls: list[tuple[int, Optional[bytes]]] = []

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
        if cmd == Command.PARADOX_SCAN:
            return Response(cmd, Status.LF_TAG_OK, TEST_ID)
        return Response(cmd, Status.LF_TAG_OK)


class TestParadoxT5577Command(unittest.TestCase):
    def test_command_id_is_next_unused_and_matches_header(self):
        header = (
            Path(__file__).resolve().parents[3]
            / "firmware"
            / "application"
            / "src"
            / "data_cmd.h"
        ).read_text()
        definitions = re.findall(r"#define\s+(DATA_CMD_\S+)\s+\(?([0-9]+)\)?", header)
        matching_names = [name for name, value in definitions if value == "3022"]

        self.assertEqual(Command.PARADOX_WRITE_TO_T55XX, 3022)
        self.assertEqual(matching_names, ["DATA_CMD_PARADOX_WRITE_TO_T55XX"])

    def test_api_serializes_id_and_project_password_keys(self):
        device = MockDevice()
        command = ChameleonCMD(device)

        command.paradox_write_to_t55xx(TEST_ID)

        self.assertEqual(
            device.calls,
            [
                (Command.PARADOX_WRITE_TO_T55XX, EXPECTED_WRITE_PAYLOAD),
            ],
        )

    def test_api_rejects_wrong_id_length_before_device_call(self):
        device = MockDevice()
        command = ChameleonCMD(device)

        with self.assertRaises(ValueError):
            command.paradox_write_to_t55xx(b"\x00" * 5)

        self.assertEqual(device.calls, [])

    def test_direct_cli_parses_and_sends_paradox_id(self):
        device = MockDevice()
        unit = LFParadoxWriteT55xx()
        unit.device_com = device
        args = unit.args_parser().parse_args(["--id", TEST_ID.hex()])

        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertTrue(unit.before_exec(args))
            unit.on_exec(args)

        write_calls = [
            call for call in device.calls if call[0] == Command.PARADOX_WRITE_TO_T55XX
        ]
        self.assertEqual(
            write_calls, [(Command.PARADOX_WRITE_TO_T55XX, EXPECTED_WRITE_PAYLOAD)]
        )
        self.assertIn("verified", output.getvalue().lower())

    def test_generic_clone_accepts_paradox_id(self):
        device = MockDevice()
        unit = LFT55xxClone()
        unit.device_com = device
        args = unit.args_parser().parse_args(
            [
                "--type",
                "paradox",
                "--id",
                TEST_ID.hex(),
            ]
        )

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(unit.before_exec(args))
            unit.on_exec(args)

        write_calls = [
            call for call in device.calls if call[0] == Command.PARADOX_WRITE_TO_T55XX
        ]
        self.assertEqual(
            write_calls, [(Command.PARADOX_WRITE_TO_T55XX, EXPECTED_WRITE_PAYLOAD)]
        )

    def test_cli_rejects_non_hex_or_wrong_length_id(self):
        unit = LFParadoxWriteT55xx()
        unit.device_com = MockDevice()
        parser = unit.args_parser()
        args = parser.parse_args(["--id", "1234"])

        with self.assertRaises(ArgsParserError):
            unit.before_exec(args)


if __name__ == "__main__":
    main()
