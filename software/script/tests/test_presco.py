"""Hardware-free Presco codec, command payload and CLI regression tests."""

import contextlib
import io
import unittest
from unittest import TestProgram as UnittestProgram
from unittest.mock import Mock

from chameleon_cli_unit import LFPrescoEconfig, LFPrescoEncode, LFPrescoWriteT55xx
from chameleon_cmd import ChameleonCMD, new_key, old_keys
from chameleon_com import Response
from chameleon_enum import Command, Status, TagSpecificType
from chameleon_utils import ArgsParserError, UnexpectedResponseError
from presco import encode_frame, parse_hex_code, parse_printed_code


class PrescoCodecTests(unittest.TestCase):
    def test_known_printed_code(self):
        # Proxmark's documented example, evaluated independently in base 12.
        self.assertEqual(parse_printed_code("018363467"), bytes.fromhex("039CA0CF"))
        self.assertEqual(parse_printed_code("*#"), bytes.fromhex("00000083"))
        self.assertEqual(parse_printed_code("0"), bytes(4))
        self.assertEqual(parse_printed_code("9#*461593"), bytes.fromhex("FFFFFFFF"))

    def test_hex_and_frame_preserve_all_bits(self):
        code = parse_hex_code("1234abcd")
        self.assertEqual(code, bytes.fromhex("1234ABCD"))
        self.assertEqual(encode_frame(code).hex(), "10d0000000000000000000001234abcd")

    def test_invalid_codes(self):
        for value in ("", "0x12345678", "1234567", "123456789", "GG123456", "1234567\n"):
            with self.subTest(hex=value), self.assertRaises(ValueError):
                parse_hex_code(value)
        for value in ("", "1234567890", "#########", "-1", "12A", " 12", "12\n"):
            with self.subTest(printed=value), self.assertRaises(ValueError):
                parse_printed_code(value)
        for size in (0, 3, 5, 16):
            with self.subTest(size=size), self.assertRaises(ValueError):
                encode_frame(bytes(size))


class PrescoTransportTests(unittest.TestCase):
    def setUp(self):
        self.device = Mock()
        self.cmd = ChameleonCMD(self.device)
        self.code = bytes.fromhex("12345678")

    def test_write_payload(self):
        self.device.send_cmd_sync.return_value = Response(Command.PRESCO_WRITE_TO_T55XX, Status.LF_TAG_OK)
        self.cmd.presco_write_to_t55xx(self.code)
        self.device.send_cmd_sync.assert_called_once_with(
            Command.PRESCO_WRITE_TO_T55XX, self.code + new_key + b"".join(old_keys)
        )

    def test_set_payload(self):
        self.device.send_cmd_sync.return_value = Response(Command.PRESCO_SET_EMU_ID, Status.SUCCESS)
        self.cmd.presco_set_emu_id(self.code)
        self.device.send_cmd_sync.assert_called_once_with(Command.PRESCO_SET_EMU_ID, self.code)

    def test_get_payload(self):
        self.device.send_cmd_sync.return_value = Response(Command.PRESCO_GET_EMU_ID, Status.SUCCESS, self.code)
        self.assertEqual(self.cmd.presco_get_emu_id(), self.code)
        self.device.send_cmd_sync.assert_called_once_with(Command.PRESCO_GET_EMU_ID)

    def test_invalid_lengths_never_send(self):
        for method in (self.cmd.presco_set_emu_id, self.cmd.presco_write_to_t55xx):
            for size in (0, 3, 5, 16):
                with self.subTest(method=method.__name__, size=size), self.assertRaises(ValueError):
                    method(bytes(size))
        self.device.send_cmd_sync.assert_not_called()

    def test_malformed_get_response(self):
        for size in (0, 3, 5):
            self.device.send_cmd_sync.return_value = Response(Command.PRESCO_GET_EMU_ID, Status.SUCCESS, bytes(size))
            with self.subTest(size=size), self.assertRaises(ValueError):
                self.cmd.presco_get_emu_id()

    def test_firmware_errors_are_reported(self):
        self.device.send_cmd_sync.return_value = Response(Command.PRESCO_GET_EMU_ID, Status.PAR_ERR)
        with self.assertRaises(UnexpectedResponseError):
            self.cmd.presco_get_emu_id()


class PrescoCLITests(unittest.TestCase):
    def test_argument_groups(self):
        for cls in (LFPrescoEncode, LFPrescoWriteT55xx, LFPrescoEconfig):
            parser = cls().args_parser()
            for flags in (["--id", "12345678"], ["--printed", "018363467"]):
                with self.subTest(command=cls.__name__, flags=flags):
                    self.assertEqual(len(parser.parse_args(flags).code), 4)
            with self.assertRaises(ArgsParserError):
                parser.parse_args(["--id", "12345678", "--printed", "1"])
            with self.assertRaises(ArgsParserError):
                parser.parse_args(["--printed", "#########"])
        self.assertIsNone(LFPrescoEconfig().args_parser().parse_args([]).code)
        for cls in (LFPrescoEncode, LFPrescoWriteT55xx):
            with self.assertRaises(ArgsParserError):
                cls().args_parser().parse_args([])

    def test_offline_encode(self):
        unit = LFPrescoEncode()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            unit.on_exec(unit.args_parser().parse_args(["--printed", "018363467"]))
        self.assertIn("10D000000000000000000000039CA0CF", output.getvalue())

    def make_econfig(self, tag_type):
        unit = LFPrescoEconfig()
        unit._device_cmd = Mock()
        unit.cmd.get_active_slot.return_value = 2
        unit.cmd.get_slot_info.return_value = [{"lf": TagSpecificType.EM410X}] * 8
        unit.cmd.get_slot_info.return_value[2] = {"lf": tag_type}
        return unit

    def test_econfig_refuses_wrong_slot(self):
        unit = self.make_econfig(TagSpecificType.EM410X)
        args = unit.args_parser().parse_args(["--id", "12345678"])
        with self.assertRaises(ArgsParserError):
            unit.on_exec(args)
        unit.cmd.presco_set_emu_id.assert_not_called()
        unit.cmd.presco_get_emu_id.assert_not_called()

    def test_econfig_get_and_set(self):
        unit = self.make_econfig(TagSpecificType.Presco)
        unit.cmd.presco_get_emu_id.return_value = bytes.fromhex("12345678")
        with contextlib.redirect_stdout(io.StringIO()):
            unit.on_exec(unit.args_parser().parse_args([]))
            unit.on_exec(unit.args_parser().parse_args(["--id", "12345678"]))
        unit.cmd.presco_get_emu_id.assert_called_once_with()
        unit.cmd.presco_set_emu_id.assert_called_once_with(bytes.fromhex("12345678"))


if __name__ == "__main__":
    UnittestProgram()
