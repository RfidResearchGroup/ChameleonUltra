import binascii
import glob
import math
import os
import secrets
import tempfile
import re
import subprocess
import argparse
import timeit
import sys
import time
import serial.tools.list_ports
import threading
import struct
from multiprocessing import Pool, cpu_count
from typing import Union
from pathlib import Path
from platform import uname
from datetime import datetime
import hardnested_utils

import chameleon_com
import chameleon_cmd
from chameleon_utils import (
    ArgumentParserNoExit,
    ArgsParserError,
    UnexpectedResponseError,
    execute_tool,
    tqdm_if_exists,
    print_key_table,
)
from chameleon_utils import CLITree
from chameleon_utils import CR, CG, CB, CC, CY, C0, color_string
from chameleon_utils import print_mem_dump
from chameleon_enum import Command, Status, SlotNumber, TagSenseType, TagSpecificType
from chameleon_enum import (
    MifareClassicWriteMode,
    MifareClassicPrngType,
    MifareClassicDarksideStatus,
    MfcKeyType,
)
from chameleon_enum import MifareUltralightWriteMode
from chameleon_enum import (
    AnimationMode,
    ButtonPressFunction,
    ButtonType,
    MfcValueBlockOperator,
)
from chameleon_enum import HIDFormat
from crypto1 import Crypto1

# NXP IDs based on https://www.nxp.com/docs/en/application-note/AN10833.pdf
type_id_SAK_dict = {
    0x00: "MIFARE Ultralight Classic/C/EV1/Nano | NTAG 2xx",
    0x08: "MIFARE Classic 1K | Plus SE 1K | Plug S 2K | Plus X 2K",
    0x09: "MIFARE Mini 0.3k",
    0x10: "MIFARE Plus 2K",
    0x11: "MIFARE Plus 4K",
    0x18: "MIFARE Classic 4K | Plus S 4K | Plus X 4K",
    0x19: "MIFARE Classic 2K",
    0x20: "MIFARE Plus EV1/EV2 | DESFire EV1/EV2/EV3 | DESFire Light | NTAG 4xx | "
    "MIFARE Plus S 2/4K | MIFARE Plus X 2/4K | MIFARE Plus SE 1K",
    0x28: "SmartMX with MIFARE Classic 1K",
    0x38: "SmartMX with MIFARE Classic 4K",
}

default_cwd = Path.cwd() / Path(__file__).with_name("bin")


def load_key_file(import_key, keys):
    """
    Load key file and append its content to the provided set of keys.
    Each key is expected to be on a new line in the file.
    """
    with open(import_key.name, "rb") as file:
        keys.update(
            line.encode("utf-8") for line in file.read().decode("utf-8").splitlines()
        )
    return keys


def load_dic_file(import_dic, keys):
    return keys


def check_tools():
    script_bin_dir = Path(__file__).resolve().with_name("bin")
    cwd_bin_dir = Path.cwd() / "bin"
    missing_tools = []

    for tool in (
        "staticnested",
        "nested",
        "darkside",
        "mfkey32v2",
        "staticnested_1nt",
        "staticnested_2x1nt_rf08s",
        "staticnested_2x1nt_rf08s_1key",
    ):
        if any(script_bin_dir.glob(f"{tool}*")) or any(cwd_bin_dir.glob(f"{tool}*")):
            continue
        else:
            missing_tools.append(tool)

    if missing_tools:
        missing_tool_str = ", ".join(missing_tools)
        warn_str = f"Warning, {missing_tool_str} not found. Corresponding commands will not work as intended."
        print(color_string((CR, warn_str)))


class BaseCLIUnit:
    def __init__(self):
        # new a device command transfer and receiver instance(Send cmd and receive response)
        self._device_com: Union[chameleon_com.ChameleonCom, None] = None
        self._device_cmd: Union[chameleon_cmd.ChameleonCMD, None] = None

    @property
    def device_com(self) -> chameleon_com.ChameleonCom:
        assert self._device_com is not None
        return self._device_com

    @device_com.setter
    def device_com(self, com):
        self._device_com = com
        self._device_cmd = chameleon_cmd.ChameleonCMD(self._device_com)

    @property
    def cmd(self) -> chameleon_cmd.ChameleonCMD:
        assert self._device_cmd is not None
        return self._device_cmd

    def args_parser(self) -> ArgumentParserNoExit:
        """
            CMD unit args.

        :return:
        """
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        """
            Call a function before exec cmd.

        :return: function references
        """
        return True

    def on_exec(self, args: argparse.Namespace):
        """
            Call a function on cmd match.

        :return: function references
        """
        raise NotImplementedError("Please implement this")

    def after_exec(self, args: argparse.Namespace):
        """
            Call a function after exec cmd.

        :return: function references
        """
        return True

    @staticmethod
    def sub_process(cmd, cwd=default_cwd):
        class ShadowProcess:
            def __init__(self):
                self.output = ""
                self.time_start = timeit.default_timer()
                self._process = subprocess.Popen(
                    cmd,
                    cwd=cwd,
                    shell=True,
                    stderr=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                )
                threading.Thread(target=self.thread_read_output).start()

            def thread_read_output(self):
                while self._process.poll() is None:
                    assert self._process.stdout is not None
                    data = self._process.stdout.read(1024)
                    if len(data) > 0:
                        self.output += data.decode(encoding="utf-8")

            def get_time_distance(self, ms=True):
                if ms:
                    return round((timeit.default_timer() - self.time_start) * 1000, 2)
                else:
                    return round(timeit.default_timer() - self.time_start, 2)

            def is_running(self):
                return self._process.poll() is None

            def is_timeout(self, timeout_ms):
                time_distance = self.get_time_distance()
                if time_distance > timeout_ms:
                    return True
                return False

            def get_output_sync(self):
                return self.output

            def get_ret_code(self):
                return self._process.poll()

            def stop_process(self):
                # noinspection PyBroadException
                try:
                    self._process.kill()
                except Exception:
                    pass

            def get_process(self):
                return self._process

            def wait_process(self):
                return self._process.wait()

        return ShadowProcess()


class DeviceRequiredUnit(BaseCLIUnit):
    """
    Make sure of device online
    """

    def before_exec(self, args: argparse.Namespace):
        ret = self.device_com.isOpen()
        if ret:
            return True
        else:
            print("Please connect to chameleon device first(use 'hw connect').")
            return False


class ReaderRequiredUnit(DeviceRequiredUnit):
    """
    Make sure of device enter to reader mode.
    """

    def before_exec(self, args: argparse.Namespace):
        if not super().before_exec(args):
            return False

        if self.cmd.is_device_reader_mode():
            return True

        self.cmd.set_device_reader_mode(True)
        print("Switch to {  Tag Reader  } mode successfully.")
        return True


class SlotIndexArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_slot_args(parser: ArgumentParserNoExit, mandatory=False):
        slot_choices = [x.value for x in SlotNumber]
        help_str = f"Slot Index: {slot_choices} Default: active slot"

        parser.add_argument(
            "-s",
            "--slot",
            type=int,
            required=mandatory,
            help=help_str,
            metavar="<1-8>",
            choices=slot_choices,
        )
        return parser


class SlotIndexArgsAndGoUnit(SlotIndexArgsUnit):
    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            self.prev_slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
            if args.slot is not None:
                self.slot_num = args.slot
                if self.slot_num != self.prev_slot_num:
                    self.cmd.set_active_slot(self.slot_num)
            else:
                self.slot_num = self.prev_slot_num
            return True
        return False

    def after_exec(self, args: argparse.Namespace):
        if self.prev_slot_num != self.slot_num:
            self.cmd.set_active_slot(self.prev_slot_num)


class SenseTypeArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_sense_type_args(parser: ArgumentParserNoExit):
        sense_group = parser.add_mutually_exclusive_group(required=True)
        sense_group.add_argument("--hf", action="store_true", help="HF type")
        sense_group.add_argument("--lf", action="store_true", help="LF type")
        return parser


class MF1AuthArgsUnit(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.add_argument(
            "--blk",
            "--block",
            type=int,
            required=True,
            metavar="<dec>",
            help="The block where the key of the card is known",
        )
        type_group = parser.add_mutually_exclusive_group()
        type_group.add_argument(
            "-a", "-A", action="store_true", help="Known key is A key (default)"
        )
        type_group.add_argument(
            "-b", "-B", action="store_true", help="Known key is B key"
        )
        parser.add_argument(
            "-k",
            "--key",
            type=str,
            required=True,
            metavar="<hex>",
            help="tag sector key",
        )
        return parser

    def get_param(self, args):
        class Param:
            def __init__(self):
                self.block = args.blk
                self.type = MfcKeyType.B if args.b else MfcKeyType.A
                key: str = args.key
                if not re.match(r"^[a-fA-F0-9]{12}$", key):
                    raise ArgsParserError("key must include 12 HEX symbols")
                self.key: bytearray = bytearray.fromhex(key)

        return Param()


class HF14AAntiCollArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_hf14a_anticoll_args(parser: ArgumentParserNoExit):
        parser.add_argument("--uid", type=str, metavar="<hex>", help="Unique ID")
        parser.add_argument(
            "--atqa", type=str, metavar="<hex>", help="Answer To Request"
        )
        parser.add_argument(
            "--sak", type=str, metavar="<hex>", help="Select AcKnowledge"
        )
        ats_group = parser.add_mutually_exclusive_group()
        ats_group.add_argument(
            "--ats", type=str, metavar="<hex>", help="Answer To Select"
        )
        ats_group.add_argument(
            "--delete-ats", action="store_true", help="Delete Answer To Select"
        )
        return parser

    def update_hf14a_anticoll(self, args, uid, atqa, sak, ats):
        anti_coll_data_changed = False
        change_requested = False
        if args.uid is not None:
            change_requested = True
            uid_str: str = args.uid.strip()
            if re.match(r"[a-fA-F0-9]+", uid_str) is not None:
                new_uid = bytes.fromhex(uid_str)
                if len(new_uid) not in [4, 7, 10]:
                    raise Exception("UID length error")
            else:
                raise Exception("UID must be hex")
            if new_uid != uid:
                uid = new_uid
                anti_coll_data_changed = True
            else:
                print(color_string((CY, "Requested UID already set")))
        if args.atqa is not None:
            change_requested = True
            atqa_str: str = args.atqa.strip()
            if re.match(r"[a-fA-F0-9]{4}", atqa_str) is not None:
                new_atqa = bytes.fromhex(atqa_str)
            else:
                raise Exception("ATQA must be 4-byte hex")
            if new_atqa != atqa:
                atqa = new_atqa
                anti_coll_data_changed = True
            else:
                print(color_string((CY, "Requested ATQA already set")))
        if args.sak is not None:
            change_requested = True
            sak_str: str = args.sak.strip()
            if re.match(r"[a-fA-F0-9]{2}", sak_str) is not None:
                new_sak = bytes.fromhex(sak_str)
            else:
                raise Exception("SAK must be 2-byte hex")
            if new_sak != sak:
                sak = new_sak
                anti_coll_data_changed = True
            else:
                print(color_string((CY, "Requested SAK already set")))
        if (args.ats is not None) or args.delete_ats:
            change_requested = True
            if args.delete_ats:
                new_ats = b""
            else:
                ats_str: str = args.ats.strip()
                if re.match(r"[a-fA-F0-9]+", ats_str) is not None:
                    new_ats = bytes.fromhex(ats_str)
                else:
                    raise Exception("ATS must be hex")
            if new_ats != ats:
                ats = new_ats
                anti_coll_data_changed = True
            else:
                print(color_string((CY, "Requested ATS already set")))
        if anti_coll_data_changed:
            self.cmd.hf14a_set_anti_coll_data(uid, atqa, sak, ats)
        return change_requested, anti_coll_data_changed, uid, atqa, sak, ats


class MFUAuthArgsUnit(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()

        def key_parser(key: str) -> bytes:
            try:
                key = bytes.fromhex(key)
            except ValueError:
                raise ValueError("Key should be a hex string")

            if len(key) not in [4, 16]:
                raise ValueError("Key should either be 4 or 16 bytes long")
            elif len(key) == 16:
                raise ValueError("Ultralight-C authentication isn't supported yet")

            return key

        parser.add_argument(
            "-k",
            "--key",
            type=key_parser,
            metavar="<hex>",
            help="Authentication key (EV1/NTAG 4 bytes).",
        )
        parser.add_argument(
            "-l",
            action="store_true",
            dest="swap_endian",
            help="Swap endianness of the key.",
        )

        return parser

    def get_param(self, args):
        key = args.key

        if key is not None and args.swap_endian:
            key = bytearray(key)
            for i in range(len(key)):
                key[i] = key[len(key) - 1 - i]
            key = bytes(key)

        class Param:
            def __init__(self, key):
                self.key = key

        return Param(key)

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class LFEMIdArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument(
            "--id", type=str, required=required, help="EM410x tag id", metavar="<hex>"
        )
        return parser

    def before_exec(self, args: argparse.Namespace):
        if not super().before_exec(args):
            return False
        if args.id is None or not re.match(r"^[a-fA-F0-9]{10}$", args.id):
            raise ArgsParserError("ID must include 10 HEX symbols")
        return True

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class LFHIDIdArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit, required=False):
        formats = [x.name for x in HIDFormat]
        parser.add_argument(
            "-",
            "--format",
            type=str,
            required=required,
            help="HIDProx card format",
            metavar="",
            choices=formats,
        )
        parser.add_argument(
            "--fc",
            type=int,
            required=False,
            help="HIDProx tag facility code",
            metavar="<int>",
        )
        parser.add_argument(
            "--cn",
            type=int,
            required=required,
            help="HIDProx tag card number",
            metavar="<int>",
        )
        parser.add_argument(
            "--il",
            type=int,
            required=False,
            help="HIDProx tag issue level",
            metavar="<int>",
        )
        parser.add_argument(
            "--oem", type=int, required=False, help="HIDProx tag OEM", metavar="<int>"
        )
        return parser

    @staticmethod
    def check_limits(
        format: int,
        fc: Union[int, None],
        cn: Union[int, None],
        il: Union[int, None],
        oem: Union[int, None],
    ):
        limits = {
            HIDFormat.H10301: [0xFF, 0xFFFF, 0, 0],
            HIDFormat.IND26: [0xFFF, 0xFFF, 0, 0],
            HIDFormat.IND27: [0x1FFF, 0x3FFF, 0, 0],
            HIDFormat.INDASC27: [0x1FFF, 0x3FFF, 0, 0],
            HIDFormat.TECOM27: [0x7FF, 0xFFFF, 0, 0],
            HIDFormat.W2804: [0xFF, 0x7FFF, 0, 0],
            HIDFormat.IND29: [0x1FFF, 0xFFFF, 0, 0],
            HIDFormat.ATSW30: [0xFFF, 0xFFFF, 0, 0],
            HIDFormat.ADT31: [0xF, 0x7FFFFF, 0, 0],
            HIDFormat.HCP32: [0, 0x3FFF, 0, 0],
            HIDFormat.HPP32: [0xFFF, 0x7FFFF, 0, 0],
            HIDFormat.KASTLE: [0xFF, 0xFFFF, 0x1F, 0],
            HIDFormat.KANTECH: [0xFF, 0xFFFF, 0, 0],
            HIDFormat.WIE32: [0xFFF, 0xFFFF, 0, 0],
            HIDFormat.D10202: [0x7F, 0xFFFFFF, 0, 0],
            HIDFormat.H10306: [0xFFFF, 0xFFFF, 0, 0],
            HIDFormat.N10002: [0xFFFF, 0xFFFF, 0, 0],
            HIDFormat.OPTUS34: [0x3FF, 0xFFFF, 0, 0],
            HIDFormat.SMP34: [0x3FF, 0xFFFF, 0x7, 0],
            HIDFormat.BQT34: [0xFF, 0xFFFFFF, 0, 0],
            HIDFormat.C1K35S: [0xFFF, 0xFFFFF, 0, 0],
            HIDFormat.C15001: [0xFF, 0xFFFF, 0, 0x3FF],
            HIDFormat.S12906: [0xFF, 0xFFFFFF, 0x3, 0],
            HIDFormat.SIE36: [0x3FFFF, 0xFFFF, 0, 0],
            HIDFormat.H10320: [0, 99999999, 0, 0],
            HIDFormat.H10302: [0, 0x7FFFFFFFF, 0, 0],
            HIDFormat.H10304: [0xFFFF, 0x7FFFF, 0, 0],
            HIDFormat.P10004: [0x1FFF, 0x3FFFF, 0, 0],
            HIDFormat.HGEN37: [0, 0xFFFFFFFF, 0, 0],
            HIDFormat.MDI37: [0xF, 0x1FFFFFFF, 0, 0],
        }
        limit = limits.get(HIDFormat(format))
        if limit is None:
            return True
        if fc is not None and fc > limit[0]:
            raise ArgsParserError(
                f"{HIDFormat(format)}: Facility Code must between 0 to {limit[0]}"
            )
        if cn is not None and cn > limit[1]:
            raise ArgsParserError(
                f"{HIDFormat(format)}: Card Number must between 0 to {limit[1]}"
            )
        if il is not None and il > limit[2]:
            raise ArgsParserError(
                f"{HIDFormat(format)}: Issue Level must between 0 to {limit[2]}"
            )
        if oem is not None and oem > limit[3]:
            raise ArgsParserError(
                f"{HIDFormat(format)}: OEM must between 0 to {limit[3]}"
            )

    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            format = HIDFormat.H10301.value
            if args.format is not None:
                format = HIDFormat[args.format].value
            LFHIDIdArgsUnit.check_limits(format, args.fc, args.cn, args.il, args.oem)
            return True
        return False

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


class LFHIDIdReadArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit, required=False):
        formats = [x.name for x in HIDFormat]
        parser.add_argument(
            "-",
            "--format",
            type=str,
            required=False,
            help="HIDProx card format hint",
            metavar="",
            choices=formats,
        )
        return parser

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


class LFVikingIdArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument(
            "--id", type=str, required=required, help="Viking tag id", metavar="<hex>"
        )
        return parser

    def before_exec(self, args: argparse.Namespace):
        if not super().before_exec(args):
            return False
        if args.id is None or not re.match(r"^[a-fA-F0-9]{8}$", args.id):
            raise ArgsParserError("ID must include 8 HEX symbols")
        return True

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class TagTypeArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_type_args(parser: ArgumentParserNoExit):
        type_names = [t.name for t in TagSpecificType.list()]
        help_str = "Tag Type: " + ", ".join(type_names)
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=True,
            metavar="TAG_TYPE",
            help=help_str,
            choices=type_names,
        )
        return parser

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


root = CLITree(root=True)
hw = root.subgroup("hw", "Hardware-related commands")
hw_slot = hw.subgroup("slot", "Emulation slots commands")
hw_settings = hw.subgroup("settings", "Chameleon settings commands")

hf = root.subgroup("hf", "High Frequency commands")
hf_14a = hf.subgroup("14a", "ISO14443-a commands")
hf_mf = hf.subgroup("mf", "MIFARE Classic commands")
hf_mfu = hf.subgroup("mfu", "MIFARE Ultralight / NTAG commands")

lf = root.subgroup("lf", "Low Frequency commands")
lf_em = lf.subgroup("em", "EM commands")
lf_em_410x = lf_em.subgroup("410x", "EM410x commands")
lf_hid = lf.subgroup("hid", "HID commands")
lf_hid_prox = lf_hid.subgroup("prox", "HID Prox commands")
lf_viking = lf.subgroup("viking", "Viking commands")


@root.command("clear")
class RootClear(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Clear screen"
        return parser

    def on_exec(self, args: argparse.Namespace):
        os.system("clear" if os.name == "posix" else "cls")


@root.command("rem")
class RootRem(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Timestamped comment"
        parser.add_argument("comment", nargs="*", help="Your comment")
        return parser

    def on_exec(self, args: argparse.Namespace):
        # precision: second
        # iso_timestamp = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')
        # precision: nanosecond (note that the comment will take some time too, ~75ns, check your system)
        iso_timestamp = datetime.utcnow().isoformat() + "Z"
        comment = " ".join(args.comment)
        print(f"{iso_timestamp} remark: {comment}")


@root.command("exit")
class RootExit(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Exit client"
        return parser

    def on_exec(self, args: argparse.Namespace):
        print("Bye, thank you.  ^.^ ")
        self.device_com.close()
        sys.exit(996)


@root.command("dump_help")
class RootDumpHelp(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Dump available commands"
        parser.add_argument(
            "-d",
            "--show-desc",
            action="store_true",
            help="Dump full command description",
        )
        parser.add_argument(
            "-g",
            "--show-groups",
            action="store_true",
            help="Dump command groups as well",
        )
        return parser

    @staticmethod
    def dump_help(cmd_node, depth=0, dump_cmd_groups=False, dump_description=False):
        visual_col1_width = 28
        col1_width = visual_col1_width + len(f"{CG}{C0}")
        if cmd_node.cls:
            p = cmd_node.cls().args_parser()
            assert p is not None
            if dump_description:
                p.print_help()
            else:
                cmd_title = color_string((CG, cmd_node.fullname))
                print(f"{cmd_title}".ljust(col1_width), end="")
                p.prog = " " * (visual_col1_width - len("usage: ") - 1)
                usage = p.format_usage().removeprefix("usage: ").strip()
                print(color_string((CY, usage)))
        else:
            if dump_cmd_groups and not cmd_node.root:
                if dump_description:
                    print("=" * 80)
                    print(color_string((CR, cmd_node.fullname)))
                    print(color_string((CC, cmd_node.help_text)))
                else:
                    print(color_string((CB, f"== {cmd_node.fullname} ==")))
            for child in cmd_node.children:
                RootDumpHelp.dump_help(
                    child, depth + 1, dump_cmd_groups, dump_description
                )

    def on_exec(self, args: argparse.Namespace):
        self.dump_help(
            root, dump_cmd_groups=args.show_groups, dump_description=args.show_desc
        )


@hw.command("connect")
class HWConnect(BaseCLIUnit):
    OPEN_RETRY_COUNT = 6
    OPEN_RETRY_DELAY_SEC = 0.35

    # Comprehensive list of well-known MIFARE Classic keys
    _LARGE_KEY_DICT = [
        # Standard NXP / factory defaults
        "FFFFFFFFFFFF",
        "000000000000",
        "A0A1A2A3A4A5",
        "B0B1B2B3B4B5",
        "D3F7D3F7D3F7",
        "C0C1C2C3C4C5",
        "E0E1E2E3E4E5",
        "F0F1F2F3F4F5",
        # mfoc / mfcuk bundled defaults
        "AABBCCDDEEFF",
        "1A2B3C4D5E6F",
        "010203040506",
        "111111111111",
        "222222222222",
        "333333333333",
        "444444444444",
        "555555555555",
        "666666666666",
        "777777777777",
        "888888888888",
        "999999999999",
        "AAAAAAAAAAAA",
        "BBBBBBBBBBBB",
        "CCCCCCCCCCCC",
        "DDDDDDDDDDDD",
        "EEEEEEEEEEEE",
        # Known deployed card keys
        "4D3A99C351DD",
        "1A982C7E459A",
        "714C5C886E97",
        "587EE5F9350F",
        "A0478CC39091",
        "533CB6C723F6",
        "8FD0A4F256E9",
        "0000014B5C31",
        "B578F38A5C61",
        "96A301BCE267",
        # Transport / access control
        "FC00018778F7",
        "E2430B3264C6",
        "9DB5AD10ACE1",
        "FCF7585340F5",
        "74B6EDA27E1D",
        "C49F6A65F9F0",
        "BF0CC29F3D3F",
        "7EBFE49EB836",
        "AB2654D3FAB1",
        "4B79141E26DF",
        "ABC30C9A38EB",
        "2B7F3253FAC5",
        "AECA78578C9B",
        "3EA2811BFAC2",
        "C68480E87B19",
        "7C6E7E8F1B2A",
        # Sequential / patterned keys
        "ABCDEF012345",
        "FEDCBA987654",
        "123456789ABC",
        "A1B2C3D4E5F6",
        "DEADBEEFCAFE",
        "1234567890AB",
        "AA1234567890",
        "0102030405AB",
        "A0B0C0D0E0F0",
        "0F1E2D3C4B5A",
        "0123456789AB",
        "BA9876543210",
        # Hotel / building access
        "030047AC0E0E",
        "030047AC6669",
        "3B7E4FD575AD",
        "A73F5DC1D333",
        "25B055B3011E",
        "21A236B0DEE8",
        "2AD9F1C29F64",
        "3DEA111A4F21",
        "B0CB8FAE4B47",
        "27D8ACB98A08",
        "F8CB44E0A44A",
        "7E8B60E70EEC",
        # Credit / parking / vending
        "1EAD827D01A2",
        "E83F9605ABD7",
        "3523044DECE9",
        "CF6A32A3B27E",
        "9C1EB79E97AE",
        "B26B49AB8A18",
        "ECEEE80E7E97",
        "399DACDBCC14",
        "7B3E2019A4BF",
        "E05B23D2C4E0",
        "AD6E4E5DD1AE",
        "DBCD9C51C60D",
        # More public default keys
        "A1A2A3A4A5A6",
        "B1B2B3B4B5B6",
        "C1C2C3C4C5C6",
        "204B8FE4B401",
        "F4B72AF64AAF",
        "2B1E4C7D89AB",
        "94A1E68DEB4A",
        "AB68B3F98FBB",
        "BBAE5EB9A43F",
        "5A9FB3D5AF14",
        "0FF3FE09CFA7",
        "38FCC38E70EE",
        "D72A3C05B0E8",
        "F1B63B195A24",
        "CA9D5AFC5FAD",
        "5C237D3B487E",
        "FF906B720ECA",
        "5B45F729C9E6",
        "25B255B203C1",
        "5BD494EEB66A",
        "F2CDAB2C6AB4",
        "FD852843EFA0",
        "ED28E6EF3C36",
        "A7BBCDB62B65",
        "4CA68DADCCB1",
        "26973EA74321",
        "CF0A2E27BD38",
        "2E06EE11BFF2",
        "12F3BCA35AB1",
        "3AB72A3E421C",
        "E8A2B012C4D6",
        "1B2C3D4E5F60",
        "6F5E4D3C2B1A",
        "0102040810FF",
        "FF080402010F",
        "42DEAD4242DE",
        "4B45524E454C",
        "424F554E4345",
        "4C4F43414C4C",
        "5354415449C3",
        "68FF6B720ECA",
        "4CA1D1AA38AB",
        "1A3C7D4E5AB2",
        "31ABCDEF1234",
    ]

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Connect to chameleon by serial port"
        parser.add_argument("-p", "--port", type=str, required=False)
        return parser

    @staticmethod
    def _is_port_busy_error(exc: Exception) -> bool:
        msg = str(exc).lower()
        return (
            "access is denied" in msg
            or "permissionerror" in msg
            or "resource busy" in msg
            or "could not open port" in msg
        )

    def _open_with_retry(self, port: str):
        last_exc = None
        for attempt in range(1, self.OPEN_RETRY_COUNT + 1):
            try:
                self.device_com.open(port)
                return
            except Exception as exc:
                last_exc = exc
                if (
                    not self._is_port_busy_error(exc)
                    or attempt == self.OPEN_RETRY_COUNT
                ):
                    raise
                wait_s = self.OPEN_RETRY_DELAY_SEC
                print(
                    color_string(
                        (
                            CY,
                            f"Port busy, retrying ({attempt}/{self.OPEN_RETRY_COUNT}) in {wait_s:.2f}s...",
                        )
                    )
                )
                time.sleep(wait_s)

        if last_exc is not None:
            raise last_exc

    def on_exec(self, args: argparse.Namespace):
        try:
            if args.port is None:  # Chameleon auto-detect if no port is supplied
                platform_name = uname().release
                if "Microsoft" in platform_name:
                    path = os.environ["PATH"].split(os.pathsep)
                    path.append("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/")
                    powershell_path = None
                    for prefix in path:
                        fn = os.path.join(prefix, "powershell.exe")
                        if not os.path.isdir(fn) and os.access(fn, os.X_OK):
                            powershell_path = fn
                            break
                    if powershell_path:
                        process = subprocess.Popen(
                            [
                                powershell_path,
                                "Get-PnPDevice -Class Ports -PresentOnly |"
                                " where {$_.DeviceID -like '*VID_6868&PID_8686*'} |"
                                " Select-Object -First 1 FriendlyName |"
                                " % FriendlyName |"
                                " select-string COM\\d+ |"
                                "% { $_.matches.value }",
                            ],
                            stdout=subprocess.PIPE,
                        )
                        res = process.communicate()[0]
                        _comport = res.decode("utf-8").strip()
                        if _comport:
                            args.port = _comport.replace("COM", "/dev/ttyS")
                else:
                    # loop through all ports and find chameleon
                    for port in serial.tools.list_ports.comports():
                        if port.vid == 0x6868:
                            args.port = port.device
                            break
                if args.port is None:  # If no chameleon was found, exit
                    print(
                        "Chameleon not found, please connect the device or try connecting manually with the -p flag."
                    )
                    return
            self._open_with_retry(args.port)
            self.device_com.commands = self.cmd.get_device_capabilities()
            major, minor = self.cmd.get_app_version()
            model = ["Ultra", "Lite"][self.cmd.get_device_model()]
            print(f" {{ Chameleon {model} connected: v{major}.{minor} }}")

            # Auto-scan: switch to reader mode, scan LF then HF
            self._auto_scan()

        except Exception as e:
            print(color_string((CR, f"Chameleon Connect fail: {str(e)}")))
            self.device_com.close()

    def _auto_scan(self):
        try:
            # Switch to reader mode
            if not self.cmd.is_device_reader_mode():
                self.cmd.set_device_reader_mode(True)
            print(f"\n [*] {color_string((CG, 'Reader mode active - scanning...'))}")

            # ---- HF Scan ----
            print(f"\n {'-' * 40}")
            print(f" [*] {color_string((CC, 'HF Scan (13.56 MHz)'))}")
            print(f" {'-' * 40}")

            self._switch_to_hf_antenna()

            print("   [*] ISO14443-A... ", end="", flush=True)
            try:
                resp = None
                for attempt in range(6):
                    resp = self.cmd.hf14a_scan()
                    if resp is not None and len(resp) > 0:
                        break
                    # Re-prime HF field between attempts to improve unstable tag wakeup.
                    if attempt in (1, 3, 5):
                        self._prepare_hf_antenna()
                        self._force_hf_wakeup()
                    time.sleep(0.10)

                # Final fallback: force wakeup and perform one more structured scan.
                if resp is None or len(resp) == 0:
                    self._prepare_hf_antenna()
                    self._force_hf_wakeup()
                    resp = self.cmd.hf14a_scan()

                if resp is not None and len(resp) > 0:
                    print(f"{color_string((CG, f'FOUND {len(resp)} tag(s)!'))}")
                    for idx, tag in enumerate(resp):
                        if len(resp) > 1:
                            print(f"\n       --- Tag {idx + 1} ---")
                        print(
                            f"       UID  : {color_string((CG, tag['uid'].hex().upper()))}"
                        )
                        print(
                            f"       ATQA : {tag['atqa'].hex().upper()} "
                            f"(0x{int.from_bytes(tag['atqa'], byteorder='little'):04x})"
                        )
                        sak_val = tag["sak"][0]
                        print(f"       SAK  : {tag['sak'].hex().upper()}")
                        if len(tag["ats"]) > 0:
                            print(f"       ATS  : {tag['ats'].hex().upper()}")
                        if sak_val in type_id_SAK_dict:
                            print(
                                f"       Type : {color_string((CG, type_id_SAK_dict[sak_val]))}"
                            )
                        if sak_val in (0x08, 0x09, 0x18, 0x19, 0x28, 0x38):
                            print(
                                f"       Note : {color_string((CY, 'MIFARE Classic detected'))}"
                            )
                            # Determine card size from SAK
                            if sak_val == 0x09:
                                mfc_size = "Mini"
                            elif sak_val in (0x08, 0x28):
                                mfc_size = "1K"
                            elif sak_val == 0x19:
                                mfc_size = "2K"
                            elif sak_val in (0x18, 0x38):
                                mfc_size = "4K"
                            else:
                                mfc_size = "Unknown"
                            prng = None
                            if len(resp) == 1:
                                try:
                                    if self.cmd.mf1_detect_support():
                                        prng = self.cmd.mf1_detect_prng()
                                        print(
                                            f"       PRNG : {MifareClassicPrngType(prng)}"
                                        )
                                except Exception:
                                    pass
                            self._recommend_attack(sak_val, mfc_size, prng, tag)
                            if len(resp) == 1:
                                self._auto_read_mf_classic_value_blocks(mfc_size)
                        elif sak_val == 0x00:
                            self._recommend_attack(sak_val, "UL", None, tag)
                        elif sak_val == 0x20:
                            self._recommend_attack(sak_val, "DESFire", None, tag)
                        else:
                            self._recommend_attack(sak_val, None, None, tag)
                else:
                    print(f"{color_string((CY, 'no tag'))}")
            except Exception:
                print(f"{color_string((CY, 'no tag'))}")

            # ---- LF Scan ----
            print(f"\n {'-' * 40}")
            print(f" [*] {color_string((CC, 'LF Scan (125 kHz)'))}")
            print(f" {'-' * 40}")

            self._switch_to_lf_antenna()

            # EM410x
            print("   [*] EM410x... ", end="", flush=True)
            try:
                resp = self.cmd.em410x_scan()
                if resp is not None:
                    tag_type, uid = resp
                    print(f"{color_string((CG, 'FOUND!'))}")
                    print(f"       Type : {TagSpecificType(tag_type)}")
                    print(f"       UID  : {color_string((CG, uid.hex().upper()))}")
                    uid_int = int.from_bytes(uid, byteorder="big")
                    print(f"       Dec  : {uid_int}")
                    if len(uid) == 5:
                        fc = (uid[2] << 1) | (uid[3] >> 7)
                        cn = ((uid[3] & 0x7F) << 8) | uid[4]
                        print(f"       FC   : {fc}  CN: {cn}")
                else:
                    print(f"{color_string((CY, 'no tag'))}")
            except Exception:
                print(f"{color_string((CY, 'no tag'))}")

            # HID Prox
            print("   [*] HID Prox... ", end="", flush=True)
            try:
                resp = self.cmd.hidprox_scan(0)
                if resp is not None:
                    print(f"{color_string((CG, 'FOUND!'))}")
                    print(f"       Format: {resp[0]}  FC: {resp[1]}  CN: {resp[2]}")
                else:
                    print(f"{color_string((CY, 'no tag'))}")
            except Exception:
                print(f"{color_string((CY, 'no tag'))}")

            # Viking
            print("   [*] Viking... ", end="", flush=True)
            try:
                resp = self.cmd.viking_scan()
                if resp is not None:
                    print(f"{color_string((CG, 'FOUND!'))}")
                    print(f"       UID  : {color_string((CG, resp.hex().upper()))}")
                else:
                    print(f"{color_string((CY, 'no tag'))}")
            except Exception:
                print(f"{color_string((CY, 'no tag'))}")

            print(f"\n [*] {color_string((CG, 'Auto scan complete.'))}")
        except Exception as e:
            print(f"\n [*] {color_string((CY, f'Auto scan skipped: {e}'))}")

    @staticmethod
    def _sector_count_from_mfc_size(mfc_size):
        return {
            "Mini": 5,
            "1K": 16,
            "2K": 32,
            "4K": 40,
        }.get(mfc_size, 16)

    @staticmethod
    def _sector_layout(sector_no):
        if sector_no < 32:
            first_block = sector_no * 4
            block_count = 4
        else:
            first_block = 128 + (sector_no - 32) * 16
            block_count = 16
        return first_block, block_count

    def _reset_hf_field_for_darkside(self):
        """Drop then re-raise the HF field so the tag returns to IDLE state."""
        for activate in (0, 1):
            try:
                self.cmd.hf14a_raw(
                    options={
                        "activate_rf_field": activate,
                        "wait_response": 0,
                        "append_crc": 0,
                        "auto_select": 0,
                        "keep_rf_field": activate,
                        "check_response_crc": 0,
                    },
                    resp_timeout_ms=20,
                    data=[0x26],
                    bitlen=7,
                )
            except Exception:
                pass
        time.sleep(0.08)

    def _darkside_recover_key(self, block_target, type_target):
        """Bootstrap one key via darkside attack. Returns hex string or None."""
        print(
            f"       [Darkside] Targeting block {block_target} type {type_target.name} ..."
        )
        self._reset_hf_field_for_darkside()
        darkside_list = []
        first_recover = True
        retry_count = 0
        while retry_count < 0xFF:
            try:
                darkside_resp = self.cmd.mf1_darkside_acquire(
                    block_target, type_target, first_recover, 30
                )
            except Exception as exc:
                print(f"       [Darkside] Acquire error: {exc}")
                break
            first_recover = False
            status = darkside_resp[0]
            if status != MifareClassicDarksideStatus.OK:
                print(f"       [Darkside] {MifareClassicDarksideStatus(status)}")
                break
            obj = darkside_resp[1]
            if obj["par"] != 0:
                darkside_list.clear()
            darkside_list.append(obj)
            params = obj["uid"]
            for item in darkside_list:
                params += f" {item['nt1']} {item['ks1']} {item['par']}"
                params += f" {item['nr']} {item['ar']}"
            cmd_recover = (
                f"darkside.exe {params}"
                if sys.platform == "win32"
                else f"./darkside {params}"
            )
            process = self.sub_process(cmd_recover)
            process.wait_process()
            output = process.get_output_sync()
            if "key not found" in output:
                retry_count += 1
                continue
            for line in output.split("\n"):
                m = re.search(r"([a-fA-F0-9]{12})", line)
                if m:
                    candidate = m.group(1)
                    if self.cmd.mf1_auth_one_key_block(
                        block_target, type_target, bytearray.fromhex(candidate)
                    ):
                        print(f"       [Darkside] Key: {candidate}")
                        return candidate
            retry_count += 1
        return None

    def _nested_recover_key(
        self, block_known, type_known, key_known, block_target, type_target
    ):
        """Recover one key via nested/staticnested. Returns hex string or None."""
        try:
            nt_level = self.cmd.mf1_detect_prng()
            if nt_level == 2:
                return None  # hardnested not handled here
            if nt_level == 0:
                nt_uid_obj = self.cmd.mf1_static_nested_acquire(
                    block_known, type_known, key_known, block_target, type_target
                )
                cmd_param = f"{nt_uid_obj['uid']} {int(type_target)}"
                for nt_item in nt_uid_obj["nts"]:
                    cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']}"
                tool_name = "staticnested"
            else:
                dist_obj = self.cmd.mf1_detect_nt_dist(
                    block_known, type_known, key_known
                )
                nt_obj = self.cmd.mf1_nested_acquire(
                    block_known, type_known, key_known, block_target, type_target
                )
                cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
                for nt_item in nt_obj:
                    cmd_param += (
                        f" {nt_item['nt']} {nt_item['nt_enc']} {nt_item['par']}"
                    )
                tool_name = "nested"
            cmd_recover = (
                f"{tool_name}.exe {cmd_param}"
                if sys.platform == "win32"
                else f"./{tool_name} {cmd_param}"
            )
            process = self.sub_process(cmd_recover)
            elapsed = 0.0
            while process.is_running():
                time.sleep(0.1)
                elapsed += 0.1
                if elapsed > 120:
                    process.stop_process()
                    return None
            if process.get_ret_code() == 0:
                output = process.get_output_sync()
                for line in output.split("\n"):
                    m = re.search(r"([a-fA-F0-9]{12})", line)
                    if m:
                        candidate = m.group(1)
                        if self.cmd.mf1_auth_one_key_block(
                            block_target, type_target, bytearray.fromhex(candidate)
                        ):
                            return candidate
        except Exception as exc:
            print(f"       [Nested] Error: {exc}")
        return None

    def _auto_read_mf_classic_value_blocks(self, mfc_size):
        print(f"\n       {'-' * 44}")
        print(f"       {color_string((CC, 'AUTO APPLY KEYS + VALUE READOUT'))}")
        print(f"       {'-' * 44}")

        max_sectors = self._sector_count_from_mfc_size(mfc_size)

        # Mask off sectors beyond the card size
        base_mask = bytearray.fromhex("00000000000000000000")
        for i in range(max_sectors, 40):
            base_mask[i // 4] |= 3 << (6 - i % 4 * 2)

        # ── Phase 1: large dictionary ────────────────────────────────────────
        print(
            f"       Phase 1: Dictionary attack ({len(self._LARGE_KEY_DICT)} keys)..."
        )
        raw_sector_keys: dict = {}
        try:
            dict_keys = [bytes.fromhex(k) for k in self._LARGE_KEY_DICT]
            mask = bytearray(base_mask)
            fchk = HFMFFCHK()
            fchk.device_com = self.device_com
            raw_sector_keys = fchk.check_keys(mask, dict_keys)
        except Exception as exc:
            print(f"       {color_string((CR, f'Dictionary attack failed: {exc}'))}")

        # known_keys: sector_no -> (MfcKeyType, bytearray)
        known_keys: dict = {}
        for sector in range(max_sectors):
            ka = raw_sector_keys.get(2 * sector)
            kb = raw_sector_keys.get(2 * sector + 1)
            if ka:
                known_keys[sector] = (MfcKeyType.A, bytearray(ka))
            elif kb:
                known_keys[sector] = (MfcKeyType.B, bytearray(kb))

        unknown = [s for s in range(max_sectors) if s not in known_keys]
        print(
            f"       After dict: {color_string((CG, str(len(known_keys))))} / "
            f"{max_sectors} sectors known, {len(unknown)} remaining"
        )

        # ── Phase 2: darkside bootstrap if nothing found yet ─────────────────
        if unknown and not known_keys:
            print(
                "       Phase 2: No keys found — running darkside to bootstrap first key..."
            )
            ds_found = False
            for ds_type in (MfcKeyType.A, MfcKeyType.B):
                key = self._darkside_recover_key(3, ds_type)  # sector-0 trailer
                if key:
                    known_keys[0] = (ds_type, bytearray.fromhex(key))
                    unknown = [s for s in range(max_sectors) if s not in known_keys]
                    ds_found = True
                    break
            if not ds_found:
                print(
                    f"       {color_string((CY, 'Darkside failed — cannot bootstrap further.'))}"
                )

        # ── Phase 3: nested propagation until all sectors covered ────────────
        if unknown and known_keys:
            print(
                f"       Phase 3: Nested attack on {len(unknown)} remaining sector(s)..."
            )
            while unknown:
                progress = False
                for sector in list(unknown):
                    first_blk, blk_cnt = self._sector_layout(sector)
                    target_block = first_blk + blk_cnt - 1  # trailer block
                    recovered = False
                    # Try every known anchor until one works
                    for anchor_sector, (anchor_type, anchor_key) in list(
                        known_keys.items()
                    ):
                        anchor_blk, anchor_cnt = self._sector_layout(anchor_sector)
                        anchor_block = anchor_blk + anchor_cnt - 1
                        for tgt_type in (MfcKeyType.A, MfcKeyType.B):
                            print(
                                f"         [Nested] Sector {sector:02d} key"
                                f" {tgt_type.name} (anchor s{anchor_sector:02d})...",
                                end=" ",
                                flush=True,
                            )
                            key = self._nested_recover_key(
                                anchor_block,
                                anchor_type,
                                bytes(anchor_key),
                                target_block,
                                tgt_type,
                            )
                            if key is None:
                                print("not found")
                                continue
                            print(f"found: {key}")
                            known_keys[sector] = (tgt_type, bytearray.fromhex(key))
                            unknown = [
                                s for s in range(max_sectors) if s not in known_keys
                            ]
                            progress = True
                            recovered = True
                            break
                        if recovered:
                            break
                if not progress:
                    print(
                        f"       {color_string((CY, f'Nested stalled — {len(unknown)} sector(s) still unknown.'))}"
                    )
                    break

        resolved = len(known_keys)
        print(
            f"\n       Keys recovered: {color_string((CG, str(resolved)))} / {max_sectors} sectors"
        )
        if unknown:
            print(f"       {color_string((CY, f'Sectors without keys: {unknown}'))}")

        # ── Phase 4: read and display value blocks ───────────────────────────
        found_any_value = False
        print("       Value blocks (hex + decimal):")
        for sector in range(max_sectors):
            if sector not in known_keys:
                continue
            key_type, key_data = known_keys[sector]
            first_block, block_count = self._sector_layout(sector)
            for blk_offset in range(block_count - 1):
                block = first_block + blk_offset
                try:
                    data = bytes(self.cmd.mf1_read_one_block(block, key_type, key_data))
                except Exception:
                    continue
                is_val, value, _addr = HFMFValueFind._is_value_block(data)
                if not is_val:
                    continue
                found_any_value = True
                hex_str = data.hex().upper()
                dec_str = str(value)
                print(
                    f"         Sector {sector:02d} Block {block:03d}"
                    f"  HEX {hex_str}"
                    f"  DEC {color_string((CG, dec_str))}"
                )

        if not found_any_value:
            print("         No MIFARE value blocks found with recovered keys.")

    def _prepare_hf_antenna(self):
        try:
            # LF commands may leave the RF frontend in a transient state; pulse HF field and send REQA once.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 1,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 1,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=30,
                data=[0x26],
                bitlen=7,
            )
        except Exception:
            pass
        time.sleep(0.05)

    def _switch_to_lf_antenna(self):
        try:
            # Ensure HF field is dropped before LF scans.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 0,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 0,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=20,
                data=[],
            )
        except Exception:
            pass
        time.sleep(0.03)

    def _switch_to_hf_antenna(self):
        try:
            # Drop any previous HF state before re-activating for a clean LF->HF handoff.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 0,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 0,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=20,
                data=[],
            )
        except Exception:
            pass
        time.sleep(0.03)
        self._prepare_hf_antenna()

    def _force_hf_wakeup(self):
        try:
            options = {
                "activate_rf_field": 1,
                "wait_response": 1,
                "append_crc": 0,
                "auto_select": 0,
                "keep_rf_field": 1,
                "check_response_crc": 0,
            }
            # Try both REQA and WUPA short frames.
            for wake_cmd in (0x26, 0x52):
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=40,
                    data=[wake_cmd],
                    bitlen=7,
                )
                if resp is not None and len(resp) > 0:
                    return True
                time.sleep(0.02)
        except Exception:
            pass
        return False

    def _recommend_attack(self, sak, card_size, prng, tag):
        """Print attack recommendations based on detected card type and PRNG."""
        print(f"\n       {'-' * 44}")
        print(f"       {color_string((CC, 'RECOMMENDED ATTACK STRATEGY'))}")
        print(f"       {'-' * 44}")

        # --- MIFARE Classic ---
        if sak in (0x08, 0x09, 0x18, 0x19, 0x28, 0x38):
            size_flag = {
                "Mini": "--mini",
                "1K": "--1k",
                "2K": "--2k",
                "4K": "--4k",
            }.get(card_size, "--1k")

            if prng is not None:
                prng_type = MifareClassicPrngType(prng)

                if prng_type == MifareClassicPrngType.STATIC:
                    print(f"       Card  : MIFARE Classic {card_size} (Static PRNG)")
                    print(
                        f"       Vuln  : {color_string((CG, 'STATIC NONCE - highly vulnerable'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print(
                        "         2. If a key found, recover all keys via static nested:"
                    )
                    print(
                        f"            {color_string((CY, 'hf mf nested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print("         3. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print("         4. Scan for value blocks:")
                    print(
                        f"            {color_string((CY, f'hf mf vfind {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CG, 'FAST - seconds per key'))}"
                    )

                elif prng_type == MifareClassicPrngType.WEAK:
                    print(f"       Card  : MIFARE Classic {card_size} (Weak PRNG)")
                    print(
                        f"       Vuln  : {color_string((CG, 'WEAK PRNG - vulnerable to nested + darkside'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print("         2a. If a key found, recover remaining via nested:")
                    print(
                        f"            {color_string((CY, 'hf mf nested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print(
                        "         2b. If NO key found, use darkside to get first key:"
                    )
                    print(f"            {color_string((CY, 'hf mf darkside'))}")
                    print(
                        "         3. Then nested to recover all keys from the darkside key"
                    )
                    print("         4. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print("         5. Scan for value blocks:")
                    print(
                        f"            {color_string((CY, f'hf mf vfind {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CG, 'FAST - seconds to minutes'))}"
                    )

                elif prng_type == MifareClassicPrngType.HARD:
                    print(f"       Card  : MIFARE Classic {card_size} (Hard PRNG)")
                    print(
                        f"       Vuln  : {color_string((CY, 'HARDENED PRNG - requires hard nested attack'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print("         2. If a key found, recover via hard nested:")
                    print(
                        f"            {color_string((CY, 'hf mf hardnested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print(
                        "         3. If NO default key, hard nested is still possible"
                    )
                    print("            but requires a known key from another source")
                    print("         4. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CY, 'SLOW - minutes to hours per key'))}"
                    )
            else:
                # PRNG detection failed or multiple tags
                print(f"       Card  : MIFARE Classic {card_size}")
                print("       Plan  :")
                print(
                    f"         1. Try default keys: {color_string((CY, f'hf mf fchk {size_flag}'))}"
                )
                print("         2. Place single tag and reconnect for PRNG detection")

        # --- MIFARE Ultralight / NTAG ---
        elif sak == 0x00:
            print("       Card  : MIFARE Ultralight / NTAG")
            print("       Plan  :")
            print(
                f"         1. Read pages:  {color_string((CY, 'hf mfu rdpg --start 0 --end 44'))}"
            )
            print("         2. Check if password-protected (NTAG21x):")
            print("            Try bruteforce or known default passwords")
            print(f"         3. Dump:  {color_string((CY, 'hf mfu dump'))}")
            print("       Note  : UL-C uses 3DES auth, EV1/NTAG use PWD_AUTH")

        # --- DESFire / MIFARE Plus ---
        elif sak == 0x20:
            ats_hex = tag["ats"].hex().upper() if len(tag["ats"]) > 0 else "N/A"
            print(f"       Card  : DESFire / MIFARE Plus (SAK=0x20, ATS={ats_hex})")
            print(
                f"       Vuln  : {color_string((CR, 'STRONG - AES/3DES crypto, no known passive attacks'))}"
            )
            print("       Plan  :")
            print("         1. Check ATS to distinguish DESFire vs MIFARE Plus")
            print("         2. DESFire: Try default app keys (all zeros)")
            print("         3. MIFARE Plus SL1: May still be Classic-compatible")
            print(f"            {color_string((CY, 'hf mf fchk --1k'))}")
            print("       Note  : Full DESFire attacks require app-level access")

        # --- Unknown SAK ---
        else:
            print(f"       Card  : Unknown type (SAK=0x{sak:02X})")
            print("       Plan  :")
            print(f"         1. Research SAK=0x{sak:02X} for this card type")
            print(f"         2. Try: {color_string((CY, 'hf 14a raw'))}")
            print("         3. Check proprietary command sets")


@hw.command("disconnect")
class HWDisconnect(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Disconnect chameleon"
        return parser

    def on_exec(self, args: argparse.Namespace):
        self.device_com.close()


@hw.command("mode")
class HWMode(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get or change device mode: tag reader or tag emulator"
        mode_group = parser.add_mutually_exclusive_group()
        mode_group.add_argument(
            "-r", "--reader", action="store_true", help="Set reader mode"
        )
        mode_group.add_argument(
            "-e", "--emulator", action="store_true", help="Set emulator mode"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.reader:
            self.cmd.set_device_reader_mode(True)
            print("Switch to {  Tag Reader  } mode successfully.")
        elif args.emulator:
            self.cmd.set_device_reader_mode(False)
            print("Switch to { Tag Emulator } mode successfully.")
        else:
            print(
                f"- Device Mode ( Tag {'Reader' if self.cmd.is_device_reader_mode() else 'Emulator'} )"
            )


@hw.command("chipid")
class HWChipId(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get device chipset ID"
        return parser

    def on_exec(self, args: argparse.Namespace):
        print(" - Device chip ID: " + self.cmd.get_device_chip_id())


@hw.command("address")
class HWAddress(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get device address (used with Bluetooth)"
        return parser

    def on_exec(self, args: argparse.Namespace):
        print(" - Device address: " + self.cmd.get_device_address())


@hw.command("version")
class HWVersion(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get current device firmware version"
        return parser

    def on_exec(self, args: argparse.Namespace):
        fw_version_tuple = self.cmd.get_app_version()
        fw_version = f"v{fw_version_tuple[0]}.{fw_version_tuple[1]}"
        git_version = self.cmd.get_git_version()
        model = ["Ultra", "Lite"][self.cmd.get_device_model()]
        print(f" - Chameleon {model}, Version: {fw_version} ({git_version})")


@hf_14a.command("scan")
class HF14AScan(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Scan 14a tag, and print basic information"
        return parser

    def check_mf1_nt(self):
        # detect mf1 support
        if self.cmd.mf1_detect_support():
            # detect prng
            print("- Mifare Classic technology")
            prng_type = self.cmd.mf1_detect_prng()
            print(f"  # Prng: {MifareClassicPrngType(prng_type)}")

    def sak_info(self, data_tag):
        # detect the technology in use based on SAK
        int_sak = data_tag["sak"][0]
        if int_sak in type_id_SAK_dict:
            print(f"- Guessed type(s) from SAK: {type_id_SAK_dict[int_sak]}")

    def scan(self, deep=False):
        resp = self.cmd.hf14a_scan()
        if resp is not None:
            for data_tag in resp:
                print(f"- UID  : {data_tag['uid'].hex().upper()}")
                print(
                    f"- ATQA : {data_tag['atqa'].hex().upper()} "
                    f"(0x{int.from_bytes(data_tag['atqa'], byteorder='little'):04x})"
                )
                print(f"- SAK  : {data_tag['sak'].hex().upper()}")
                if len(data_tag["ats"]) > 0:
                    print(f"- ATS  : {data_tag['ats'].hex().upper()}")
                if deep:
                    self.sak_info(data_tag)
                    # TODO: following checks cannot be done yet if multiple cards are present
                    if len(resp) == 1:
                        self.check_mf1_nt()
                        # TODO: check for ATS support on 14A3 tags
                    else:
                        print("Multiple tags detected, skipping deep tests...")
        else:
            print("ISO14443-A Tag no found")

    def on_exec(self, args: argparse.Namespace):
        self.scan()


@hf_14a.command("info")
class HF14AInfo(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Scan 14a tag, and print detail information"
        return parser

    def on_exec(self, args: argparse.Namespace):
        scan = HF14AScan()
        scan.device_com = self.device_com
        scan.scan(deep=True)


@hf_mf.command("nested")
class HFMFNested(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Mifare Classic nested recover key"
        parser.add_argument(
            "--blk",
            "--known-block",
            type=int,
            required=True,
            metavar="<dec>",
            help="Known key block number",
        )
        srctype_group = parser.add_mutually_exclusive_group()
        srctype_group.add_argument(
            "-a", "-A", action="store_true", help="Known key is A key (default)"
        )
        srctype_group.add_argument(
            "-b", "-B", action="store_true", help="Known key is B key"
        )
        parser.add_argument(
            "-k", "--key", type=str, required=True, metavar="<hex>", help="Known key"
        )
        # tblk required because only single block mode is supported for now
        parser.add_argument(
            "--tblk",
            "--target-block",
            type=int,
            required=True,
            metavar="<dec>",
            help="Target key block number",
        )
        dsttype_group = parser.add_mutually_exclusive_group()
        dsttype_group.add_argument(
            "--ta", "--tA", action="store_true", help="Target A key (default)"
        )
        dsttype_group.add_argument(
            "--tb", "--tB", action="store_true", help="Target B key"
        )
        return parser

    def from_nt_level_code_to_str(self, nt_level):
        if nt_level == 0:
            return "StaticNested"
        if nt_level == 1:
            return "Nested"
        if nt_level == 2:
            return "HardNested"

    def recover_a_key(
        self, block_known, type_known, key_known, block_target, type_target
    ) -> Union[str, None]:
        """
            recover a key from key known.

        :param block_known:
        :param type_known:
        :param key_known:
        :param block_target:
        :param type_target:
        :return:
        """
        # check nt level, we can run static or nested auto...
        nt_level = self.cmd.mf1_detect_prng()
        print(
            f" - NT vulnerable: {color_string((CY, self.from_nt_level_code_to_str(nt_level)))}"
        )
        if nt_level == 2:
            print(" [!] Use hf mf hardnested")
            return None

        # acquire
        if nt_level == 0:  # It's a staticnested tag?
            nt_uid_obj = self.cmd.mf1_static_nested_acquire(
                block_known, type_known, key_known, block_target, type_target
            )
            cmd_param = f"{nt_uid_obj['uid']} {int(type_target)}"
            for nt_item in nt_uid_obj["nts"]:
                cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']}"
            tool_name = "staticnested"
        else:
            dist_obj = self.cmd.mf1_detect_nt_dist(block_known, type_known, key_known)
            nt_obj = self.cmd.mf1_nested_acquire(
                block_known, type_known, key_known, block_target, type_target
            )
            # create cmd
            cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
            for nt_item in nt_obj:
                cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']} {nt_item['par']}"
            tool_name = "nested"

        # Cross-platform compatibility
        if sys.platform == "win32":
            cmd_recover = f"{tool_name}.exe {cmd_param}"
        else:
            cmd_recover = f"./{tool_name} {cmd_param}"

        print(f"   Executing {cmd_recover}")
        # start a decrypt process
        process = self.sub_process(cmd_recover)

        # wait end
        while process.is_running():
            msg = f"   [ Time elapsed {process.get_time_distance() / 1000:#.1f}s ]\r"
            print(msg, end="")
            time.sleep(0.1)
        # clear \r
        print()

        if process.get_ret_code() == 0:
            output_str = process.get_output_sync()
            key_list = []
            for line in output_str.split("\n"):
                sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                if sea_obj is not None:
                    key_list.append(sea_obj[1])
            # Here you have to verify the password first, and then get the one that is successfully verified
            # If there is no verified password, it means that the recovery failed, you can try again
            print(f" - [{len(key_list)} candidate key(s) found ]")
            for key in key_list:
                key_bytes = bytearray.fromhex(key)
                if self.cmd.mf1_auth_one_key_block(
                    block_target, type_target, key_bytes
                ):
                    return key
        else:
            # No keys recover, and no errors.
            return None

    def on_exec(self, args: argparse.Namespace):
        block_known = args.blk
        # default to A
        type_known = MfcKeyType.B if args.b else MfcKeyType.A
        key_known: str = args.key
        if not re.match(r"^[a-fA-F0-9]{12}$", key_known):
            print("key must include 12 HEX symbols")
            return
        key_known_bytes = bytes.fromhex(key_known)
        block_target = args.tblk
        # default to A
        type_target = MfcKeyType.B if args.tb else MfcKeyType.A
        if block_known == block_target and type_known == type_target:
            print(color_string((CR, "Target key already known")))
            return
        print(" - Nested recover one key running...")
        key = self.recover_a_key(
            block_known, type_known, key_known_bytes, block_target, type_target
        )
        if key is None:
            print(color_string((CY, "No key found, you can retry.")))
        else:
            print(
                f" - Block {block_target} Type {type_target.name} Key Found: {color_string((CG, key))}"
            )
        return


@hf_mf.command("darkside")
class HFMFDarkside(ReaderRequiredUnit):
    def __init__(self):
        super().__init__()
        self.darkside_list = []

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Mifare Classic darkside recover key"
        return parser

    def recover_key(self, block_target, type_target):
        """
            Execute darkside acquisition and decryption.

        :param block_target:
        :param type_target:
        :return:
        """
        first_recover = True
        retry_count = 0
        while retry_count < 0xFF:
            darkside_resp = self.cmd.mf1_darkside_acquire(
                block_target, type_target, first_recover, 30
            )
            first_recover = False  # not first run.
            if darkside_resp[0] != MifareClassicDarksideStatus.OK:
                print(
                    f"Darkside error: {MifareClassicDarksideStatus(darkside_resp[0])}"
                )
                break
            darkside_obj = darkside_resp[1]

            if darkside_obj["par"] != 0:  # NXP tag workaround.
                self.darkside_list.clear()

            self.darkside_list.append(darkside_obj)
            recover_params = f"{darkside_obj['uid']}"
            for darkside_item in self.darkside_list:
                recover_params += f" {darkside_item['nt1']} {darkside_item['ks1']} {darkside_item['par']}"
                recover_params += f" {darkside_item['nr']} {darkside_item['ar']}"
            if sys.platform == "win32":
                cmd_recover = f"darkside.exe {recover_params}"
            else:
                cmd_recover = f"./darkside {recover_params}"
            # subprocess.run(cmd_recover, cwd=os.path.abspath("../bin/"), shell=True)
            # print(f"   Executing {cmd_recover}")
            # start a decrypt process
            process = self.sub_process(cmd_recover)
            # wait end
            process.wait_process()
            # get output
            output_str = process.get_output_sync()
            if "key not found" in output_str:
                print(f" - No key found, retrying({retry_count})...")
                retry_count += 1
                continue  # retry
            else:
                key_list = []
                for line in output_str.split("\n"):
                    sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                    if sea_obj is not None:
                        key_list.append(sea_obj[1])
                # auth key
                for key in key_list:
                    key_bytes = bytearray.fromhex(key)
                    if self.cmd.mf1_auth_one_key_block(
                        block_target, type_target, key_bytes
                    ):
                        return key
        return None

    def on_exec(self, args: argparse.Namespace):
        key = self.recover_key(0x03, MfcKeyType.A)
        if key is not None:
            print(f" - Key Found: {key}")
        else:
            print(" - Key recover fail.")
        return


@hf_mf.command("hardnested")
class HFMFHardNested(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Mifare Classic hardnested recover key "
        parser.add_argument(
            "--blk",
            "--known-block",
            type=int,
            required=True,
            metavar="<dec>",
            help="Known key block number",
        )
        srctype_group = parser.add_mutually_exclusive_group()
        srctype_group.add_argument(
            "-a", "-A", action="store_true", help="Known key is A key (default)"
        )
        srctype_group.add_argument(
            "-b", "-B", action="store_true", help="Known key is B key"
        )
        parser.add_argument(
            "-k", "--key", type=str, required=True, metavar="<hex>", help="Known key"
        )
        parser.add_argument(
            "--tblk",
            "--target-block",
            type=int,
            required=True,
            metavar="<dec>",
            help="Target key block number",
        )
        dsttype_group = parser.add_mutually_exclusive_group()
        dsttype_group.add_argument(
            "--ta", "--tA", action="store_true", help="Target A key (default)"
        )
        dsttype_group.add_argument(
            "--tb", "--tB", action="store_true", help="Target B key"
        )
        parser.add_argument(
            "--slow",
            action="store_true",
            help="Use slower acquisition mode (more nonces)",
        )
        parser.add_argument(
            "--keep-nonce-file",
            action="store_true",
            help="Keep the generated nonce file (nonces.bin)",
        )
        parser.add_argument(
            "--max-runs",
            type=int,
            default=200,
            metavar="<dec>",
            help="Maximum acquisition runs per attempt before giving up (default: 200)",
        )
        # Add max acquisition attempts
        parser.add_argument(
            "--max-attempts",
            type=int,
            default=3,
            metavar="<dec>",
            help="Maximum acquisition attempts if MSB sum is invalid (default: 3)",
        )
        return parser

    def recover_key(
        self,
        slow_mode,
        block_known,
        type_known,
        key_known,
        block_target,
        type_target,
        keep_nonce_file,
        max_runs,
        max_attempts,
    ):
        """
        Recover a key using the HardNested attack via a nonce file, with dynamic MSB-based acquisition and restart on invalid sum.

        :param slow_mode: Boolean indicating if slow mode should be used.
        :param block_known: Known key block number.
        :param type_known: Known key type (A or B).
        :param key_known: Known key bytes.
        :param block_target: Target key block number.
        :param type_target: Target key type (A or B).
        :param keep_nonce_file: Boolean indicating whether to keep the nonce file.
        :param max_runs: Maximum number of acquisition runs per attempt.
        :param max_attempts: Maximum number of full acquisition attempts.
        :return: Recovered key as a hex string, or None if not found.
        """
        print(" - Starting HardNested attack...")
        nonces_buffer = bytearray()  # This will hold the final data for the file
        uid_bytes = b""  # To store UID from the successful attempt

        # --- Outer loop for acquisition attempts ---
        acquisition_success = False  # Flag to indicate if any attempt was successful
        for attempt in range(max_attempts):
            print(
                f"\n--- Starting Acquisition Attempt {attempt + 1}/{max_attempts} ---"
            )
            total_raw_nonces_bytes = (
                bytearray()
            )  # Accumulator for raw nonces for THIS attempt
            nonces_buffer.clear()  # Clear buffer for each new attempt

            # --- MSB Tracking Initialization (Reset for each attempt) ---
            seen_msbs = [False] * 256
            unique_msb_count = 0
            msb_parity_sum = 0
            # --- End MSB Tracking Initialization ---

            run_count = 0
            acquisition_goal_met = False

            # 1. Scan for the tag to get UID and prepare file header (Done ONCE per attempt)
            print("   Scanning for tag...")
            try:
                scan_resp = self.cmd.hf14a_scan()
            except Exception as e:
                print(color_string((CR, f"   Error scanning tag: {e}")))
                # Decide if we should retry or fail completely. Let's fail for now.
                print(
                    color_string((CR, "   Attack failed due to error during scanning."))
                )
                return None

            if scan_resp is None or len(scan_resp) == 0:
                print(color_string((CR, "Error: No tag found.")))
                if attempt + 1 < max_attempts:
                    print(color_string((CY, "   Retrying scan in 1 second...")))
                    time.sleep(1)
                    continue  # Retry the outer loop (next attempt)
                else:
                    print(
                        color_string(
                            (
                                CR,
                                "   Maximum attempts reached without finding tag. Attack failed.",
                            )
                        )
                    )
                    return None
            if len(scan_resp) > 1:
                print(
                    color_string(
                        (
                            CR,
                            "   Error: Multiple tags found. Please present only one tag.",
                        )
                    )
                )
                # Fail immediately if multiple tags are present
                return None

            tag_info = scan_resp[0]
            uid_bytes = tag_info["uid"]  # Store UID for later verification
            uid_len = len(uid_bytes)
            uid_for_file = b""
            if uid_len == 4:
                uid_for_file = uid_bytes[0:4]
            elif uid_len == 7:
                uid_for_file = uid_bytes[3:7]
            elif uid_len == 10:
                uid_for_file = uid_bytes[6:10]
            else:
                print(
                    color_string(
                        (
                            CR,
                            f"   Error: Unexpected UID length ({uid_len} bytes). Cannot create nonce file header.",
                        )
                    )
                )
                return None  # Fail if UID length is unexpected
            print(f"   Tag found with UID: {uid_bytes.hex().upper()}")
            # Prepare header in the main buffer for this attempt
            nonces_buffer.extend(uid_for_file)
            nonces_buffer.extend(
                struct.pack("!BB", block_target, type_target.value & 0x01)
            )
            print(f"   Nonce file header prepared: {nonces_buffer.hex().upper()}")

            # 2. Acquire nonces dynamically based on MSB criteria (Inner loop for runs)
            print(
                f"   Acquiring nonces (slow mode: {slow_mode}, max runs: {max_runs}). This may take a while..."
            )
            while run_count < max_runs:
                run_count += 1
                print(f"   Starting acquisition run {run_count}/{max_runs}...")
                try:
                    # Check if tag is still present before each run
                    current_scan = self.cmd.hf14a_scan()
                    if (
                        current_scan is None
                        or len(current_scan) == 0
                        or current_scan[0]["uid"] != uid_bytes
                    ):
                        print(
                            color_string(
                                (
                                    CY,
                                    f"   Error: Tag lost or changed before run {run_count}. Stopping acquisition attempt.",
                                )
                            )
                        )
                        acquisition_goal_met = False  # Mark as failed
                        break  # Exit inner run loop for this attempt

                    # Acquire nonces for this run
                    raw_nonces_bytes_this_run = self.cmd.mf1_hard_nested_acquire(
                        slow_mode,
                        block_known,
                        type_known,
                        key_known,
                        block_target,
                        type_target,
                    )

                    if not raw_nonces_bytes_this_run:
                        print(
                            color_string(
                                (
                                    CY,
                                    f"   Run {run_count}: No nonces acquired in this run. Continuing...",
                                )
                            )
                        )
                        time.sleep(0.1)  # Small delay before retrying
                        continue

                    # Append successfully acquired nonces to the total buffer for this attempt
                    total_raw_nonces_bytes.extend(raw_nonces_bytes_this_run)

                    # --- Process acquired nonces for MSB tracking ---
                    num_pairs_this_run = len(raw_nonces_bytes_this_run) // 9
                    print(
                        f"   Run {run_count}: Acquired {num_pairs_this_run * 2} nonces ({len(raw_nonces_bytes_this_run)} bytes raw). Processing MSBs..."
                    )

                    new_msbs_found_this_run = 0
                    for i in range(num_pairs_this_run):
                        offset = i * 9
                        try:
                            nt, nt_enc, par = struct.unpack_from(
                                "!IIB", raw_nonces_bytes_this_run, offset
                            )
                        except struct.error as unpack_err:
                            print(
                                color_string(
                                    (
                                        CR,
                                        f"   Error unpacking nonce data at offset {offset}: {unpack_err}. Skipping pair.",
                                    )
                                )
                            )
                            continue

                        msb = (nt_enc >> 24) & 0xFF

                        if not seen_msbs[msb]:
                            seen_msbs[msb] = True
                            unique_msb_count += 1
                            new_msbs_found_this_run += 1
                            parity_bit = hardnested_utils.evenparity32(
                                (nt_enc & 0xFF000000) | (par & 0x08)
                            )
                            msb_parity_sum += parity_bit
                            print(
                                f"\r   Unique MSBs: {unique_msb_count}/256 | Current Sum: {msb_parity_sum}   ",
                                end="",
                            )

                    if new_msbs_found_this_run > 0:
                        print()  # Print a newline after progress update

                    # --- Check termination condition ---
                    if unique_msb_count == 256:
                        print()
                        print(
                            f"{color_string((CG, '   All 256 unique MSBs found.'))} Final parity sum: {msb_parity_sum}"
                        )
                        if msb_parity_sum in hardnested_utils.hardnested_sums:
                            print(
                                color_string(
                                    (
                                        CG,
                                        f"   Parity sum {msb_parity_sum} is VALID. Stopping acquisition runs.",
                                    )
                                )
                            )
                            acquisition_goal_met = True
                            acquisition_success = True  # Mark attempt as successful
                            break  # Exit the inner run loop successfully
                        else:
                            print(
                                color_string(
                                    (
                                        CR,
                                        f"   Parity sum {msb_parity_sum} is INVALID (Expected one of {hardnested_utils.hardnested_sums}).",
                                    )
                                )
                            )
                            acquisition_goal_met = False  # Mark as failed
                            acquisition_success = False
                            break  # Exit the inner run loop to restart the attempt

                except chameleon_com.CMDInvalidException:
                    print(
                        color_string(
                            (
                                CR,
                                "   Error: Hardnested command not supported by this firmware version.",
                            )
                        )
                    )
                    return None  # Cannot proceed at all
                except UnexpectedResponseError as e:
                    print(
                        color_string(
                            (
                                CR,
                                f"   Error acquiring nonces during run {run_count}: {e}",
                            )
                        )
                    )
                    print(
                        color_string(
                            (CY, "   Stopping acquisition runs for this attempt...")
                        )
                    )
                    acquisition_goal_met = False
                    break  # Exit inner run loop
                except TimeoutError:
                    print(
                        color_string(
                            (
                                CR,
                                f"   Error: Timeout during nonce acquisition run {run_count}.",
                            )
                        )
                    )
                    print(
                        color_string(
                            (CY, "   Stopping acquisition runs for this attempt...")
                        )
                    )
                    acquisition_goal_met = False
                    break  # Exit inner run loop
                except Exception as e:
                    print(
                        color_string(
                            (
                                CR,
                                f"   Unexpected error during acquisition run {run_count}: {e}",
                            )
                        )
                    )
                    print(
                        color_string(
                            (CY, "   Stopping acquisition runs for this attempt...")
                        )
                    )
                    acquisition_goal_met = False
                    break  # Exit inner run loop
            # --- End of inner run loop (while run_count < max_runs) ---

            # --- Post-Acquisition Summary for this attempt ---
            print(f"\n   Finished acquisition phase for attempt {attempt + 1}.")
            if acquisition_success:
                print(
                    color_string(
                        (
                            CG,
                            f"   Successfully acquired nonces meeting the MSB sum criteria in {run_count} runs.",
                        )
                    )
                )
                # Append collected raw nonces to the main buffer for the file
                nonces_buffer.extend(total_raw_nonces_bytes)
                break  # Exit the outer attempt loop successfully
            elif unique_msb_count == 256 and not acquisition_goal_met:
                print(
                    color_string(
                        (CR, "   Found all 256 MSBs, but the parity sum was invalid.")
                    )
                )
                if attempt + 1 < max_attempts:
                    print(color_string((CY, "   Restarting acquisition process...")))
                    time.sleep(1)  # Small delay before restarting
                    continue  # Continue to the next iteration of the outer attempt loop
                else:
                    print(
                        color_string(
                            (
                                CR,
                                f"   Maximum attempts ({max_attempts}) reached with invalid sum. Attack failed.",
                            )
                        )
                    )
                    return None  # Failed after max attempts
            elif run_count >= max_runs:
                print(
                    color_string(
                        (
                            CY,
                            f"   Warning: Reached max runs ({max_runs}) for attempt {attempt + 1}. Found {unique_msb_count}/256 unique MSBs.",
                        )
                    )
                )
                if attempt + 1 < max_attempts:
                    print(color_string((CY, "   Restarting acquisition process...")))
                    time.sleep(1)
                    continue  # Continue to the next iteration of the outer attempt loop
                else:
                    print(
                        color_string(
                            (
                                CR,
                                f"   Maximum attempts ({max_attempts}) reached without meeting criteria. Attack failed.",
                            )
                        )
                    )
                    return None  # Failed after max attempts
            else:  # Acquisition stopped due to error or tag loss
                print(
                    color_string(
                        (
                            CR,
                            f"Acquisition attempt {attempt + 1} stopped prematurely due to an error after {run_count} runs.",
                        )
                    )
                )
                # Decide if we should retry or fail completely. Let's fail for now.
                print(
                    color_string((CR, "Attack failed due to error during acquisition."))
                )
                return None  # Failed due to error

        # --- End of outer attempt loop ---

        # If we exited the loop successfully (acquisition_success is True)
        if not acquisition_success:
            # This case should ideally be caught within the loop, but as a safeguard:
            print(
                color_string(
                    (CR, f"   Error: Acquisition failed after {max_attempts} attempts.")
                )
            )
            return None

        # --- Proceed with the rest of the attack using the successfully collected nonces ---
        total_nonce_pairs = (
            len(total_raw_nonces_bytes) // 9
        )  # Use data from the successful attempt
        print(
            f"\n   Proceeding with attack using {total_nonce_pairs * 2} nonces ({len(total_raw_nonces_bytes)} bytes raw)."
        )
        print(f"   Total nonce file size will be {len(nonces_buffer)} bytes.")

        if total_nonce_pairs == 0:
            print(
                color_string(
                    (
                        CR,
                        "   Error: No nonces were successfully acquired in the final attempt.",
                    )
                )
            )
            return None

        # 3. Save nonces to a temporary file
        nonce_file_path = None
        temp_nonce_file = None
        output_str = ""  # To store the output read from the file

        try:
            # --- Nonce File Handling ---
            delete_nonce_on_close = not keep_nonce_file
            # Use delete_on_close=False to manage deletion manually in finally block
            temp_nonce_file = tempfile.NamedTemporaryFile(
                suffix=".bin",
                prefix="hardnested_nonces_",
                delete=False,
                mode="wb",
                dir=".",
            )
            temp_nonce_file.write(
                nonces_buffer
            )  # Write the buffer from the successful attempt
            temp_nonce_file.flush()
            nonce_file_path = temp_nonce_file.name
            temp_nonce_file.close()  # Close it so hardnested can access it
            temp_nonce_file = None  # Clear variable after closing
            print(
                f"   Nonces saved to {'temporary ' if delete_nonce_on_close else ''}file: {os.path.abspath(nonce_file_path)}"
            )

            # 4. Prepare and run the external hardnested tool, redirecting output
            print(
                color_string(
                    (CC, "--- Running Hardnested Tool (Output redirected) ---")
                )
            )

            output_str = execute_tool("hardnested", [os.path.abspath(nonce_file_path)])

            print(color_string((CC, "--- Hardnested Tool Finished ---")))

            # 5. Read the output from the temporary log file
            # 6. Process the result (using output_str read from the file)
            key_list = []
            key_prefix = "Key found: "  # Define the specific prefix to look for
            for line in output_str.splitlines():
                line_stripped = line.strip()  # Remove leading/trailing whitespace
                if line_stripped.startswith(key_prefix):
                    # Found the target line, now extract the key using regex
                    # Regex now looks for 12 hex chars specifically after the prefix
                    sea_obj = re.search(
                        r"([a-fA-F0-9]{12})", line_stripped[len(key_prefix) :]
                    )
                    if sea_obj:
                        key_list.append(sea_obj.group(1))
                        # Optional: Break if you only expect one "Key found:" line
                        # break

            if not key_list:
                print(
                    color_string(
                        (
                            CY,
                            f"   No line starting with '{key_prefix}' found in the output file.",
                        )
                    )
                )
                return None

            # 7. Verify Keys (Same as before)
            print(
                f"   [{len(key_list)} candidate key(s) found in output. Verifying...]"
            )
            # Use the UID from the successful acquisition attempt
            uid_bytes_for_verify = (
                uid_bytes  # From the last successful scan in the outer loop
            )

            for key_hex in key_list:
                key_bytes = bytes.fromhex(key_hex)
                print(f"   Trying key: {key_hex.upper()}...", end="")
                try:
                    # Check tag presence before auth attempt
                    scan_check = self.cmd.hf14a_scan()
                    if (
                        scan_check is None
                        or len(scan_check) == 0
                        or scan_check[0]["uid"] != uid_bytes_for_verify
                    ):
                        print(
                            color_string(
                                (
                                    CR,
                                    " Tag lost or changed during verification. Cannot verify.",
                                )
                            )
                        )
                        return None  # Stop verification if tag is gone

                    if self.cmd.mf1_auth_one_key_block(
                        block_target, type_target, key_bytes
                    ):
                        print(color_string((CG, " Success!")))
                        return key_hex  # Return the verified key
                    else:
                        print(color_string((CR, "Auth failed.")))
                except UnexpectedResponseError as e:
                    print(color_string((CR, f" Verification error: {e}")))
                    # Consider if we should continue trying other keys or stop
                except Exception as e:
                    print(
                        color_string(
                            (CR, f" Unexpected error during verification: {e}")
                        )
                    )
                    # Consider stopping here

            print(color_string((CY, "   Verification failed for all candidate keys.")))
            return None

        finally:
            # 8. Clean up nonce file
            if nonce_file_path and os.path.exists(nonce_file_path):
                if keep_nonce_file:
                    final_nonce_filename = "nonces.bin"
                    try:
                        if os.path.exists(final_nonce_filename):
                            os.remove(final_nonce_filename)
                        # Use replace for atomicity if possible
                        os.replace(nonce_file_path, final_nonce_filename)
                        print(
                            f"   Nonce file kept as: {os.path.abspath(final_nonce_filename)}"
                        )
                    except OSError as e:
                        print(
                            color_string(
                                (
                                    CR,
                                    f"   Error renaming/replacing temporary nonce file to {final_nonce_filename}: {e}",
                                )
                            )
                        )
                        print(f"   Temporary file might remain: {nonce_file_path}")
                else:
                    try:
                        os.remove(nonce_file_path)
                        # print(f"   Temporary nonce file deleted: {nonce_file_path}") # Optional confirmation
                    except OSError as e:
                        print(
                            color_string(
                                (
                                    CR,
                                    f"   Error deleting temporary nonce file {nonce_file_path}: {e}",
                                )
                            )
                        )

    def on_exec(self, args: argparse.Namespace):
        block_known = args.blk
        type_known = MfcKeyType.B if args.b else MfcKeyType.A
        key_known_str: str = args.key
        if not re.match(r"^[a-fA-F0-9]{12}$", key_known_str):
            raise ArgsParserError("Known key must include 12 HEX symbols")
        key_known_bytes = bytes.fromhex(key_known_str)

        block_target = args.tblk
        type_target = MfcKeyType.B if args.tb else MfcKeyType.A

        if block_known == block_target and type_known == type_target:
            print(color_string((CR, "Target key is the same as the known key.")))
            return

        # Pass the max_runs and max_attempts arguments
        recovered_key = self.recover_key(
            args.slow,
            block_known,
            type_known,
            key_known_bytes,
            block_target,
            type_target,
            args.keep_nonce_file,
            args.max_runs,
            args.max_attempts,
        )

        if recovered_key:
            print(
                f" - Key Found: Block {block_target} Type {type_target.name} Key = {color_string((CG, recovered_key.upper()))}"
            )
        else:
            print(color_string((CR, " - HardNested attack failed to recover the key.")))


@hf_mf.command("senested")
class HFMFStaticEncryptedNested(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Mifare Classic static encrypted recover key via backdoor"
        parser.add_argument(
            "--key",
            "-k",
            help="Backdoor key (as hex[12] format), currently known: A396EFA4E24F (default), A31667A8CEC1, 518B3354E760. See https://eprint.iacr.org/2024/1275",
            metavar="<hex>",
            type=str,
        )
        parser.add_argument(
            "--sectors", "-s", type=int, metavar="<dec>", help="Sector count"
        )
        parser.add_argument(
            "--starting-sector",
            type=int,
            metavar="<dec>",
            help="Start recovery from this sector",
        )
        parser.set_defaults(sectors=16)
        parser.set_defaults(starting_sector=0)
        parser.set_defaults(key="A396EFA4E24F")
        return parser

    def on_exec(self, args: argparse.Namespace):
        acquire_datas = self.cmd.mf1_static_encrypted_nested_acquire(
            bytes.fromhex(args.key), args.sectors, args.starting_sector
        )

        if not acquire_datas:
            print("Failed to collect nonces, is card present and has backdoor?")

        uid = format(acquire_datas["uid"], "x")

        key_map = {"A": {}, "B": {}}

        check_speed = 1.95  # sec per 64 keys

        for sector in range(args.starting_sector, args.sectors):
            sector_name = str(sector).zfill(2)
            print("Recovering", sector, "sector...")
            execute_tool(
                "staticnested_1nt",
                [
                    uid,
                    sector_name,
                    format(acquire_datas["nts"]["a"][sector]["nt"], "x").zfill(8),
                    format(acquire_datas["nts"]["a"][sector]["nt_enc"], "x").zfill(8),
                    str(acquire_datas["nts"]["a"][sector]["parity"]).zfill(4),
                ],
            )
            execute_tool(
                "staticnested_1nt",
                [
                    uid,
                    sector_name,
                    format(acquire_datas["nts"]["b"][sector]["nt"], "x").zfill(8),
                    format(acquire_datas["nts"]["b"][sector]["nt_enc"], "x").zfill(8),
                    str(acquire_datas["nts"]["b"][sector]["parity"]).zfill(4),
                ],
            )
            a_key_dic = f"keys_{uid}_{sector_name}_{format(acquire_datas['nts']['a'][sector]['nt'], 'x').zfill(8)}.dic"
            b_key_dic = f"keys_{uid}_{sector_name}_{format(acquire_datas['nts']['b'][sector]['nt'], 'x').zfill(8)}.dic"
            execute_tool("staticnested_2x1nt_rf08s", [a_key_dic, b_key_dic])

            keys = open(
                os.path.join(
                    tempfile.gettempdir(), b_key_dic.replace(".dic", "_filtered.dic")
                )
            ).readlines()
            keys_bytes = []
            for key in keys:
                keys_bytes.append(bytes.fromhex(key.strip()))

            key = None

            print(
                "Start checking possible B keys, will take up to",
                math.floor(len(keys_bytes) / 64 * check_speed),
                "seconds for",
                len(keys_bytes),
                "keys",
            )
            for i in tqdm_if_exists(range(0, len(keys_bytes), 64)):
                data = self.cmd.mf1_check_keys_on_block(
                    sector * 4 + 3, 0x61, keys_bytes[i : i + 64]
                )
                if data:
                    key = data.hex().zfill(12)
                    key_map["B"][sector] = key
                    print("Found B key", key)
                    break

            if key:
                a_key = execute_tool(
                    "staticnested_2x1nt_rf08s_1key",
                    [
                        format(acquire_datas["nts"]["b"][sector]["nt"], "x").zfill(8),
                        key,
                        a_key_dic,
                    ],
                )
                keys_bytes = []
                for key in a_key.split("\n"):
                    keys_bytes.append(bytes.fromhex(key.strip()))
                data = self.cmd.mf1_check_keys_on_block(
                    sector * 4 + 3, 0x60, keys_bytes
                )
                if data:
                    key = data.hex().zfill(12)
                    print("Found A key", key)
                    key_map["A"][sector] = key
                    continue
                else:
                    print(
                        "Failed to find A key by fast method, trying all possible keys"
                    )
                    keys = open(
                        os.path.join(
                            tempfile.gettempdir(),
                            a_key_dic.replace(".dic", "_filtered.dic"),
                        )
                    ).readlines()
                    keys_bytes = []
                    for key in keys:
                        keys_bytes.append(bytes.fromhex(key.strip()))

                    print(
                        "Start checking possible A keys, will take up to",
                        math.floor(len(keys_bytes) / 64 * check_speed),
                        "seconds for",
                        len(keys_bytes),
                        "keys",
                    )
                    for i in tqdm_if_exists(range(0, len(keys_bytes), 64)):
                        data = self.cmd.mf1_check_keys_on_block(
                            sector * 4 + 3, 0x60, keys_bytes[i : i + 64]
                        )
                        if data:
                            key = data.hex().zfill(12)
                            print("Found A key", key)
                            key_map["A"][sector] = key
                            break
            else:
                print("Failed to find key")

        for file in glob.glob(tempfile.gettempdir() + "/keys_*.dic"):
            os.remove(file)

        print_key_table(key_map)


@hf_mf.command("fchk")
class HFMFFCHK(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Mifare Classic fast key check on sectors"

        mifare_type_group = parser.add_mutually_exclusive_group()
        mifare_type_group.add_argument(
            "--mini",
            help="MIFARE Classic Mini / S20",
            action="store_const",
            dest="maxSectors",
            const=5,
        )
        mifare_type_group.add_argument(
            "--1k",
            help="MIFARE Classic 1k / S50 (default)",
            action="store_const",
            dest="maxSectors",
            const=16,
        )
        mifare_type_group.add_argument(
            "--2k",
            help="MIFARE Classic/Plus 2k",
            action="store_const",
            dest="maxSectors",
            const=32,
        )
        mifare_type_group.add_argument(
            "--4k",
            help="MIFARE Classic 4k / S70",
            action="store_const",
            dest="maxSectors",
            const=40,
        )

        parser.add_argument(
            dest="keys",
            help="Key (as hex[12] format)",
            metavar="<hex>",
            type=str,
            nargs="*",
        )
        parser.add_argument(
            "--key",
            dest="import_key",
            type=argparse.FileType("rb"),
            help="Read keys from .key format file",
        )
        parser.add_argument(
            "--dic",
            dest="import_dic",
            type=argparse.FileType("r", encoding="utf8"),
            help="Read keys from .dic format file",
        )

        parser.add_argument(
            "--export-key",
            type=argparse.FileType("wb"),
            help=f"Export result as .key format, file will be {color_string((CR, 'OVERWRITTEN'))} if exists",
        )
        parser.add_argument(
            "--export-dic",
            type=argparse.FileType("w", encoding="utf8"),
            help=f"Export result as .dic format, file will be {color_string((CR, 'OVERWRITTEN'))} if exists",
        )

        parser.add_argument(
            "-m",
            "--mask",
            help="Which sectorKey to be skip, 1 bit per sectorKey. `0b1` represent to skip to check. (in hex[20] format)",
            type=str,
            default="00000000000000000000",
            metavar="<hex>",
        )

        parser.set_defaults(maxSectors=16)
        return parser

    def check_keys(self, mask: bytearray, keys: list[bytes], chunkSize=20):
        sectorKeys = dict()

        for i in range(0, len(keys), chunkSize):
            # print("mask = {}".format(mask.hex(sep=' ', bytes_per_sep=1)))
            chunkKeys = keys[i : i + chunkSize]
            print(
                f" - progress of checking keys... {color_string((CY, i))} / {len(keys)} ({color_string((CY, f'{100 * i / len(keys):.1f}'))} %)"
            )
            resp = self.cmd.mf1_check_keys_of_sectors(mask, chunkKeys)
            # print(resp)

            if resp["status"] != Status.HF_TAG_OK:
                print(
                    f" - check interrupted, reason: {color_string((CR, Status(resp['status'])))}"
                )
                break
            elif "sectorKeys" not in resp:
                print(
                    f" - check interrupted, reason: {color_string((CG, 'All sectorKey is found or masked'))}"
                )
                break

            for j in range(10):
                mask[j] |= resp["found"][j]
            sectorKeys.update(resp["sectorKeys"])

        return sectorKeys

    def on_exec(self, args: argparse.Namespace):
        # print(args)

        keys = set()

        # keys from args
        for key in args.keys:
            if not re.match(r"^[a-fA-F0-9]{12}$", key):
                print(
                    f' - {color_string((CR, "Key should in hex[12] format, invalid key is ignored"))}, key = "{key}"'
                )
                continue
            keys.add(bytes.fromhex(key))

        # read keys from key format file
        if args.import_key is not None:
            if not load_key_file(args.import_key, keys):
                return

        if args.import_dic is not None:
            if not load_dic_file(args.import_dic, keys):
                return

        if len(keys) == 0:
            print(f" - {color_string((CR, 'No keys'))}")
            return

        print(f" - loaded {color_string((CG, len(keys)))} keys")

        # mask
        if not re.match(r"^[a-fA-F0-9]{1,20}$", args.mask):
            print(
                f' - {color_string((CR, "mask should in hex[20] format"))}, mask = "{args.mask}"'
            )
            return
        mask = bytearray.fromhex(f"{args.mask:0<20}")
        for i in range(args.maxSectors, 40):
            mask[i // 4] |= 3 << (6 - i % 4 * 2)

        # check keys
        startedAt = datetime.now()
        sectorKeys = self.check_keys(mask, list(keys))
        endedAt = datetime.now()
        duration = endedAt - startedAt
        print(
            f" - elapsed time: {color_string((CY, f'{duration.total_seconds():.3f}s'))}"
        )

        if args.export_key is not None:
            unknownkey = bytes(6)
            for sectorNo in range(args.maxSectors):
                args.export_key.write(sectorKeys.get(2 * sectorNo, unknownkey))
                args.export_key.write(sectorKeys.get(2 * sectorNo + 1, unknownkey))
            print(
                f" - result exported to: {color_string((CG, args.export_key.name))} (as .key format)"
            )

        if args.export_dic is not None:
            uniq_result = set(sectorKeys.values())
            for key in uniq_result:
                args.export_dic.write(key.hex().upper() + "\n")
            print(
                f" - result exported to: {color_string((CG, args.export_dic.name))} (as .dic format)"
            )

        # print sectorKeys
        print(f"\n - {color_string((CG, 'result of key checking:'))}\n")
        print("-----+-----+--------------+---+--------------+----")
        print(" Sec | Blk | key A        |res| key B        |res ")
        print("-----+-----+--------------+---+--------------+----")
        for sectorNo in range(args.maxSectors):
            blk = (sectorNo * 4 + 3) if sectorNo < 32 else (sectorNo * 16 - 369)
            keyA = sectorKeys.get(2 * sectorNo, None)
            if keyA:
                keyA = f"{color_string((CG, keyA.hex().upper()))} | {color_string((CG, '1'))}"
            else:
                keyA = (
                    f"{color_string((CR, '------------'))} | {color_string((CR, '0'))}"
                )
            keyB = sectorKeys.get(2 * sectorNo + 1, None)
            if keyB:
                keyB = f"{color_string((CG, keyB.hex().upper()))} | {color_string((CG, '1'))}"
            else:
                keyB = (
                    f"{color_string((CR, '------------'))} | {color_string((CR, '0'))}"
                )
            print(
                f" {color_string((CY, f'{sectorNo:03d}'))} | {blk:03d} | {keyA} | {keyB} "
            )
        print("-----+-----+--------------+---+--------------+----")
        print(
            f"( {color_string((CR, '0'))}: Failed, {color_string((CG, '1'))}: Success )\n\n"
        )


@hf_mf.command("rdbl")
class HFMFRDBL(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "Mifare Classic read one block"
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        resp = self.cmd.mf1_read_one_block(param.block, param.type, param.key)
        print(f" - Data: {resp.hex()}")


@hf_mf.command("wrbl")
class HFMFWRBL(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "Mifare Classic write one block"
        parser.add_argument(
            "-d",
            "--data",
            type=str,
            required=True,
            metavar="<hex>",
            help="Your block data, as hex string.",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        if not re.match(r"^[a-fA-F0-9]{32}$", args.data):
            raise ArgsParserError("Data must include 32 HEX symbols")
        data = bytearray.fromhex(args.data)
        resp = self.cmd.mf1_write_one_block(param.block, param.type, param.key, data)
        if resp:
            print(f" - {color_string((CG, 'Write done.'))}")
        else:
            print(f" - {color_string((CR, 'Write fail.'))}")


@hf_mf.command("view")
class HFMFView(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Display content from tag memory or dump file"
        mifare_type_group = parser.add_mutually_exclusive_group()
        mifare_type_group.add_argument(
            "--mini",
            help="MIFARE Classic Mini / S20",
            action="store_const",
            dest="maxSectors",
            const=5,
        )
        mifare_type_group.add_argument(
            "--1k",
            help="MIFARE Classic 1k / S50 (default)",
            action="store_const",
            dest="maxSectors",
            const=16,
        )
        mifare_type_group.add_argument(
            "--2k",
            help="MIFARE Classic/Plus 2k",
            action="store_const",
            dest="maxSectors",
            const=32,
        )
        mifare_type_group.add_argument(
            "--4k",
            help="MIFARE Classic 4k / S70",
            action="store_const",
            dest="maxSectors",
            const=40,
        )
        parser.add_argument(
            "-d",
            "--dump-file",
            required=False,
            type=argparse.FileType("rb"),
            help="Dump file to read",
        )
        parser.add_argument(
            "-k",
            "--key-file",
            required=False,
            type=argparse.FileType("r"),
            help="File containing keys of tag to write (exported with fchk --export)",
        )
        parser.set_defaults(maxSectors=16)
        return parser

    def on_exec(self, args: argparse.Namespace):
        data = bytearray(0)
        if args.dump_file is not None:
            print("Reading dump file")
            data = args.dump_file.read()
        elif args.key_file is not None:
            print("Reading tag memory")
            # read keys from file
            keys = list()
            for line in args.key_file.readlines():
                a, b = [bytes.fromhex(h) for h in line[:-1].split(":")]
                keys.append((a, b))
            if len(keys) != args.maxSectors:
                raise ArgsParserError(
                    f"Invalid key file. Found {len(keys)}, expected {args.maxSectors}"
                )
            # iterate over blocks
            for blk in range(0, args.maxSectors * 4):
                resp = None
                try:
                    # first try with key B
                    resp = self.cmd.mf1_read_one_block(
                        blk, MfcKeyType.B, keys[blk // 4][1]
                    )
                except UnexpectedResponseError:
                    # ignore read errors at this stage as we want to try key A
                    pass
                if not resp:
                    # try with key A if B was unsuccessful
                    # this will raise an exception if key A fails too
                    resp = self.cmd.mf1_read_one_block(
                        blk, MfcKeyType.A, keys[blk // 4][0]
                    )
                data.extend(resp)
        else:
            raise ArgsParserError(
                "Missing args. Specify --dump-file (-d) or --key-file (-k)"
            )
        print_mem_dump(data, 16)


@hf_mf.command("value")
class HFMFVALUE(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "MIFARE Classic value block commands"

        operator_group = parser.add_mutually_exclusive_group()
        operator_group.add_argument(
            "--get", action="store_true", help="get value from src block"
        )
        operator_group.add_argument(
            "--set",
            type=int,
            required=False,
            metavar="<dec>",
            help="set value X (-2147483647 ~ 2147483647) to src block",
        )
        operator_group.add_argument(
            "--inc",
            type=int,
            required=False,
            metavar="<dec>",
            help="increment value by X (0 ~ 2147483647) from src to dst",
        )
        operator_group.add_argument(
            "--dec",
            type=int,
            required=False,
            metavar="<dec>",
            help="decrement value by X (0 ~ 2147483647) from src to dst",
        )
        operator_group.add_argument(
            "--res",
            "--cp",
            action="store_true",
            help="copy value from src to dst (Restore and Transfer)",
        )

        parser.add_argument(
            "--blk",
            "--src-block",
            type=int,
            required=True,
            metavar="<dec>",
            help="block number of src",
        )
        srctype_group = parser.add_mutually_exclusive_group()
        srctype_group.add_argument(
            "-a", "-A", action="store_true", help="key of src is A key (default)"
        )
        srctype_group.add_argument(
            "-b", "-B", action="store_true", help="key of src is B key"
        )
        parser.add_argument(
            "-k",
            "--src-key",
            type=str,
            required=True,
            metavar="<hex>",
            help="key of src",
        )

        parser.add_argument(
            "--tblk",
            "--dst-block",
            type=int,
            metavar="<dec>",
            help="block number of dst (default to src)",
        )
        dsttype_group = parser.add_mutually_exclusive_group()
        dsttype_group.add_argument(
            "--ta",
            "--tA",
            action="store_true",
            help="key of dst is A key (default to src)",
        )
        dsttype_group.add_argument(
            "--tb",
            "--tB",
            action="store_true",
            help="key of dst is B key (default to src)",
        )
        parser.add_argument(
            "--tkey",
            "--dst-key",
            type=str,
            metavar="<hex>",
            help="key of dst (default to src)",
        )

        return parser

    def on_exec(self, args: argparse.Namespace):
        # print(args)
        # src
        src_blk = args.blk
        src_type = MfcKeyType.B if args.b is not False else MfcKeyType.A
        src_key = args.src_key
        if not re.match(r"^[a-fA-F0-9]{12}$", src_key):
            print("src_key must include 12 HEX symbols")
            return
        src_key = bytearray.fromhex(src_key)
        # print(src_blk, src_type, src_key)

        if args.get is not False:
            self.get_value(src_blk, src_type, src_key)
            return
        elif args.set is not None:
            self.set_value(src_blk, src_type, src_key, args.set)
            return

        # dst
        dst_blk = args.tblk if args.tblk is not None else src_blk
        dst_type = (
            MfcKeyType.A
            if args.ta is not False
            else (MfcKeyType.B if args.tb is not False else src_type)
        )
        dst_key = args.tkey if args.tkey is not None else args.src_key
        if not re.match(r"^[a-fA-F0-9]{12}$", dst_key):
            print("dst_key must include 12 HEX symbols")
            return
        dst_key = bytearray.fromhex(dst_key)
        # print(dst_blk, dst_type, dst_key)

        if args.inc is not None:
            self.inc_value(
                src_blk, src_type, src_key, args.inc, dst_blk, dst_type, dst_key
            )
            return
        elif args.dec is not None:
            self.dec_value(
                src_blk, src_type, src_key, args.dec, dst_blk, dst_type, dst_key
            )
            return
        elif args.res is not False:
            self.res_value(src_blk, src_type, src_key, dst_blk, dst_type, dst_key)
            return
        else:
            raise ArgsParserError("Please specify a value command")

    def get_value(self, block, type, key):
        resp = self.cmd.mf1_read_one_block(block, type, key)
        val1, val2, val3, adr1, adr2, adr3, adr4 = struct.unpack("<iiiBBBB", resp)
        # print(f"{val1}, {val2}, {val3}, {adr1}, {adr2}, {adr3}, {adr4}")
        if (val1 != val3) or (val1 + val2 != -1):
            print(
                f" - {color_string((CR, f'Invalid value of value block: {resp.hex()}'))}"
            )
            return
        if (adr1 != adr3) or (adr2 != adr4) or (adr1 + adr2 != 0xFF):
            print(
                f" - {color_string((CR, f'Invalid address of value block: {resp.hex()}'))}"
            )
            return
        print(
            f" - block[{block}] = {color_string((CG, f'{{ value: {val1}, adr: {adr1} }}'))}"
        )

    def set_value(self, block, type, key, value):
        if value < -2147483647 or value > 2147483647:
            raise ArgsParserError(
                f"Set value must be between -2147483647 and 2147483647. Got {value}"
            )
        adr_inverted = 0xFF - block
        data = struct.pack(
            "<iiiBBBB",
            value,
            -value - 1,
            value,
            block,
            adr_inverted,
            block,
            adr_inverted,
        )
        resp = self.cmd.mf1_write_one_block(block, type, key, data)
        if resp:
            print(f" - {color_string((CG, 'Set done.'))}")
            self.get_value(block, type, key)
        else:
            print(f" - {color_string((CR, 'Set fail.'))}")

    def inc_value(self, src_blk, src_type, src_key, value, dst_blk, dst_type, dst_key):
        if value < 0 or value > 2147483647:
            raise ArgsParserError(
                f"Increment value must be between 0 and 2147483647. Got {value}"
            )
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk,
            src_type,
            src_key,
            MfcValueBlockOperator.INCREMENT,
            value,
            dst_blk,
            dst_type,
            dst_key,
        )
        if resp:
            print(f" - {color_string((CG, 'Increment done.'))}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {color_string((CR, 'Increment fail.'))}")

    def dec_value(self, src_blk, src_type, src_key, value, dst_blk, dst_type, dst_key):
        if value < 0 or value > 2147483647:
            raise ArgsParserError(
                f"Decrement value must be between 0 and 2147483647. Got {value}"
            )
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk,
            src_type,
            src_key,
            MfcValueBlockOperator.DECREMENT,
            value,
            dst_blk,
            dst_type,
            dst_key,
        )
        if resp:
            print(f" - {color_string((CG, 'Decrement done.'))}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {color_string((CR, 'Decrement fail.'))}")

    def res_value(self, src_blk, src_type, src_key, dst_blk, dst_type, dst_key):
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk,
            src_type,
            src_key,
            MfcValueBlockOperator.RESTORE,
            0,
            dst_blk,
            dst_type,
            dst_key,
        )
        if resp:
            print(f" - {color_string((CG, 'Restore done.'))}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {color_string((CR, 'Restore fail.'))}")


_KEY = re.compile("[a-fA-F0-9]{12}", flags=re.MULTILINE)


def _run_mfkey32v2(items):
    output_str = subprocess.run(
        [
            default_cwd / ("mfkey32v2.exe" if sys.platform == "win32" else "mfkey32v2"),
            items[0]["uid"],
            items[0]["nt"],
            items[0]["nr"],
            items[0]["ar"],
            items[1]["nt"],
            items[1]["nr"],
            items[1]["ar"],
        ],
        capture_output=True,
        check=True,
        encoding="ascii",
    ).stdout
    sea_obj = _KEY.search(output_str)
    if sea_obj is not None:
        return sea_obj[0], items
    return None


class ItemGenerator:
    def __init__(self, rs, uid_found_keys=set()):
        self.rs: list = rs
        self.progress = 0
        self.i = 0
        self.j = 1
        self.found = set()
        self.keys = set()
        for known_key in uid_found_keys:
            self.test_key(known_key)

    def __iter__(self):
        return self

    def __next__(self):
        size = len(self.rs)
        if self.j >= size:
            self.i += 1
            if self.i >= size - 1:
                raise StopIteration
            self.j = self.i + 1
        item_i, item_j = self.rs[self.i], self.rs[self.j]
        self.progress += 1
        self.j += 1
        if self.key_from_item(item_i) in self.found:
            self.progress += max(0, size - self.j)
            self.i += 1
            self.j = self.i + 1
            return next(self)
        if self.key_from_item(item_j) in self.found:
            return next(self)
        return item_i, item_j

    @staticmethod
    def key_from_item(item):
        return "{uid}-{nt}-{nr}-{ar}".format(**item)

    def test_key(self, key, items=list()):
        for item in self.rs:
            item_key = self.key_from_item(item)
            if item_key in self.found:
                continue
            if (item in items) or (
                Crypto1.mfkey32_is_reader_has_key(
                    int(item["uid"], 16),
                    int(item["nt"], 16),
                    int(item["nr"], 16),
                    int(item["ar"], 16),
                    key,
                )
            ):
                self.keys.add(key)
                self.found.add(item_key)


@hf_mf.command("elog")
class HFMFELog(DeviceRequiredUnit):
    detection_log_size = 18

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "MF1 Detection log count/decrypt"
        parser.add_argument(
            "--decrypt", action="store_true", help="Decrypt key from MF1 log list"
        )
        return parser

    def decrypt_by_list(self, rs: list, uid_found_keys: set = set()):
        """
            Decrypt key from reconnaissance log list

        :param rs:
        :return:
        """
        msg1 = f"  > {len(rs)} records => "
        msg2 = f"/{(len(rs) * (len(rs) - 1)) // 2} combinations. "
        msg3 = " key(s) found"
        gen = ItemGenerator(rs, uid_found_keys)
        print(f"{msg1}{gen.progress}{msg2}{len(gen.keys)}{msg3}\r", end="")
        with Pool(cpu_count()) as pool:
            for result in pool.imap(_run_mfkey32v2, gen):
                if result is not None:
                    gen.test_key(*result)
                print(f"{msg1}{gen.progress}{msg2}{len(gen.keys)}{msg3}\r", end="")
        print(f"{msg1}{gen.progress}{msg2}{len(gen.keys)}{msg3}")
        return gen.keys

    def on_exec(self, args: argparse.Namespace):
        if not args.decrypt:
            count = self.cmd.mf1_get_detection_count()
            print(f" - MF1 detection log count = {count}")
            return
        index = 0
        count = self.cmd.mf1_get_detection_count()
        if count == 0:
            print(" - No detection log to download")
            return
        print(f" - MF1 detection log count = {count}, start download", end="")
        result_list = []
        while index < count:
            tmp = self.cmd.mf1_get_detection_log(index)
            recv_count = len(tmp)
            index += recv_count
            result_list.extend(tmp)
            print("." * recv_count, end="")
        print()
        print(f" - Download done ({len(result_list)} records), start parse and decrypt")
        # classify
        result_maps = {}
        for item in result_list:
            uid = item["uid"]
            if uid not in result_maps:
                result_maps[uid] = {}
            block = item["block"]
            if block not in result_maps[uid]:
                result_maps[uid][block] = {}
            type = item["type"]
            if type not in result_maps[uid][block]:
                result_maps[uid][block][type] = []

            result_maps[uid][block][type].append(item)

        for uid in result_maps.keys():
            print(f" - Detection log for uid [{uid.upper()}]")
            result_maps_for_uid = result_maps[uid]
            uid_found_keys = set()
            for block in result_maps_for_uid:
                for keyType in "AB":
                    records = (
                        result_maps_for_uid[block][keyType]
                        if keyType in result_maps_for_uid[block]
                        else []
                    )
                    if len(records) < 1:
                        continue
                    print(f"  > Decrypting block {block} key {keyType} detect log...")
                    result_maps[uid][block][keyType] = self.decrypt_by_list(
                        records, uid_found_keys
                    )
                    uid_found_keys.update(result_maps[uid][block][keyType])

            print("  > Result ---------------------------")
            for block in result_maps_for_uid.keys():
                if "A" in result_maps_for_uid[block]:
                    print(
                        f"  > Block {block}, A key result: {result_maps_for_uid[block]['A']}"
                    )
                if "B" in result_maps_for_uid[block]:
                    print(
                        f"  > Block {block}, B key result: {result_maps_for_uid[block]['B']}"
                    )
        return


@hf_mf.command("eload")
class HFMFELoad(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Load data to emulator memory"
        self.add_slot_args(parser)
        parser.add_argument("-", "--file", type=str, required=True, help="file path")
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=False,
            help="content type",
            choices=["bin", "hex"],
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        file = args.file
        if args.type is None:
            if file.endswith(".bin"):
                content_type = "bin"
            elif file.endswith(".eml"):
                content_type = "hex"
            else:
                raise Exception(
                    "Unknown file format, Specify content type with -t option"
                )
        else:
            content_type = args.type
        buffer = bytearray()

        with open(file, mode="rb") as fd:
            if content_type == "bin":
                buffer.extend(fd.read())
            if content_type == "hex":
                buffer.extend(bytearray.fromhex(fd.read().decode()))

        if len(buffer) % 16 != 0:
            raise Exception("Data block not align for 16 bytes")
        if len(buffer) / 16 > 256:
            raise Exception("Data block memory overflow")

        index = 0
        block = 0
        max_blocks = (self.device_com.data_max_length - 1) // 16
        while index + 16 < len(buffer):
            # split a block from buffer
            block_data = buffer[index : index + 16 * max_blocks]
            n_blocks = len(block_data) // 16
            index += 16 * n_blocks
            # load to device
            self.cmd.mf1_write_emu_block_data(block, block_data)
            print("." * n_blocks, end="")
            block += n_blocks
        print("\n - Load success")


@hf_mf.command("esave")
class HFMFESave(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Read data from emulator memory"
        self.add_slot_args(parser)
        parser.add_argument("-", "--file", type=str, required=True, help="file path")
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=False,
            help="content type",
            choices=["bin", "hex"],
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        file = args.file
        if args.type is None:
            if file.endswith(".bin"):
                content_type = "bin"
            elif file.endswith(".eml"):
                content_type = "hex"
            else:
                raise Exception(
                    "Unknown file format, Specify content type with -t option"
                )
        else:
            content_type = args.type

        selected_slot = self.cmd.get_active_slot()
        slot_info = self.cmd.get_slot_info()
        tag_type = TagSpecificType(slot_info[selected_slot]["hf"])
        if tag_type == TagSpecificType.MIFARE_Mini:
            block_count = 20
        elif tag_type == TagSpecificType.MIFARE_1024:
            block_count = 64
        elif tag_type == TagSpecificType.MIFARE_2048:
            block_count = 128
        elif tag_type == TagSpecificType.MIFARE_4096:
            block_count = 256
        else:
            raise Exception(
                "Card in current slot is not Mifare Classic/Plus in SL1 mode"
            )

        index = 0
        data = bytearray(0)
        max_blocks = self.device_com.data_max_length // 16
        while block_count > 0:
            chunk_count = min(block_count, max_blocks)
            data.extend(self.cmd.mf1_read_emu_block_data(index, chunk_count))
            index += chunk_count
            block_count -= chunk_count
            print("." * chunk_count, end="")

        with open(file, "wb") as fd:
            if content_type == "hex":
                for i in range(len(data) // 16):
                    fd.write(binascii.hexlify(data[i * 16 : (i + 1) * 16]) + b"\n")
            else:
                fd.write(data)
        print("\n - Read success")


@hf_mf.command("eview")
class HFMFEView(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "View data from emulator memory"
        self.add_slot_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        selected_slot = self.cmd.get_active_slot()
        slot_info = self.cmd.get_slot_info()
        tag_type = TagSpecificType(slot_info[selected_slot]["hf"])

        if tag_type == TagSpecificType.MIFARE_Mini:
            block_count = 20
        elif tag_type == TagSpecificType.MIFARE_1024:
            block_count = 64
        elif tag_type == TagSpecificType.MIFARE_2048:
            block_count = 128
        elif tag_type == TagSpecificType.MIFARE_4096:
            block_count = 256
        else:
            raise Exception(
                "Card in current slot is not Mifare Classic/Plus in SL1 mode"
            )
        index = 0
        data = bytearray(0)
        max_blocks = self.device_com.data_max_length // 16
        while block_count > 0:
            # read all the blocks
            chunk_count = min(block_count, max_blocks)
            data.extend(self.cmd.mf1_read_emu_block_data(index, chunk_count))
            index += chunk_count
            block_count -= chunk_count
        print_mem_dump(data, 16)


@hf_mf.command("econfig")
class HFMFEConfig(SlotIndexArgsAndGoUnit, HF14AAntiCollArgsUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Settings of Mifare Classic emulator"
        self.add_slot_args(parser)
        self.add_hf14a_anticoll_args(parser)
        gen1a_group = parser.add_mutually_exclusive_group()
        gen1a_group.add_argument(
            "--enable-gen1a", action="store_true", help="Enable Gen1a magic mode"
        )
        gen1a_group.add_argument(
            "--disable-gen1a", action="store_true", help="Disable Gen1a magic mode"
        )
        gen2_group = parser.add_mutually_exclusive_group()
        gen2_group.add_argument(
            "--enable-gen2", action="store_true", help="Enable Gen2 magic mode"
        )
        gen2_group.add_argument(
            "--disable-gen2", action="store_true", help="Disable Gen2 magic mode"
        )
        block0_group = parser.add_mutually_exclusive_group()
        block0_group.add_argument(
            "--enable-block0",
            action="store_true",
            help="Use anti-collision data from block 0 for 4 byte UID tags",
        )
        block0_group.add_argument(
            "--disable-block0",
            action="store_true",
            help="Use anti-collision data from settings",
        )
        write_names = [w.name for w in MifareClassicWriteMode.list()]
        help_str = "Write Mode: " + ", ".join(write_names)
        parser.add_argument(
            "--write", type=str, help=help_str, metavar="MODE", choices=write_names
        )
        log_group = parser.add_mutually_exclusive_group()
        log_group.add_argument(
            "--enable-log",
            action="store_true",
            help="Enable logging of MFC authentication data",
        )
        log_group.add_argument(
            "--disable-log",
            action="store_true",
            help="Disable logging of MFC authentication data",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        # collect current settings
        anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
        if anti_coll_data is None or len(anti_coll_data) == 0:
            print(
                f"{color_string((CR, f'Slot {self.slot_num} does not contain any HF 14A config'))}"
            )
            return
        uid = anti_coll_data["uid"]
        atqa = anti_coll_data["atqa"]
        sak = anti_coll_data["sak"]
        ats = anti_coll_data["ats"]
        slotinfo = self.cmd.get_slot_info()
        fwslot = SlotNumber.to_fw(self.slot_num)
        hf_tag_type = TagSpecificType(slotinfo[fwslot]["hf"])
        if hf_tag_type not in [
            TagSpecificType.MIFARE_Mini,
            TagSpecificType.MIFARE_1024,
            TagSpecificType.MIFARE_2048,
            TagSpecificType.MIFARE_4096,
        ]:
            print(
                f"{color_string((CR, f'Slot {self.slot_num} not configured as MIFARE Classic'))}"
            )
            return
        mfc_config = self.cmd.mf1_get_emulator_config()
        gen1a_mode = mfc_config["gen1a_mode"]
        gen2_mode = mfc_config["gen2_mode"]
        block_anti_coll_mode = mfc_config["block_anti_coll_mode"]
        write_mode = MifareClassicWriteMode(mfc_config["write_mode"])
        detection = mfc_config["detection"]
        change_requested, change_done, uid, atqa, sak, ats = self.update_hf14a_anticoll(
            args, uid, atqa, sak, ats
        )
        if args.enable_gen1a:
            change_requested = True
            if not gen1a_mode:
                gen1a_mode = True
                self.cmd.mf1_set_gen1a_mode(gen1a_mode)
                change_done = True
            else:
                print(f"{color_string((CY, 'Requested gen1a already enabled'))}")
        elif args.disable_gen1a:
            change_requested = True
            if gen1a_mode:
                gen1a_mode = False
                self.cmd.mf1_set_gen1a_mode(gen1a_mode)
                change_done = True
            else:
                print(f"{color_string((CY, 'Requested gen1a already disabled'))}")
        if args.enable_gen2:
            change_requested = True
            if not gen2_mode:
                gen2_mode = True
                self.cmd.mf1_set_gen2_mode(gen2_mode)
                change_done = True
            else:
                print(f"{color_string((CY, 'Requested gen2 already enabled'))}")
        elif args.disable_gen2:
            change_requested = True
            if gen2_mode:
                gen2_mode = False
                self.cmd.mf1_set_gen2_mode(gen2_mode)
                change_done = True
            else:
                print(f"{color_string((CY, 'Requested gen2 already disabled'))}")
        if args.enable_block0:
            change_requested = True
            if not block_anti_coll_mode:
                block_anti_coll_mode = True
                self.cmd.mf1_set_block_anti_coll_mode(block_anti_coll_mode)
                change_done = True
            else:
                print(
                    f"{color_string((CY, 'Requested block0 anti-coll mode already enabled'))}"
                )
        elif args.disable_block0:
            change_requested = True
            if block_anti_coll_mode:
                block_anti_coll_mode = False
                self.cmd.mf1_set_block_anti_coll_mode(block_anti_coll_mode)
                change_done = True
            else:
                print(
                    f"{color_string((CY, 'Requested block0 anti-coll mode already disabled'))}"
                )
        if args.write is not None:
            change_requested = True
            new_write_mode = MifareClassicWriteMode[args.write]
            if new_write_mode != write_mode:
                write_mode = new_write_mode
                self.cmd.mf1_set_write_mode(write_mode)
                change_done = True
            else:
                print(f"{color_string((CY, 'Requested write mode already set'))}")
        if args.enable_log:
            change_requested = True
            if not detection:
                detection = True
                self.cmd.mf1_set_detection_enable(detection)
                change_done = True
            else:
                print(
                    f"{color_string((CY, 'Requested logging of MFC authentication data already enabled'))}"
                )
        elif args.disable_log:
            change_requested = True
            if detection:
                detection = False
                self.cmd.mf1_set_detection_enable(detection)
                change_done = True
            else:
                print(
                    f"{color_string((CY, 'Requested logging of MFC authentication data already disabled'))}"
                )

        if change_done:
            print(" - MF1 Emulator settings updated")
        if not change_requested:
            enabled_str = color_string((CG, "enabled"))
            disabled_str = color_string((CR, "disabled"))
            atqa_string = f"{atqa.hex().upper()} (0x{int.from_bytes(atqa, byteorder='little'):04x})"
            print(f"- {'Type:':40}{color_string((CY, hf_tag_type))}")
            print(f"- {'UID:':40}{color_string((CY, uid.hex().upper()))}")
            print(f"- {'ATQA:':40}{color_string((CY, atqa_string))}")
            print(f"- {'SAK:':40}{color_string((CY, sak.hex().upper()))}")
            if len(ats) > 0:
                print(f"- {'ATS:':40}{color_string((CY, ats.hex().upper()))}")
            print(
                f"- {'Gen1A magic mode:':40}{f'{enabled_str}' if gen1a_mode else f'{disabled_str}'}"
            )
            print(
                f"- {'Gen2 magic mode:':40}{f'{enabled_str}' if gen2_mode else f'{disabled_str}'}"
            )
            print(
                f"- {'Use anti-collision data from block 0:':40}"
                f"{f'{enabled_str}' if block_anti_coll_mode else f'{disabled_str}'}"
            )
            try:
                print(
                    f"- {'Write mode:':40}{color_string((CY, MifareClassicWriteMode(write_mode)))}"
                )
            except ValueError:
                print(f"- {'Write mode:':40}{color_string((CR, 'invalid value!'))}")
            print(
                f"- {'Log (mfkey32) mode:':40}{f'{enabled_str}' if detection else f'{disabled_str}'}"
            )


@hf_mfu.command("ercnt")
class HFMFUERCNT(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Read MIFARE Ultralight / NTAG counter value."
        parser.add_argument(
            "-c", "--counter", type=int, required=True, help="Counter index."
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        value, no_tearing = self.cmd.mfu_read_emu_counter_data(args.counter)
        print(f" - Value: {value:06x} ({value})")
        if no_tearing:
            print(f" - Tearing: {color_string((CG, 'not set'))}")
        else:
            print(f" - Tearing: {color_string((CR, 'set'))}")


@hf_mfu.command("ewcnt")
class HFMFUEWCNT(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Write MIFARE Ultralight / NTAG counter value."
        parser.add_argument(
            "-c", "--counter", type=int, required=True, help="Counter index."
        )
        parser.add_argument(
            "-v", "--value", type=int, required=True, help="Counter value (24-bit)."
        )
        parser.add_argument(
            "-t",
            "--reset-tearing",
            action="store_true",
            help="Reset tearing event flag.",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.value > 0xFFFFFF:
            print(color_string((CR, f"Counter value {args.value:#x} is too large.")))
            return

        self.cmd.mfu_write_emu_counter_data(
            args.counter, args.value, args.reset_tearing
        )

        print("- Ok")


@hf_mfu.command("rdpg")
class HFMFURDPG(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "MIFARE Ultralight / NTAG read one page"
        parser.add_argument(
            "-p",
            "--page",
            type=int,
            required=True,
            metavar="<dec>",
            help="The page where the key will be used against",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 0,
            "check_response_crc": 1,
        }

        if param.key is not None:
            options["keep_rf_field"] = 1
            try:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!B", 0x1B) + param.key,
                )

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception:
                # failed auth may cause tags to be lost
                failed_auth = True

            options["keep_rf_field"] = 0
            options["auto_select"] = 0
        else:
            failed_auth = False

        if not failed_auth:
            resp = self.cmd.hf14a_raw(
                options=options,
                resp_timeout_ms=200,
                data=struct.pack("!BB", 0x30, args.page),
            )
            print(f" - Data: {resp[:4].hex()}")
        else:
            try:
                self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!BB", 0x30, args.page),
                )
            except Exception:
                # we may lose the tag again here
                pass
            print(color_string((CR, " - Auth failed")))


@hf_mfu.command("wrpg")
class HFMFUWRPG(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "MIFARE Ultralight / NTAG write one page"
        parser.add_argument(
            "-p",
            "--page",
            type=int,
            required=True,
            metavar="<dec>",
            help="The index of the page to write to.",
        )
        parser.add_argument(
            "-d",
            "--data",
            type=bytes.fromhex,
            required=True,
            metavar="<hex>",
            help="Your page data, as a 4 byte (8 character) hex string.",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        data = args.data
        if len(data) != 4:
            print(
                color_string(
                    (CR, "Page data should be a 4 byte (8 character) hex string")
                )
            )
            return

        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 0,
            "check_response_crc": 0,
        }

        if param.key is not None:
            options["keep_rf_field"] = 1
            options["check_response_crc"] = 1
            try:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!B", 0x1B) + param.key,
                )

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception:
                # failed auth may cause tags to be lost
                failed_auth = True

            options["keep_rf_field"] = 0
            options["auto_select"] = 0
            options["check_response_crc"] = 0
        else:
            failed_auth = False

        if not failed_auth:
            resp = self.cmd.hf14a_raw(
                options=options,
                resp_timeout_ms=200,
                data=struct.pack("!BB", 0xA2, args.page) + data,
            )

            if resp[0] == 0x0A:
                print(" - Ok")
            else:
                print(color_string((CR, f"Write failed ({resp[0]:#04x}).")))
        else:
            # send a command just to disable the field. use read to avoid corrupting the data
            try:
                self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!BB", 0x30, args.page),
                )
            except Exception:
                # we may lose the tag again here
                pass
            print(color_string((CR, " - Auth failed")))


@hf_mfu.command("eview")
class HFMFUEVIEW(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "MIFARE Ultralight / NTAG view emulator data"
        return parser

    def on_exec(self, args: argparse.Namespace):
        nr_pages = self.cmd.mfu_get_emu_pages_count()
        page = 0
        while page < nr_pages:
            count = min(nr_pages - page, 16)
            data = self.cmd.mfu_read_emu_page_data(page, count)
            for i in range(0, len(data), 4):
                print(f"#{page + (i >> 2):02x}: {data[i : i + 4].hex()}")
            page += count


@hf_mfu.command("eload")
class HFMFUELOAD(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "MIFARE Ultralight / NTAG load emulator data"
        parser.add_argument(
            "-", "--file", required=True, type=str, help="File to load data from."
        )
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=False,
            help="Force writing as either raw binary or hex.",
            choices=["bin", "hex"],
        )
        return parser

    def get_param(self, args):
        class Param:
            def __init__(self):
                pass

        return Param()

    def on_exec(self, args: argparse.Namespace):
        file_type = args.type
        if file_type is None:
            if args.file.endswith(".eml") or args.file.endswith(".txt"):
                file_type = "hex"
            else:
                file_type = "bin"

        if file_type == "hex":
            with open(args.file, "r") as f:
                data = f.read()
            data = re.sub("#.*$", "", data, flags=re.MULTILINE)
            data = bytes.fromhex(data)
        else:
            with open(args.file, "rb") as f:
                data = f.read()

        # this will throw an exception on incorrect slot type
        nr_pages = self.cmd.mfu_get_emu_pages_count()
        size = nr_pages * 4
        if len(data) > size:
            print(
                color_string(
                    (
                        CR,
                        f"Dump file is too large for the current slot (expected {size} bytes).",
                    )
                )
            )
            return
        elif (len(data) % 4) > 0:
            print(
                color_string((CR, "Dump file's length is not a multiple of 4 bytes."))
            )
            return
        elif len(data) < size:
            print(
                color_string(
                    (
                        CY,
                        f"Dump file is smaller than the current slot's memory ({len(data)} < {size}).",
                    )
                )
            )

        nr_pages = len(data) >> 2
        page = 0
        while page < nr_pages:
            offset = page * 4
            cur_count = min(16, nr_pages - page)

            if offset >= len(data):
                page_data = bytes.fromhex("00000000") * cur_count
            else:
                page_data = data[offset : offset + 4 * cur_count]

            self.cmd.mfu_write_emu_page_data(page, page_data)
            page += cur_count

        print(" - Ok")


@hf_mfu.command("esave")
class HFMFUESAVE(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "MIFARE Ultralight / NTAG save emulator data"
        parser.add_argument(
            "-", "--file", required=True, type=str, help="File to save data to."
        )
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=False,
            help="Force writing as either raw binary or hex.",
            choices=["bin", "hex"],
        )
        return parser

    def get_param(self, args):
        class Param:
            def __init__(self):
                pass

        return Param()

    def on_exec(self, args: argparse.Namespace):
        file_type = args.type
        fd = None
        save_as_eml = False

        if file_type is None:
            if args.file.endswith(".eml") or args.file.endswith(".txt"):
                file_type = "hex"
            else:
                file_type = "bin"

        if file_type == "hex":
            fd = open(args.file, "w+")
            save_as_eml = True
        else:
            fd = open(args.file, "wb+")

        with fd:
            # this will throw an exception on incorrect slot type
            nr_pages = self.cmd.mfu_get_emu_pages_count()

            fd.truncate(0)

            # write version and signature as comments if saving as .eml
            if save_as_eml:
                try:
                    version = self.cmd.mf0_ntag_get_version_data()

                    fd.write(f"# Version: {version.hex()}\n")
                except Exception:
                    pass  # slot does not have version data

                try:
                    signature = self.cmd.mf0_ntag_get_signature_data()

                    if signature != b"\x00" * 32:
                        fd.write(f"# Signature: {signature.hex()}\n")
                except Exception:
                    pass  # slot does not have signature data

            page = 0
            while page < nr_pages:
                cur_count = min(32, nr_pages - page)

                data = self.cmd.mfu_read_emu_page_data(page, cur_count)
                if save_as_eml:
                    for i in range(0, len(data), 4):
                        fd.write(data[i : i + 4].hex() + "\n")
                else:
                    fd.write(data)

                page += cur_count

        print(" - Ok")


@hf_mfu.command("rcnt")
class HFMFURCNT(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "MIFARE Ultralight / NTAG read counter"
        parser.add_argument(
            "-c",
            "--counter",
            type=int,
            required=True,
            metavar="<dec>",
            help="Index of the counter to read (always 0 for NTAG, 0-2 for Ultralight EV1).",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 0,
            "check_response_crc": 1,
        }

        if param.key is not None:
            options["keep_rf_field"] = 1
            try:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!B", 0x1B) + param.key,
                )

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception:
                # failed auth may cause tags to be lost
                failed_auth = True

            options["keep_rf_field"] = 0
            options["auto_select"] = 0
        else:
            failed_auth = False

        if not failed_auth:
            resp = self.cmd.hf14a_raw(
                options=options,
                resp_timeout_ms=200,
                data=struct.pack("!BB", 0x39, args.counter),
            )
            print(f" - Data: {resp[:3].hex()}")
        else:
            try:
                self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!BB", 0x39, args.counter),
                )
            except Exception:
                # we may lose the tag again here
                pass
            print(color_string((CR, " - Auth failed")))


@hf_mfu.command("dump")
class HFMFUDUMP(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = "MIFARE Ultralight dump pages"
        parser.add_argument(
            "-p",
            "--page",
            type=int,
            required=False,
            metavar="<dec>",
            default=0,
            help="Manually set number of pages to dump",
        )
        parser.add_argument(
            "-q",
            "--qty",
            type=int,
            required=False,
            metavar="<dec>",
            help="Manually set number of pages to dump",
        )
        parser.add_argument(
            "-",
            "--file",
            type=str,
            required=False,
            default="",
            help="Specify a filename for dump file",
        )
        parser.add_argument(
            "-t",
            "--type",
            type=str,
            required=False,
            choices=["bin", "hex"],
            help="Force writing as either raw binary or hex.",
        )
        return parser

    def do_dump(self, args: argparse.Namespace, param, fd, save_as_eml):
        if args.qty is not None:
            stop_page = min(args.page + args.qty, 256)
        else:
            stop_page = None

        tags = self.cmd.hf14a_scan()
        if len(tags) > 1:
            print(f"- {color_string((CR, 'Collision detected, leave only one tag.'))}")
            return
        elif len(tags) == 0:
            print(f"- {color_string((CR, 'No tag detected.'))}")
            return
        elif tags[0]["atqa"] != b"\x44\x00" or tags[0]["sak"] != b"\x00":
            err = color_string(
                (
                    CR,
                    f"Tag is not Mifare Ultralight compatible (ATQA {tags[0]['atqa'].hex()} SAK {tags[0]['sak'].hex()}).",
                )
            )
            print(f"- {err}")
            return

        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 1,
            "check_response_crc": 1,
        }

        # if stop page isn't set manually, try autodetection
        if stop_page is None:
            tag_name = None

            # first try sending the GET_VERSION command
            try:
                version = self.cmd.hf14a_raw(
                    options=options, resp_timeout_ms=100, data=struct.pack("!B", 0x60)
                )
                if len(version) == 0:
                    version = None
            except Exception:
                version = None

            # try sending AUTHENTICATE command and observe the result
            try:
                supports_auth = (
                    len(
                        self.cmd.hf14a_raw(
                            options=options,
                            resp_timeout_ms=100,
                            data=struct.pack("!B", 0x1A),
                        )
                    )
                    != 0
                )
            except Exception:
                supports_auth = False

            if version is not None and not supports_auth:
                # either ULEV1 or NTAG
                assert len(version) == 8

                is_mikron_ulev1 = version[1] == 0x34 and version[2] == 0x21
                if (
                    (version[2] == 3 or is_mikron_ulev1)
                    and version[4] == 1
                    and version[5] == 0
                ):
                    # Ultralight EV1 V0
                    size_map = {
                        0x0B: ("Mifare Ultralight EV1 48b", 20),
                        0x0E: ("Mifare Ultralight EV1 128b", 41),
                    }
                elif version[2] == 4 and version[4] == 1 and version[5] == 0:
                    # NTAG 210/212/213/215/216 V0
                    size_map = {
                        0x0B: ("NTAG 210", 20),
                        0x0E: ("NTAG 212", 41),
                        0x0F: ("NTAG 213", 45),
                        0x11: ("NTAG 215", 135),
                        0x13: ("NTAG 216", 231),
                    }
                else:
                    size_map = {}

                if version[6] in size_map:
                    tag_name, stop_page = size_map[version[6]]
            elif version is None and supports_auth:
                # Ultralight C
                tag_name = "Mifare Ultralight C"
                stop_page = 48
            elif version is None and not supports_auth:
                try:
                    # Invalid command returning a NAK means that's some old type of NTAG.
                    self.cmd.hf14a_raw(
                        options=options,
                        resp_timeout_ms=100,
                        data=struct.pack("!B", 0xFF),
                    )

                    print(
                        color_string(
                            (CY, "Tag is likely NTAG 20x, reading until first error.")
                        )
                    )
                    stop_page = 256
                except Exception:
                    # Regular Ultralight
                    tag_name = "Mifare Ultralight"
                    stop_page = 16
            else:
                # This is probably Ultralight AES, but we don't support this one yet.
                pass

            if tag_name is not None:
                print(f" - Detected tag type as {tag_name}.")

            if stop_page is None:
                err_str = "Couldn't autodetect the expected card size, reading until first error."
                print(f"- {color_string((CY, err_str))}")
                stop_page = 256

        needs_stop = False

        if param.key is not None:
            try:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!B", 0x1B) + param.key,
                )

                needs_stop = len(resp) < 2
                if not needs_stop:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception:
                # failed auth may cause tags to be lost
                needs_stop = True

            options["auto_select"] = 0

        # this handles auth failure
        if needs_stop:
            print(color_string((CR, " - Auth failed")))
            if fd is not None:
                fd.close()
                fd = None

        for i in range(args.page, stop_page):
            # this could be done once in theory but the command would need to be optimized properly
            if param.key is not None and not needs_stop:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!B", 0x1B) + param.key,
                )
                options["auto_select"] = 0  # prevent resets

            # disable the rf field after the last command
            if i == (stop_page - 1) or needs_stop:
                options["keep_rf_field"] = 0

            try:
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=200,
                    data=struct.pack("!BB", 0x30, i),
                )
            except Exception:
                # probably lost tag, but we still need to disable rf field
                resp = None

            if needs_stop:
                # break if this command was sent just to disable RF field
                break
            elif resp is None or len(resp) == 0:
                # we need to disable RF field if we reached the last valid page so send one more read command
                needs_stop = True
                continue

            # after the read we are sure we no longer need to select again
            options["auto_select"] = 0

            # TODO: can be optimized as we get 4 pages at once but beware of wrapping
            # in case of end of memory or LOCK on ULC and no key provided
            data = resp[:4]
            print(f" - Page {i:2}: {data.hex()}")
            if fd is not None:
                if save_as_eml:
                    fd.write(data.hex() + "\n")
                else:
                    fd.write(data)

        if needs_stop and stop_page != 256:
            print(f"- {color_string((CY, 'Dump is shorter than expected.'))}")
        if args.file != "":
            print(f"- {color_string((CG, f'Dump written in {args.file}.'))}")

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        file_type = args.type
        fd = None
        save_as_eml = False

        if args.file != "":
            if file_type is None:
                if args.file.endswith(".eml") or args.file.endswith(".txt"):
                    file_type = "hex"
                else:
                    file_type = "bin"

            if file_type == "hex":
                fd = open(args.file, "w+")
                save_as_eml = True
            else:
                fd = open(args.file, "wb+")

        if fd is not None:
            with fd:
                fd.truncate(0)
                self.do_dump(args, param, fd, save_as_eml)
        else:
            self.do_dump(args, param, fd, save_as_eml)


@hf_mfu.command("version")
class HFMFUVERSION(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Request MIFARE Ultralight / NTAG version data."
        return parser

    def on_exec(self, args: argparse.Namespace):
        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 0,
            "check_response_crc": 1,
        }

        resp = self.cmd.hf14a_raw(
            options=options, resp_timeout_ms=200, data=struct.pack("!B", 0x60)
        )
        print(f" - Data: {resp[:8].hex()}")


@hf_mfu.command("signature")
class HFMFUSIGNATURE(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Request MIFARE Ultralight / NTAG ECC signature data."
        return parser

    def on_exec(self, args: argparse.Namespace):
        options = {
            "activate_rf_field": 0,
            "wait_response": 1,
            "append_crc": 1,
            "auto_select": 1,
            "keep_rf_field": 0,
            "check_response_crc": 1,
        }

        resp = self.cmd.hf14a_raw(
            options=options, resp_timeout_ms=200, data=struct.pack("!BB", 0x3C, 0x00)
        )
        print(f" - Data: {resp[:32].hex()}")


@hf_mfu.command("econfig")
class HFMFUEConfig(SlotIndexArgsAndGoUnit, HF14AAntiCollArgsUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Settings of Mifare Ultralight / NTAG emulator"
        self.add_slot_args(parser)
        self.add_hf14a_anticoll_args(parser)
        uid_magic_group = parser.add_mutually_exclusive_group()
        uid_magic_group.add_argument(
            "--enable-uid-magic", action="store_true", help="Enable UID magic mode"
        )
        uid_magic_group.add_argument(
            "--disable-uid-magic", action="store_true", help="Disable UID magic mode"
        )

        # Add this new write mode parameter
        write_names = [w.name for w in MifareUltralightWriteMode.list()]
        help_str = "Write Mode: " + ", ".join(write_names)
        parser.add_argument(
            "--write", type=str, help=help_str, metavar="MODE", choices=write_names
        )

        parser.add_argument(
            "--set-version",
            type=bytes.fromhex,
            help="Set data to be returned by the GET_VERSION command.",
        )
        parser.add_argument(
            "--set-signature",
            type=bytes.fromhex,
            help="Set data to be returned by the READ_SIG command.",
        )
        parser.add_argument(
            "--reset-auth-cnt",
            action="store_true",
            help="Resets the counter of unsuccessful authentication attempts.",
        )

        detection_group = parser.add_mutually_exclusive_group()
        detection_group.add_argument(
            "--enable-log",
            action="store_true",
            help="Enable password authentication logging",
        )
        detection_group.add_argument(
            "--disable-log",
            action="store_true",
            help="Disable password authentication logging",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        aux_data_changed = False
        aux_data_change_requested = False

        if args.set_version is not None:
            aux_data_change_requested = True
            aux_data_changed = True

            if len(args.set_version) != 8:
                print(color_string((CR, "Version data should be 8 bytes long.")))
                return

            try:
                self.cmd.mf0_ntag_set_version_data(args.set_version)
            except Exception:
                print(
                    color_string((CR, "Tag type does not support GET_VERSION command."))
                )
                return

        if args.set_signature is not None:
            aux_data_change_requested = True
            aux_data_changed = True

            if len(args.set_signature) != 32:
                print(color_string((CR, "Signature data should be 32 bytes long.")))
                return

            try:
                self.cmd.mf0_ntag_set_signature_data(args.set_signature)
            except Exception:
                print(color_string((CR, "Tag type does not support READ_SIG command.")))
                return

        if args.reset_auth_cnt:
            aux_data_change_requested = True
            old_value = self.cmd.mfu_reset_auth_cnt()
            if old_value != 0:
                aux_data_changed = True
                print(
                    f"- Unsuccessful auth counter has been reset from {old_value} to 0."
                )

        # collect current settings
        anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
        if len(anti_coll_data) == 0:
            print(
                color_string(
                    (CR, f"Slot {self.slot_num} does not contain any HF 14A config")
                )
            )
            return
        uid = anti_coll_data["uid"]
        atqa = anti_coll_data["atqa"]
        sak = anti_coll_data["sak"]
        ats = anti_coll_data["ats"]
        slotinfo = self.cmd.get_slot_info()
        fwslot = SlotNumber.to_fw(self.slot_num)
        hf_tag_type = TagSpecificType(slotinfo[fwslot]["hf"])
        if hf_tag_type not in [
            TagSpecificType.MF0ICU1,
            TagSpecificType.MF0ICU2,
            TagSpecificType.MF0UL11,
            TagSpecificType.MF0UL21,
            TagSpecificType.NTAG_210,
            TagSpecificType.NTAG_212,
            TagSpecificType.NTAG_213,
            TagSpecificType.NTAG_215,
            TagSpecificType.NTAG_216,
        ]:
            print(
                color_string(
                    (
                        CR,
                        f"Slot {self.slot_num} not configured as MIFARE Ultralight / NTAG",
                    )
                )
            )
            return
        change_requested, change_done, uid, atqa, sak, ats = self.update_hf14a_anticoll(
            args, uid, atqa, sak, ats
        )

        if args.enable_uid_magic:
            change_requested = True
            self.cmd.mf0_ntag_set_uid_magic_mode(True)
            magic_mode = True
        elif args.disable_uid_magic:
            change_requested = True
            self.cmd.mf0_ntag_set_uid_magic_mode(False)
            magic_mode = False
        else:
            magic_mode = self.cmd.mf0_ntag_get_uid_magic_mode()

        # Add this new write mode handling
        write_mode = None
        if args.write is not None:
            change_requested = True
            new_write_mode = MifareUltralightWriteMode[args.write]
            try:
                current_write_mode = self.cmd.mf0_ntag_get_write_mode()
                if new_write_mode != current_write_mode:
                    self.cmd.mf0_ntag_set_write_mode(new_write_mode)
                    change_done = True
                    write_mode = new_write_mode
                else:
                    print(color_string((CY, "Requested write mode already set")))
            except Exception:
                print(
                    color_string(
                        (
                            CR,
                            "Failed to set write mode. Check if device firmware supports this feature.",
                        )
                    )
                )

        detection = self.cmd.mf0_ntag_get_detection_enable()
        if args.enable_log:
            change_requested = True
            if detection is not None:
                if not detection:
                    detection = True
                    self.cmd.mf0_ntag_set_detection_enable(detection)
                    change_done = True
                else:
                    print(
                        color_string(
                            (
                                CY,
                                "Requested logging of MFU authentication data already enabled",
                            )
                        )
                    )
            else:
                print(
                    color_string(
                        (CR, "Detection functionality not available in this firmware")
                    )
                )
        elif args.disable_log:
            change_requested = True
            if detection is not None:
                if detection:
                    detection = False
                    self.cmd.mf0_ntag_set_detection_enable(detection)
                    change_done = True
                else:
                    print(
                        color_string(
                            (
                                CY,
                                "Requested logging of MFU authentication data already disabled",
                            )
                        )
                    )
            else:
                print(
                    color_string(
                        (CR, "Detection functionality not available in this firmware")
                    )
                )

        if change_done or aux_data_changed:
            print(" - MFU/NTAG Emulator settings updated")
        if not (change_requested or aux_data_change_requested):
            atqa_string = f"{atqa.hex().upper()} (0x{int.from_bytes(atqa, byteorder='little'):04x})"
            print(f"- {'Type:':40}{color_string((CY, hf_tag_type))}")
            print(f"- {'UID:':40}{color_string((CY, uid.hex().upper()))}")
            print(f"- {'ATQA:':40}{color_string((CY, atqa_string))}")
            print(f"- {'SAK:':40}{color_string((CY, sak.hex().upper()))}")
            if len(ats) > 0:
                print(f"- {'ATS:':40}{color_string((CY, ats.hex().upper()))}")

            # Display UID Magic status
            magic_status = "enabled" if magic_mode else "disabled"
            print(f"- {'UID Magic:':40}{color_string((CY, magic_status))}")

            # Add this to display write mode if available
            try:
                write_mode = MifareUltralightWriteMode(
                    self.cmd.mf0_ntag_get_write_mode()
                )
                print(f"- {'Write mode:':40}{color_string((CY, write_mode))}")
            except Exception:
                # Write mode not supported in current firmware
                pass

            # Existing version/signature display code
            try:
                version = self.cmd.mf0_ntag_get_version_data().hex().upper()
                print(f"- {'Version:':40}{color_string((CY, version))}")
            except Exception:
                pass

            try:
                signature = self.cmd.mf0_ntag_get_signature_data().hex().upper()
                print(f"- {'Signature:':40}{color_string((CY, signature))}")
            except Exception:
                pass

            try:
                detection = (
                    color_string((CG, "enabled"))
                    if self.cmd.mf0_ntag_get_detection_enable()
                    else color_string((CR, "disabled"))
                )
                print(f"- {'Log (password) mode:':40}{f'{detection}'}")
            except Exception:
                pass


@hf_mfu.command("edetect")
class HFMFUEDetect(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get Mifare Ultralight / NTAG emulator detection logs"
        self.add_slot_args(parser)
        parser.add_argument(
            "--count",
            type=int,
            help="Number of log entries to retrieve",
            metavar="COUNT",
        )
        parser.add_argument(
            "--index",
            type=int,
            default=0,
            help="Starting index (default: 0)",
            metavar="INDEX",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        detection_enabled = self.cmd.mf0_ntag_get_detection_enable()
        if not detection_enabled:
            print(color_string((CY, "Detection logging is disabled for this slot")))
            return

        total_count = self.cmd.mf0_ntag_get_detection_count()
        print(f"Total detection log entries: {total_count}")

        if total_count == 0:
            print(color_string((CY, "No detection logs available")))
            return

        if args.count is not None:
            entries_to_get = min(args.count, total_count - args.index)
        else:
            entries_to_get = total_count - args.index

        if entries_to_get <= 0:
            print(color_string((CY, f"No entries available from index {args.index}")))
            return

        logs = self.cmd.mf0_ntag_get_detection_log(args.index)

        print(
            f"\nPassword detection logs (showing {len(logs)} entries from index {args.index}):"
        )
        print("-" * 50)

        for i, log_entry in enumerate(logs):
            actual_index = args.index + i
            password = log_entry["password"]
            print(f"{actual_index:3d}: {color_string((CY, password.upper()))}")


@lf_em_410x.command("read")
class LFEMRead(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Scan em410x tag and print id"
        return parser

    def on_exec(self, args: argparse.Namespace):
        data = self.cmd.em410x_scan()
        print(color_string((TagSpecificType(data[0])), (CG, data[1].hex())))


@lf_em_410x.command("write")
class LFEM410xWriteT55xx(LFEMIdArgsUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Write em410x id to t55xx"
        return self.add_card_arg(parser, required=True)

    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytes.fromhex(id_hex)
        self.cmd.em410x_write_to_t55xx(id_bytes)
        print(f" - EM410x ID(10H): {id_hex} write done.")


@lf_hid_prox.command("read")
class LFHIDProxRead(LFHIDIdReadArgsUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Scan hid prox tag and print card format, facility code, card number, issue level and OEM code"
        return self.add_card_arg(parser, required=True)

    def on_exec(self, args: argparse.Namespace):
        format = 0
        if args.format is not None:
            format = HIDFormat[args.format].value
        (format, fc, cn1, cn2, il, oem) = self.cmd.hidprox_scan(format)
        cn = (cn1 << 32) + cn2
        print(f"HIDProx/{HIDFormat(format)}")
        if fc > 0:
            print(f" FC: {color_string((CG, fc))}")
        if il > 0:
            print(f" IL: {color_string((CG, il))}")
        if oem > 0:
            print(f" OEM: {color_string((CG, oem))}")
        print(f" CN: {color_string((CG, cn))}")


@lf_hid_prox.command("write")
class LFHIDProxWriteT55xx(LFHIDIdArgsUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Write hidprox card data to t55xx"
        return self.add_card_arg(parser, required=True)

    def on_exec(self, args: argparse.Namespace):
        if args.fc is None:
            args.fc = 0
        if args.il is None:
            args.il = 0
        if args.oem is None:
            args.oem = 0
        format = HIDFormat[args.format]
        id = struct.pack(
            ">BIBIBH",
            format.value,
            args.fc,
            (args.cn >> 32),
            args.cn & 0xFFFFFFFF,
            args.il,
            args.oem,
        )
        self.cmd.hidprox_write_to_t55xx(id)
        print(f"HIDProx/{format}")
        if args.fc > 0:
            print(f" FC: {args.fc}")
        if args.il > 0:
            print(f" IL: {args.il}")
        if args.oem > 0:
            print(f" OEM: {args.oem}")
        print(f" CN: {args.cn}")
        print("write done.")


@lf_hid_prox.command("econfig")
class LFHIDProxEconfig(SlotIndexArgsAndGoUnit, LFHIDIdArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulated hidprox card id"
        self.add_slot_args(parser)
        self.add_card_arg(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.cn is not None:
            if args.fc is None:
                args.fc = 0
            if args.il is None:
                args.il = 0
            if args.oem is None:
                args.oem = 0
            format = HIDFormat.H10301
            if args.format is not None:
                format = HIDFormat[args.format]
            id = struct.pack(
                ">BIBIBH",
                format.value,
                args.fc,
                (args.cn >> 32),
                args.cn & 0xFFFFFFFF,
                args.il,
                args.oem,
            )
            self.cmd.hidprox_set_emu_id(id)
            print(" - Set hidprox tag id success.")
        else:
            (format, fc, cn1, cn2, il, oem) = self.cmd.hidprox_get_emu_id()
            cn = (cn1 << 32) + cn2
            print(" - Get hidprox tag id success.")
            print(f" - HIDProx/{HIDFormat(format)}")
        if fc > 0:
            print(f"   FC: {color_string((CG, fc))}")
        if il > 0:
            print(f"   IL: {color_string((CG, il))}")
        if oem > 0:
            print(f"   OEM: {color_string((CG, oem))}")
        print(f"   CN: {color_string((CG, cn))}")


@lf_viking.command("read")
class LFVikingRead(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Scan Viking tag and print id"
        return parser

    def on_exec(self, args: argparse.Namespace):
        id = self.cmd.viking_scan()
        print(f" Viking: {color_string((CG, id.hex()))}")


@lf_viking.command("write")
class LFVikingWriteT55xx(LFVikingIdArgsUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Write Viking id to t55xx"
        return self.add_card_arg(parser, required=True)

    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytes.fromhex(id_hex)
        self.cmd.viking_write_to_t55xx(id_bytes)
        print(f" - Viking ID(8H): {id_hex} write done.")


@hw_slot.command("list")
class HWSlotList(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get information about slots"
        parser.add_argument(
            "--short",
            action="store_true",
            help="Hide slot nicknames and Mifare Classic emulator settings",
        )
        return parser

    def get_slot_name(self, slot, sense):
        try:
            name = self.cmd.get_slot_tag_nick(slot, sense)
            return {
                "baselen": len(name),
                "metalen": len(CC + C0),
                "name": color_string((CC, name)),
            }
        except UnexpectedResponseError:
            return {"baselen": 0, "metalen": 0, "name": ""}
        except UnicodeDecodeError:
            name = "UTF8 Err"
            return {
                "baselen": len(name),
                "metalen": len(CC + C0),
                "name": color_string((CC, name)),
            }

    def on_exec(self, args: argparse.Namespace):
        slotinfo = self.cmd.get_slot_info()
        selected = SlotNumber.from_fw(self.cmd.get_active_slot())
        current = selected
        enabled = self.cmd.get_enabled_slots()
        maxnamelength = 0

        slotnames = []
        all_nicks = self.cmd.get_all_slot_nicks()
        for slot_data in all_nicks:
            hfn = {
                "baselen": len(slot_data["hf"]),
                "metalen": len(CC + C0),
                "name": color_string((CC, slot_data["hf"])),
            }
            lfn = {
                "baselen": len(slot_data["lf"]),
                "metalen": len(CC + C0),
                "name": color_string((CC, slot_data["lf"])),
            }
            m = max(hfn["baselen"], lfn["baselen"])
            maxnamelength = m if m > maxnamelength else maxnamelength
            slotnames.append({"hf": hfn, "lf": lfn})

        for slot in SlotNumber:
            fwslot = SlotNumber.to_fw(slot)
            status = f"({color_string((CG, 'active'))})" if slot == selected else ""
            hf_tag_type = TagSpecificType(slotinfo[fwslot]["hf"])
            lf_tag_type = TagSpecificType(slotinfo[fwslot]["lf"])
            print(f" - {f'Slot {slot}:':{4 + maxnamelength + 1}} {status}")

            # HF
            field_length = maxnamelength + slotnames[fwslot]["hf"]["metalen"] + 1
            status = (
                f"({color_string((CR, 'disabled'))})"
                if not enabled[fwslot]["hf"]
                else ""
            )
            print(f"   HF: {slotnames[fwslot]['hf']['name']:{field_length}}", end="")
            print(status, end="")
            if hf_tag_type != TagSpecificType.UNDEFINED:
                color = CY if enabled[fwslot]["hf"] else C0
                print(color_string((color, hf_tag_type)))
            else:
                print("undef")
            if (
                (not args.short)
                and enabled[fwslot]["hf"]
                and hf_tag_type != TagSpecificType.UNDEFINED
            ):
                if current != slot:
                    self.cmd.set_active_slot(slot)
                    current = slot
                anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
                uid = anti_coll_data["uid"]
                atqa = anti_coll_data["atqa"]
                sak = anti_coll_data["sak"]
                ats = anti_coll_data["ats"]
                # print('    - ISO14443A emulator settings:')
                atqa_hex_le = f"(0x{int.from_bytes(atqa, byteorder='little'):04x})"
                print(f"      {'UID:':40}{color_string((CY, uid.hex().upper()))}")
                print(
                    f"      {'ATQA:':40}{color_string((CY, f'{atqa.hex().upper()} {atqa_hex_le}'))}"
                )
                print(f"      {'SAK:':40}{color_string((CY, sak.hex().upper()))}")
                if len(ats) > 0:
                    print(f"      {'ATS:':40}{color_string((CY, ats.hex().upper()))}")
                if hf_tag_type in [
                    TagSpecificType.MIFARE_Mini,
                    TagSpecificType.MIFARE_1024,
                    TagSpecificType.MIFARE_2048,
                    TagSpecificType.MIFARE_4096,
                ]:
                    config = self.cmd.mf1_get_emulator_config()
                    # print('    - Mifare Classic emulator settings:')
                    enabled_str = color_string((CG, "enabled"))
                    disabled_str = color_string((CR, "disabled"))
                    print(
                        f"      {'Gen1A magic mode:':40}"
                        f"{enabled_str if config['gen1a_mode'] else disabled_str}"
                    )
                    print(
                        f"      {'Gen2 magic mode:':40}"
                        f"{enabled_str if config['gen2_mode'] else disabled_str}"
                    )
                    print(
                        f"      {'Use anti-collision data from block 0:':40}"
                        f"{enabled_str if config['block_anti_coll_mode'] else disabled_str}"
                    )
                    try:
                        print(
                            f"      {'Write mode:':40}"
                            f"{color_string((CY, MifareClassicWriteMode(config['write_mode'])))}"
                        )
                    except ValueError:
                        print(
                            f"      {'Write mode:':40}{color_string((CR, 'invalid value!'))}"
                        )
                    print(
                        f"      {'Log (mfkey32) mode:':40}"
                        f"{enabled_str if config['detection'] else disabled_str}"
                    )

            # LF
            field_length = maxnamelength + slotnames[fwslot]["lf"]["metalen"] + 1
            status = (
                f"({color_string((CR, 'disabled'))})"
                if not enabled[fwslot]["lf"]
                else ""
            )
            print(f"   LF: {slotnames[fwslot]['lf']['name']:{field_length}}", end="")
            print(status, end="")
            if lf_tag_type != TagSpecificType.UNDEFINED:
                color = CY if enabled[fwslot]["lf"] else C0
                print(color_string((color, lf_tag_type)))
            else:
                print("undef")
            if (
                (not args.short)
                and enabled[fwslot]["lf"]
                and lf_tag_type != TagSpecificType.UNDEFINED
            ):
                if current != slot:
                    self.cmd.set_active_slot(slot)
                    current = slot
                if lf_tag_type == TagSpecificType.EM410X:
                    id = self.cmd.em410x_get_emu_id()
                    print(f"      {'ID:':40}{color_string((CY, id.hex().upper()))}")
                if lf_tag_type == TagSpecificType.HIDProx:
                    (format, fc, cn1, cn2, il, oem) = self.cmd.hidprox_get_emu_id()
                    cn = (cn1 << 32) + cn2
                    print(
                        f"      {'Format:':40}{color_string((CY, HIDFormat(format)))}"
                    )
                    if fc > 0:
                        print(f" FC: {color_string((CG, fc))}")
                    if il > 0:
                        print(f" IL: {color_string((CG, il))}")
                    if oem > 0:
                        print(f" OEM: {color_string((CG, oem))}")
                    print(f" CN: {color_string((CG, cn))}")
                if lf_tag_type == TagSpecificType.Viking:
                    id = self.cmd.viking_get_emu_id()
                    print(f"      {'ID:':40}{color_string((CY, id.hex().upper()))}")
        if current != selected:
            self.cmd.set_active_slot(selected)


@hw_slot.command("change")
class HWSlotSet(SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulation tag slot activated"
        return self.add_slot_args(parser, mandatory=True)

    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd.set_active_slot(slot_index)
        print(f" - Set slot {slot_index} activated success.")


@hw_slot.command("type")
class HWSlotType(TagTypeArgsUnit, SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulation tag type"
        self.add_slot_args(parser)
        self.add_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        tag_type = TagSpecificType[args.type]
        if args.slot is not None:
            slot_num = args.slot
        else:
            slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
        self.cmd.set_slot_tag_type(slot_num, tag_type)
        print(f" - Set slot {slot_num} tag type success.")


@hw_slot.command("delete")
class HWDeleteSlotSense(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Delete sense type data for a specific slot"
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.slot is not None:
            slot_num = args.slot
        else:
            slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
        if args.lf:
            sense_type = TagSenseType.LF
        else:
            sense_type = TagSenseType.HF
        self.cmd.delete_slot_sense_type(slot_num, sense_type)
        print(f" - Delete slot {slot_num} {sense_type.name} tag type success.")


@hw_slot.command("init")
class HWSlotInit(TagTypeArgsUnit, SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulation tag data to default"
        self.add_slot_args(parser)
        self.add_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        tag_type = TagSpecificType[args.type]
        if args.slot is not None:
            slot_num = args.slot
        else:
            slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
        self.cmd.set_slot_data_default(slot_num, tag_type)
        print(" - Set slot tag data init success.")


@hw_slot.command("enable")
class HWSlotEnable(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Enable tag slot"
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.slot is not None:
            slot_num = args.slot
        else:
            slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
        if args.lf:
            sense_type = TagSenseType.LF
        else:
            sense_type = TagSenseType.HF
        self.cmd.set_slot_enable(slot_num, sense_type, True)
        print(f" - Enable slot {slot_num} {sense_type.name} success.")


@hw_slot.command("disable")
class HWSlotDisable(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Disable tag slot"
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        if args.lf:
            sense_type = TagSenseType.LF
        else:
            sense_type = TagSenseType.HF
        self.cmd.set_slot_enable(slot_num, sense_type, False)
        print(f" - Disable slot {slot_num} {sense_type.name} success.")


@lf_em_410x.command("econfig")
class LFEM410xEconfig(SlotIndexArgsAndGoUnit, LFEMIdArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulated em410x card id"
        self.add_slot_args(parser)
        self.add_card_arg(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.id is not None:
            self.cmd.em410x_set_emu_id(bytes.fromhex(args.id))
            print(" - Set em410x tag id success.")
        else:
            response = self.cmd.em410x_get_emu_id()
            print(" - Get em410x tag id success.")
            print(f"ID: {response.hex()}")


@lf_viking.command("econfig")
class LFVikingEconfig(SlotIndexArgsAndGoUnit, LFVikingIdArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Set emulated Viking card id"
        self.add_slot_args(parser)
        self.add_card_arg(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.id is not None:
            slotinfo = self.cmd.get_slot_info()
            selected = SlotNumber.from_fw(self.cmd.get_active_slot())
            lf_tag_type = TagSpecificType(slotinfo[selected - 1]["lf"])
            if lf_tag_type != TagSpecificType.Viking:
                print(f"{color_string((CR, 'WARNING'))}: Slot type not set to Viking.")
            self.cmd.viking_set_emu_id(bytes.fromhex(args.id))
            print(" - Set Viking tag id success.")
        else:
            response = self.cmd.viking_get_emu_id()
            print(" - Get Viking tag id success.")
            print(f"ID: {response.hex().upper()}")


@hw_slot.command("nick")
class HWSlotNick(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get/Set/Delete tag nick name for slot"
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        action_group = parser.add_mutually_exclusive_group()
        action_group.add_argument(
            "-n", "--name", type=str, required=False, help="Set tag nick name for slot"
        )
        action_group.add_argument(
            "-d", "--delete", action="store_true", help="Delete tag nick name for slot"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.slot is not None:
            slot_num = args.slot
        else:
            slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
        if args.lf:
            sense_type = TagSenseType.LF
        else:
            sense_type = TagSenseType.HF
        if args.name is not None:
            name: str = args.name
            self.cmd.set_slot_tag_nick(slot_num, sense_type, name)
            print(f" - Set tag nick name for slot {slot_num} {sense_type.name}: {name}")
        elif args.delete:
            self.cmd.delete_slot_tag_nick(slot_num, sense_type)
            print(f" - Delete tag nick name for slot {slot_num} {sense_type.name}")
        else:
            res = self.cmd.get_slot_tag_nick(slot_num, sense_type)
            print(f" - Get tag nick name for slot {slot_num} {sense_type.name}: {res}")


@hw_slot.command("store")
class HWSlotUpdate(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Store slots config & data to device flash"
        return parser

    def on_exec(self, args: argparse.Namespace):
        self.cmd.slot_data_config_save()
        print(" - Store slots config and data from device memory to flash success.")


@hw_slot.command("openall")
class HWSlotOpenAll(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Open all slot and set to default data"
        return parser

    def on_exec(self, args: argparse.Namespace):
        # what type you need set to default?
        hf_type = TagSpecificType.MIFARE_1024
        lf_type = TagSpecificType.EM410X

        # set all slot
        for slot in SlotNumber:
            print(f" Slot {slot} setting...")
            # first to set tag type
            self.cmd.set_slot_tag_type(slot, hf_type)
            self.cmd.set_slot_tag_type(slot, lf_type)
            # to init default data
            self.cmd.set_slot_data_default(slot, hf_type)
            self.cmd.set_slot_data_default(slot, lf_type)
            # finally, we can enable this slot.
            self.cmd.set_slot_enable(slot, TagSenseType.HF, True)
            self.cmd.set_slot_enable(slot, TagSenseType.LF, True)
            print(f" Slot {slot} setting done.")

        # update config and save to flash
        self.cmd.slot_data_config_save()
        print(" - Succeeded opening all slots and setting data to default.")


@hw.command("dfu")
class HWDFU(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Restart application to bootloader/DFU mode"
        return parser

    def on_exec(self, args: argparse.Namespace):
        print("Application restarting...")
        self.cmd.enter_bootloader()
        # In theory, after the above command is executed, the dfu mode will enter, and then the USB will restart,
        # To judge whether to enter the USB successfully, we only need to judge whether the USB becomes the VID and PID
        # of the DFU device.
        # At the same time, we remember to confirm the information of the device,
        # it is the same device when it is consistent.
        print(" - Enter success @.@~")
        # let time for comm thread to send dfu cmd and close port
        time.sleep(0.1)


@hw_settings.command("animation")
class HWSettingsAnimation(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get or change current animation mode value"
        mode_names = [m.name for m in list(AnimationMode)]
        help_str = "Mode: " + ", ".join(mode_names)
        parser.add_argument(
            "-m",
            "--mode",
            type=str,
            required=False,
            help=help_str,
            metavar="MODE",
            choices=mode_names,
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.mode is not None:
            mode = AnimationMode[args.mode]
            self.cmd.set_animation_mode(mode)
            print("Animation mode change success.")
            print(color_string((CY, "Do not forget to store your settings in flash!")))
        else:
            print(AnimationMode(self.cmd.get_animation_mode()))


@hw_settings.command("bleclearbonds")
class HWSettingsBleClearBonds(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Clear all BLE bindings. Warning: effect is immediate!"
        parser.add_argument(
            "--force", default=False, action="store_true", help="Just to be sure"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print(
                "If you are you really sure, read the command documentation to see how to proceed."
            )
            return
        self.cmd.delete_all_ble_bonds()
        print(" - Successfully clear all bonds")


@hw_settings.command("store")
class HWSettingsStore(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Store current settings to flash"
        return parser

    def on_exec(self, args: argparse.Namespace):
        print("Storing settings...")
        if self.cmd.save_settings():
            print(" - Store success @.@~")
        else:
            print(" - Store failed")


@hw_settings.command("reset")
class HWSettingsReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Reset settings to default values"
        parser.add_argument(
            "--force", default=False, action="store_true", help="Just to be sure"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print(
                "If you are you really sure, read the command documentation to see how to proceed."
            )
            return
        print("Initializing settings...")
        if self.cmd.reset_settings():
            print(" - Reset success @.@~")
        else:
            print(" - Reset failed")


@hw.command("factory_reset")
class HWFactoryReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = (
            "Wipe all slot data and custom settings and return to factory settings"
        )
        parser.add_argument(
            "--force", default=False, action="store_true", help="Just to be sure"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print(
                "If you are you really sure, read the command documentation to see how to proceed."
            )
            return
        if self.cmd.wipe_fds():
            print(" - Reset successful! Please reconnect.")
            # let time for comm thread to close port
            time.sleep(0.1)
        else:
            print(" - Reset failed!")


@hw.command("battery")
class HWBatteryInfo(DeviceRequiredUnit):
    # How much remaining battery is considered low?
    BATTERY_LOW_LEVEL = 30

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get battery information, voltage and level"
        return parser

    def on_exec(self, args: argparse.Namespace):
        voltage, percentage = self.cmd.get_battery_info()
        print(" - Battery information:")
        print(f"   voltage    -> {voltage} mV")
        print(f"   percentage -> {percentage}%")
        if percentage < HWBatteryInfo.BATTERY_LOW_LEVEL:
            print(color_string((CR, "[!] Low battery, please charge.")))


@hw_settings.command("btnpress")
class HWButtonSettingsGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get or set button press function of Button A and Button B"
        button_group = parser.add_mutually_exclusive_group()
        button_group.add_argument("-a", "-A", action="store_true", help="Button A")
        button_group.add_argument("-b", "-B", action="store_true", help="Button B")
        duration_group = parser.add_mutually_exclusive_group()
        duration_group.add_argument(
            "-s", "--short", action="store_true", help="Short-press (default)"
        )
        duration_group.add_argument(
            "-l", "--long", action="store_true", help="Long-press"
        )
        function_names = [f.name for f in list(ButtonPressFunction)]
        function_descs = [f"{f.name} ({f})" for f in list(ButtonPressFunction)]
        help_str = "Function: " + ", ".join(function_descs)
        parser.add_argument(
            "-",
            "--function",
            type=str,
            required=False,
            help=help_str,
            metavar="FUNCTION",
            choices=function_names,
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.function is not None:
            function = ButtonPressFunction[args.function]
            if not args.a and not args.b:
                print(
                    color_string(
                        (CR, "You must specify which button you want to change")
                    )
                )
                return
            if args.a:
                button = ButtonType.A
            else:
                button = ButtonType.B
            if args.long:
                self.cmd.set_long_button_press_config(button, function)
            else:
                self.cmd.set_button_press_config(button, function)
            print(
                f" - Successfully set function '{function}'"
                f" to Button {button.name} {'long-press' if args.long else 'short-press'}"
            )
            print(color_string((CY, "Do not forget to store your settings in flash!")))
        else:
            if args.a:
                button_list = [ButtonType.A]
            elif args.b:
                button_list = [ButtonType.B]
            else:
                button_list = list(ButtonType)
            for button in button_list:
                if not args.long:
                    resp = self.cmd.get_button_press_config(button)
                    button_fn = ButtonPressFunction(resp)
                    print(f"{color_string((CG, f'{button.name} short'))}: {button_fn}")
                if not args.short:
                    resp_long = self.cmd.get_long_button_press_config(button)
                    button_long_fn = ButtonPressFunction(resp_long)
                    print(
                        f"{color_string((CG, f'{button.name} long'))}: {button_long_fn}"
                    )
                print("")


@hw_settings.command("blekey")
class HWSettingsBLEKey(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Get or set the ble connect key"
        parser.add_argument(
            "-k", "--key", required=False, help="Ble connect key for your device"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        key = self.cmd.get_ble_pairing_key()
        print(f" - The current key of the device(ascii): {color_string((CG, key))}")

        if args.key is not None:
            if len(args.key) != 6:
                print(
                    f" - {color_string((CR, 'The ble connect key length must be 6'))}"
                )
                return
            if re.match(r"[0-9]{6}", args.key):
                self.cmd.set_ble_connect_key(args.key)
                print(
                    f" - Successfully set ble connect key to : {color_string((CG, args.key))}"
                )
                print(
                    color_string((CY, "Do not forget to store your settings in flash!"))
                )
            else:
                print(
                    f" - {color_string((CR, 'Only 6 ASCII characters from 0 to 9 are supported.'))}"
                )


@hw_settings.command("blepair")
class HWBlePair(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Show or configure BLE pairing"
        set_group = parser.add_mutually_exclusive_group()
        set_group.add_argument(
            "-e", "--enable", action="store_true", help="Enable BLE pairing"
        )
        set_group.add_argument(
            "-d", "--disable", action="store_true", help="Disable BLE pairing"
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        is_pairing_enable = self.cmd.get_ble_pairing_enable()
        enabled_str = color_string((CG, "Enabled"))
        disabled_str = color_string((CR, "Disabled"))

        if not args.enable and not args.disable:
            if is_pairing_enable:
                print(f" - BLE pairing: {enabled_str}")
            else:
                print(f" - BLE pairing: {disabled_str}")
        elif args.enable:
            if is_pairing_enable:
                print(color_string((CY, "BLE pairing is already enabled.")))
                return
            self.cmd.set_ble_pairing_enable(True)
            print(f" - Successfully change ble pairing to {enabled_str}.")
            print(color_string((CY, "Do not forget to store your settings in flash!")))
        elif args.disable:
            if not is_pairing_enable:
                print(color_string((CY, "BLE pairing is already disabled.")))
                return
            self.cmd.set_ble_pairing_enable(False)
            print(f" - Successfully change ble pairing to {disabled_str}.")
            print(color_string((CY, "Do not forget to store your settings in flash!")))


@hw.command("raw")
class HWRaw(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Send raw command"
        cmd_names = sorted([c.name for c in list(Command)])
        help_str = "Command: " + ", ".join(cmd_names)
        command_group = parser.add_mutually_exclusive_group(required=True)
        command_group.add_argument(
            "-c",
            "--command",
            type=str,
            metavar="COMMAND",
            help=help_str,
            choices=cmd_names,
        )
        command_group.add_argument(
            "-n",
            "--num_command",
            type=int,
            metavar="<dec>",
            help="Numeric command ID: <dec>",
        )
        parser.add_argument(
            "-d", "--data", type=str, help="Data to send", default="", metavar="<hex>"
        )
        parser.add_argument(
            "-t",
            "--timeout",
            type=int,
            help="Timeout in seconds",
            default=3,
            metavar="<dec>",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.command is not None:
            command = Command[args.command]
        else:
            # We accept not-yet-known command ids as "hw raw" is meant for debugging
            command = args.num_command
        response = self.cmd.device.send_cmd_sync(
            command, data=bytes.fromhex(args.data), status=0x0, timeout=args.timeout
        )
        print(" - Received:")
        try:
            command = Command(response.cmd)
            print(f"   Command: {response.cmd} {command.name}")
        except ValueError:
            print(f"   Command: {response.cmd} (unknown)")

        status_string = f"   Status:  {response.status:#02x}"
        try:
            status = Status(response.status)
            status_string += f" {status.name}"
            status_string += f": {str(status)}"
        except ValueError:
            pass
        print(status_string)
        print(f"   Data (HEX): {response.data.hex()}")


@hf_14a.command("raw")
class HF14ARaw(ReaderRequiredUnit):
    def bool_to_bit(self, value):
        return 1 if value else 0

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.formatter_class = argparse.RawDescriptionHelpFormatter
        parser.description = "Send raw command"
        parser.add_argument(
            "-a",
            "--activate-rf",
            help="Active signal field ON without select",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "-s",
            "--select-tag",
            help="Active signal field ON with select",
            action="store_true",
            default=False,
        )
        # TODO: parser.add_argument('-3', '--type3-select-tag',
        #           help="Active signal field ON with ISO14443-3 select (no RATS)", action='store_true', default=False,)
        parser.add_argument(
            "-d", "--data", type=str, metavar="<hex>", help="Data to be sent"
        )
        parser.add_argument(
            "-b",
            "--bits",
            type=int,
            metavar="<dec>",
            help="Number of bits to send. Useful for send partial byte",
        )
        parser.add_argument(
            "-c",
            "--crc",
            help="Calculate and append CRC",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "-r",
            "--no-response",
            help="Do not read response",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "-cc",
            "--crc-clear",
            help="Verify and clear CRC of received data",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "-k",
            "--keep-rf",
            help="Keep signal field ON after receive",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "-t",
            "--timeout",
            type=int,
            metavar="<dec>",
            help="Timeout in ms",
            default=100,
        )
        parser.epilog = """
examples/notes:
  hf 14a raw -b 7 -d 40 -k
  hf 14a raw -d 43 -k
  hf 14a raw -d 3000 -c
  hf 14a raw -sc -d 6000
"""
        return parser

    def on_exec(self, args: argparse.Namespace):
        options = {
            "activate_rf_field": self.bool_to_bit(args.activate_rf),
            "wait_response": self.bool_to_bit(not args.no_response),
            "append_crc": self.bool_to_bit(args.crc),
            "auto_select": self.bool_to_bit(args.select_tag),
            "keep_rf_field": self.bool_to_bit(args.keep_rf),
            "check_response_crc": self.bool_to_bit(args.crc_clear),
            # 'auto_type3_select': self.bool_to_bit(args.type3-select-tag),
        }
        data: str = args.data
        if data is not None:
            data = data.replace(" ", "")
            if re.match(r"^[0-9a-fA-F]+$", data):
                if len(data) % 2 != 0:
                    print(
                        f" [!] {color_string((CR, 'The length of the data must be an integer multiple of 2.'))}"
                    )
                    return
                else:
                    data_bytes = bytes.fromhex(data)
            else:
                print(f" [!] {color_string((CR, 'The data must be a HEX string'))}")
                return
        else:
            data_bytes = []
        if args.bits is not None and args.crc:
            print(
                f" [!] {color_string((CR, '--bits and --crc are mutually exclusive'))}"
            )
            return

        # Exec 14a raw cmd.
        resp = self.cmd.hf14a_raw(options, args.timeout, data_bytes, args.bits)
        if len(resp) > 0:
            print(
                # print head
                " - "
                +
                # print data
                " ".join([hex(byte).replace("0x", "").rjust(2, "0") for byte in resp])
            )
        else:
            print(f" [*] {color_string((CY, 'No response'))}")


# =====================================================================================
# NFC Fuzzer / Attack Commands
# =====================================================================================


@hf_14a.command("fuzz")
class HF14AFuzz(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "ISO14443-A NFC Fuzzer - send fuzzed commands to a tag"

        parser.add_argument(
            "-m",
            "--mode",
            type=str,
            choices=["random", "cmd", "auth", "ats", "uid"],
            default="random",
            help="Fuzz mode: random (random bytes), cmd (mutate known cmds), "
            "auth (auth sequence fuzzing), ats (ATS request fuzzing), "
            "uid (anticollision fuzzing)",
        )
        parser.add_argument(
            "-n",
            "--count",
            type=int,
            default=50,
            metavar="<dec>",
            help="Number of fuzz iterations (default: 50)",
        )
        parser.add_argument(
            "--min-len",
            type=int,
            default=1,
            metavar="<dec>",
            help="Minimum payload length in bytes (default: 1)",
        )
        parser.add_argument(
            "--max-len",
            type=int,
            default=32,
            metavar="<dec>",
            help="Maximum payload length in bytes (default: 32)",
        )
        parser.add_argument(
            "-t",
            "--timeout",
            type=int,
            default=100,
            metavar="<dec>",
            help="Response timeout in ms (default: 100)",
        )
        parser.add_argument(
            "--log",
            type=argparse.FileType("w", encoding="utf8"),
            help="Log results to file",
        )
        parser.add_argument(
            "-d",
            "--delay",
            type=float,
            default=0.05,
            metavar="<float>",
            help="Delay between fuzz attempts in seconds (default: 0.05)",
        )
        return parser

    # Known ISO 14443-A commands for mutation
    KNOWN_CMDS = [
        bytes([0x26]),  # REQA (7 bit)
        bytes([0x52]),  # WUPA (7 bit)
        bytes([0x93, 0x20]),  # ANTICOLLISION CL1
        bytes([0x93, 0x70]),  # SELECT CL1
        bytes([0x95, 0x20]),  # ANTICOLLISION CL2
        bytes([0x95, 0x70]),  # SELECT CL2
        bytes([0x50, 0x00]),  # HALT
        bytes([0xE0, 0x80]),  # RATS
        bytes([0x60]),  # MF AUTH_A
        bytes([0x61]),  # MF AUTH_B
        bytes([0x30]),  # MF READ
        bytes([0xA0]),  # MF WRITE
        bytes([0xC0]),  # MF DECREMENT
        bytes([0xC1]),  # MF INCREMENT
        bytes([0xC2]),  # MF RESTORE
        bytes([0xB0]),  # MF TRANSFER
    ]

    def _random_payload(self, min_len, max_len):
        length = secrets.randbelow(max_len - min_len + 1) + min_len
        return secrets.token_bytes(length)

    def _mutate_cmd(self, cmd):
        data = bytearray(cmd)
        mutation = secrets.randbelow(4)
        if mutation == 0:
            # Bit flip in random byte
            idx = secrets.randbelow(len(data))
            bit = 1 << secrets.randbelow(8)
            data[idx] ^= bit
        elif mutation == 1:
            # Append random bytes
            extra = secrets.randbelow(4) + 1
            data.extend(secrets.token_bytes(extra))
        elif mutation == 2:
            # Replace random byte
            idx = secrets.randbelow(len(data))
            data[idx] = secrets.randbelow(256)
        elif mutation == 3:
            # Truncate
            if len(data) > 1:
                data = data[: secrets.randbelow(len(data) - 1) + 1]
        return bytes(data)

    def _send_raw(
        self,
        data,
        timeout,
        auto_select=False,
        keep_rf=True,
        crc=False,
        activate=False,
        bitlen=None,
        wait_response=True,
    ):
        options = {
            "activate_rf_field": 1 if activate else 0,
            "wait_response": 1 if wait_response else 0,
            "append_crc": 1 if crc else 0,
            "auto_select": 1 if auto_select else 0,
            "keep_rf_field": 1 if keep_rf else 0,
            "check_response_crc": 0,
        }
        try:
            if len(data) == 0 and bitlen is None:
                # For activate/select-only calls with no data, don't wait for response
                options["wait_response"] = 0
            resp = self.cmd.hf14a_raw(options, timeout, data, bitlen)
            if resp is not None and isinstance(resp, (bytes, bytearray)):
                return resp
            return b""
        except Exception:
            return b""

    def _format_hex(self, data):
        return " ".join(f"{b:02x}" for b in data)

    def _log_result(self, log_file, iteration, tx_data, rx_data, bitlen=None):
        tx_hex = self._format_hex(tx_data)
        rx_hex = (
            self._format_hex(rx_data)
            if rx_data is not None and len(rx_data) > 0
            else "NO_RESPONSE"
        )
        bits_str = f" (bits={bitlen})" if bitlen else ""
        line = f"[{iteration:04d}] TX: {tx_hex}{bits_str} => RX: {rx_hex}"
        print(f" - {line}")
        if log_file:
            log_file.write(line + "\n")

    def on_exec(self, args: argparse.Namespace):
        mode = args.mode
        count = args.count
        log_file = args.log
        timeout = args.timeout
        delay = args.delay
        responses = 0

        print(
            f" [*] {color_string((CY, f'NFC Fuzzer - Mode: {mode}, Iterations: {count}'))}"
        )
        print(
            f" [*] Payload size: {args.min_len}-{args.max_len} bytes, timeout: {timeout}ms"
        )
        print(f" [*] {color_string((CY, 'Press Ctrl+C to stop'))}\n")

        try:
            for i in range(count):
                tx_data = b""
                bitlen = None

                if mode == "random":
                    tx_data = self._random_payload(args.min_len, args.max_len)
                    # Activate and select first, then send random data
                    self._send_raw(
                        b"", timeout, activate=True, auto_select=True, keep_rf=True
                    )
                    resp = self._send_raw(tx_data, timeout, keep_rf=True)

                elif mode == "cmd":
                    base_cmd = self.KNOWN_CMDS[secrets.randbelow(len(self.KNOWN_CMDS))]
                    tx_data = self._mutate_cmd(base_cmd)
                    # Activate RF for command fuzzing
                    self._send_raw(b"", timeout, activate=True, keep_rf=True)
                    resp = self._send_raw(tx_data, timeout, keep_rf=True)

                elif mode == "auth":
                    # Fuzz authentication sequences
                    self._send_raw(
                        b"", timeout, activate=True, auto_select=True, keep_rf=True
                    )
                    auth_cmd = secrets.choice([0x60, 0x61])
                    block = secrets.randbelow(64)
                    tx_data = bytes([auth_cmd, block])
                    resp = self._send_raw(tx_data, timeout, keep_rf=True, crc=True)

                elif mode == "ats":
                    # Fuzz RATS/ATS parameters
                    self._send_raw(
                        b"", timeout, activate=True, auto_select=True, keep_rf=True
                    )
                    # RATS command with fuzzed parameter byte
                    param = secrets.randbelow(256)
                    tx_data = bytes([0xE0, param])
                    resp = self._send_raw(tx_data, timeout, keep_rf=True, crc=True)

                elif mode == "uid":
                    # Fuzz anticollision - send short frames
                    self._send_raw(b"", timeout, activate=True, keep_rf=True)
                    sel_choices = [0x93, 0x95, 0x97]
                    sel = secrets.choice(sel_choices)
                    nvb = secrets.randbelow(256)
                    tx_data = bytes([sel, nvb])
                    resp = self._send_raw(tx_data, timeout, keep_rf=True)

                else:
                    resp = b""

                if len(resp) > 0:
                    responses += 1
                self._log_result(log_file, i + 1, tx_data, resp, bitlen)

                time.sleep(delay)

        except KeyboardInterrupt:
            print(f"\n [!] {color_string((CY, 'Fuzzing interrupted by user'))}")
        except Exception as e:
            print(f"\n [!] {color_string((CR, f'Fuzzing error: {e}'))}")
        finally:
            # Turn off RF field
            try:
                self._send_raw(
                    b"", timeout, keep_rf=False, activate=False, wait_response=False
                )
            except Exception:
                pass

        print(
            f"\n [*] {color_string((CG, f'Fuzzing complete. {responses}/{count} got responses.'))}"
        )
        if log_file:
            log_file.close()
            print(" [*] Log saved.")


@hf_14a.command("attacks")
class HF14AAttacks(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "ISO14443-A attack suite - common NFC attack patterns"

        parser.add_argument(
            "-m",
            "--mode",
            type=str,
            choices=["keybf", "default-keys", "access-brute", "relay-test"],
            required=True,
            help="Attack mode: keybf (brute force last byte of key), "
            "default-keys (try common default keys), "
            "access-brute (try all access bit combos), "
            "relay-test (timing measurement for relay detection)",
        )
        parser.add_argument(
            "--blk",
            "--block",
            type=int,
            default=0,
            metavar="<dec>",
            help="Target block number (default: 0)",
        )
        parser.add_argument(
            "-k",
            "--key",
            type=str,
            default="FFFFFFFFFFFF",
            metavar="<hex>",
            help="Base key for partial brute force (default: FFFFFFFFFFFF)",
        )
        parser.add_argument(
            "-t",
            "--timeout",
            type=int,
            default=100,
            metavar="<dec>",
            help="Response timeout in ms (default: 100)",
        )
        parser.add_argument(
            "--log",
            type=argparse.FileType("w", encoding="utf8"),
            help="Log results to file",
        )
        return parser

    # Common default keys for MIFARE Classic
    DEFAULT_KEYS = [
        "FFFFFFFFFFFF",
        "000000000000",
        "A0A1A2A3A4A5",
        "B0B1B2B3B4B5",
        "D3F7D3F7D3F7",
        "AABBCCDDEEFF",
        "1A2B3C4D5E6F",
        "010203040506",
        "111111111111",
        "222222222222",
        "333333333333",
        "444444444444",
        "555555555555",
        "666666666666",
        "777777777777",
        "888888888888",
        "999999999999",
        "AAAAAAAAAAAA",
        "BBBBBBBBBBBB",
        "CCCCCCCCCCCC",
        "DDDDDDDDDDDD",
        "EEEEEEEEEEEE",
        "4D3A99C351DD",
        "1A982C7E459A",
        "714C5C886E97",
        "587EE5F9350F",
        "A0478CC39091",
        "533CB6C723F6",
        "8FD0A4F256E9",
        "0000014B5C31",
        "B578F38A5C61",
        "96A301BCE267",
    ]

    def _try_auth(self, block, key_type, key_bytes):
        try:
            self.cmd.mf1_read_one_block(block, key_type, key_bytes)
            return True
        except UnexpectedResponseError:
            return False
        except Exception:
            return False

    def _attack_default_keys(self, args):
        block = args.blk
        print(f" [*] Trying {len(self.DEFAULT_KEYS)} default keys on block {block}...")
        found = []

        for key_hex in self.DEFAULT_KEYS:
            key_bytes = bytearray.fromhex(key_hex)
            for key_type, type_name in [(MfcKeyType.A, "A"), (MfcKeyType.B, "B")]:
                if self._try_auth(block, key_type, key_bytes):
                    print(
                        f" [+] {color_string((CG, f'FOUND Key {type_name}: {key_hex} on block {block}'))}"
                    )
                    found.append((type_name, key_hex))
                    if args.log:
                        args.log.write(
                            f"FOUND Key {type_name}: {key_hex} block {block}\n"
                        )

        if not found:
            print(f" [-] {color_string((CR, 'No default keys found'))}")
        else:
            print(f"\n [*] {color_string((CG, f'Found {len(found)} key(s)'))}")
        return found

    def _attack_keybf(self, args):
        if not re.match(r"^[a-fA-F0-9]{12}$", args.key):
            print(f" [!] {color_string((CR, 'Key must be 12 hex characters'))}")
            return

        block = args.blk
        base_key = bytearray.fromhex(args.key)
        print(f" [*] Brute forcing last byte of key on block {block}...")
        print(f" [*] Base key: {args.key[:10]}XX")

        for key_type, type_name in [(MfcKeyType.A, "A"), (MfcKeyType.B, "B")]:
            for last_byte in range(256):
                candidate = bytearray(base_key)
                candidate[5] = last_byte
                if self._try_auth(block, key_type, candidate):
                    found_key = candidate.hex().upper()
                    print(
                        f"\n [+] {color_string((CG, f'FOUND Key {type_name}: {found_key}'))}"
                    )
                    if args.log:
                        args.log.write(
                            f"FOUND Key {type_name}: {found_key} block {block}\n"
                        )
                    return found_key
                if last_byte % 32 == 0:
                    print(
                        f" - Key {type_name}: testing {args.key[:10]}{last_byte:02X}... ({last_byte}/255)"
                    )

        print(f" [-] {color_string((CR, 'Key not found'))}")
        return None

    def _attack_access_brute(self, args):
        if not re.match(r"^[a-fA-F0-9]{12}$", args.key):
            print(f" [!] {color_string((CR, 'Key must be 12 hex characters'))}")
            return

        block = args.blk
        key_bytes = bytearray.fromhex(args.key)
        # Determine sector trailer block
        if block < 128:
            trailer = (block // 4) * 4 + 3
        else:
            trailer = (block // 16) * 16 + 15
        sector = trailer // 4 if trailer < 128 else 32 + (trailer - 128) // 16

        print(f" [*] Reading sector {sector} trailer (block {trailer}) access bits...")
        try:
            resp = self.cmd.mf1_read_one_block(trailer, MfcKeyType.A, key_bytes)
            access_bytes = resp[6:10]
            print(f" [*] Access bytes: {access_bytes.hex().upper()}")

            # Decode access bits
            b7 = access_bytes[2]
            b8 = access_bytes[1]

            for blk_idx in range(4):
                c1 = (b7 >> (4 + blk_idx)) & 1
                c2 = (b8 >> blk_idx) & 1
                c3 = (b8 >> (4 + blk_idx)) & 1
                access = (c3 << 2) | (c2 << 1) | c1
                desc = self._describe_access(blk_idx, access, blk_idx == 3)
                blk_num = trailer - 3 + blk_idx
                print(f"   Block {blk_num}: C1={c1} C2={c2} C3={c3} => {desc}")
                if args.log:
                    args.log.write(
                        f"Block {blk_num}: C1={c1} C2={c2} C3={c3} => {desc}\n"
                    )

        except Exception as e:
            print(f" [!] {color_string((CR, f'Failed to read trailer: {e}'))}")

    @staticmethod
    def _describe_access(block_in_sector, access_val, is_trailer):
        if is_trailer:
            descriptions = {
                0: "Key A: write=A, Access bits: read=A write=never, Key B: read=A write=A",
                1: "Key A: write=never, Access bits: read=A write=A, Key B: read=A write=never",
                2: "Key A: write=never, Access bits: read=A write=never, Key B: read=A write=never",
                3: "Key A: write=B, Access bits: read=A/B write=B, Key B: read=never write=B",
                4: "Key A: write=B, Access bits: read=A/B write=never, Key B: read=never write=B",
                5: "Key A: write=never, Access bits: read=A/B write=B, Key B: read=never write=never",
                6: "Key A: write=never, Access bits: read=A/B write=never, Key B: read=never write=never",
                7: "Key A: write=never, Access bits: read=A/B write=never, Key B: read=never write=never",
            }
        else:
            descriptions = {
                0: "read=A/B write=A/B inc=A/B dec/rest=A/B",
                1: "read=A/B write=never inc=never dec/rest=A/B (value)",
                2: "read=A/B write=never inc=never dec/rest=never",
                3: "read=B write=B inc=never dec/rest=never",
                4: "read=A/B write=B inc=never dec/rest=never",
                5: "read=B write=never inc=never dec/rest=never",
                6: "read=A/B write=B inc=B dec/rest=A/B (value)",
                7: "read=never write=never inc=never dec/rest=never",
            }
        return descriptions.get(access_val, "Unknown")

    def _attack_relay_test(self, args):
        print(" [*] Relay attack timing test - measuring response times...")
        print(" [*] Low latency = direct, High latency = possible relay\n")
        times = []

        for i in range(10):
            options = {
                "activate_rf_field": 1,
                "wait_response": 1,
                "append_crc": 0,
                "auto_select": 1,
                "keep_rf_field": 1,
                "check_response_crc": 0,
            }
            start = time.perf_counter()
            try:
                options["wait_response"] = 0  # don't wait for data response on select
                self.cmd.hf14a_raw(options, args.timeout, b"", None)
            except Exception:
                pass
            elapsed_ms = (time.perf_counter() - start) * 1000
            times.append(elapsed_ms)
            print(f" - Round {i + 1:2d}: {elapsed_ms:.2f} ms")
            time.sleep(0.1)

        if times:
            avg = sum(times) / len(times)
            min_t = min(times)
            max_t = max(times)
            print(
                f"\n [*] Results: avg={avg:.2f}ms  min={min_t:.2f}ms  max={max_t:.2f}ms"
            )
            if avg > 50:
                print(
                    f" [!] {color_string((CY, 'WARNING: High latency detected - possible relay attack'))}"
                )
            else:
                print(f" [*] {color_string((CG, 'Timing looks normal'))}")
            if args.log:
                args.log.write(f"avg={avg:.2f}ms min={min_t:.2f}ms max={max_t:.2f}ms\n")

    def on_exec(self, args: argparse.Namespace):
        mode = args.mode
        print(f" [*] {color_string((CY, f'Attack mode: {mode}'))}\n")

        if mode == "default-keys":
            self._attack_default_keys(args)
        elif mode == "keybf":
            self._attack_keybf(args)
        elif mode == "access-brute":
            self._attack_access_brute(args)
        elif mode == "relay-test":
            self._attack_relay_test(args)

        if args.log:
            args.log.close()
            print(" [*] Log saved.")


# =====================================================================================
# Hex Editor Command
# =====================================================================================


@hf_mf.command("hexedit")
class HFMFHexEdit(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Interactive hex editor for MIFARE Classic blocks"

        parser.add_argument(
            "--blk",
            "--block",
            type=int,
            required=True,
            metavar="<dec>",
            help="Block number to edit",
        )
        type_group = parser.add_mutually_exclusive_group()
        type_group.add_argument(
            "-a", "-A", action="store_true", help="Key type A (default)"
        )
        type_group.add_argument("-b", "-B", action="store_true", help="Key type B")
        parser.add_argument(
            "-k",
            "--key",
            type=str,
            required=True,
            metavar="<hex>",
            help="Authentication key (hex[12])",
        )
        parser.add_argument(
            "--set-offset",
            type=int,
            metavar="<dec>",
            help="Byte offset (0-15) to start editing at",
        )
        parser.add_argument(
            "--set-data",
            type=str,
            metavar="<hex>",
            help="Hex data to write at offset (use with --set-offset)",
        )
        parser.add_argument(
            "--fill",
            type=str,
            metavar="<hex>",
            help="Fill entire block with repeating byte pattern (e.g. '00' or 'DEAD')",
        )
        parser.add_argument(
            "--xor",
            type=str,
            metavar="<hex>",
            help="XOR entire block with a hex byte (e.g. 'FF')",
        )
        parser.add_argument(
            "--replace",
            type=str,
            nargs=2,
            metavar=("<old_hex>", "<new_hex>"),
            help="Find and replace hex pattern in block",
        )
        parser.add_argument(
            "--view-only",
            action="store_true",
            help="Only display block data, do not write",
        )
        parser.add_argument(
            "--write-raw",
            type=str,
            metavar="<hex>",
            help="Write full 16-byte raw data to block (hex[32])",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        key_type = MfcKeyType.B if args.b else MfcKeyType.A
        if not re.match(r"^[a-fA-F0-9]{12}$", args.key):
            print(f" [!] {color_string((CR, 'Key must be 12 hex characters'))}")
            return
        key_bytes = bytearray.fromhex(args.key)
        block = args.blk

        # Read current block data
        try:
            data = bytearray(self.cmd.mf1_read_one_block(block, key_type, key_bytes))
        except Exception as e:
            print(f" [!] {color_string((CR, f'Failed to read block {block}: {e}'))}")
            return

        print(f"\n [*] Block {block} - Current data:")
        self._display_block(block, data)

        if args.view_only:
            return

        modified = False

        if args.write_raw is not None:
            raw = args.write_raw.replace(" ", "")
            if not re.match(r"^[a-fA-F0-9]{32}$", raw):
                print(
                    f" [!] {color_string((CR, 'Raw data must be exactly 32 hex characters (16 bytes)'))}"
                )
                return
            data = bytearray.fromhex(raw)
            modified = True

        elif args.fill is not None:
            pattern = args.fill.replace(" ", "")
            if not re.match(r"^[a-fA-F0-9]+$", pattern) or len(pattern) % 2 != 0:
                print(f" [!] {color_string((CR, 'Fill pattern must be valid hex'))}")
                return
            pat_bytes = bytes.fromhex(pattern)
            for i in range(16):
                data[i] = pat_bytes[i % len(pat_bytes)]
            modified = True

        elif args.xor is not None:
            xor_val = args.xor.replace(" ", "")
            if not re.match(r"^[a-fA-F0-9]{2}$", xor_val):
                print(
                    f" [!] {color_string((CR, 'XOR value must be a single hex byte (e.g. FF)'))}"
                )
                return
            xor_byte = int(xor_val, 16)
            for i in range(16):
                data[i] ^= xor_byte
            modified = True

        elif args.replace is not None:
            old_hex = args.replace[0].replace(" ", "")
            new_hex = args.replace[1].replace(" ", "")
            if not re.match(r"^[a-fA-F0-9]+$", old_hex) or len(old_hex) % 2 != 0:
                print(f" [!] {color_string((CR, 'Old pattern must be valid hex'))}")
                return
            if not re.match(r"^[a-fA-F0-9]+$", new_hex) or len(new_hex) % 2 != 0:
                print(f" [!] {color_string((CR, 'New pattern must be valid hex'))}")
                return
            old_bytes = bytes.fromhex(old_hex)
            new_bytes = bytes.fromhex(new_hex)
            if len(old_bytes) != len(new_bytes):
                print(
                    f" [!] {color_string((CR, 'Old and new patterns must be same length'))}"
                )
                return
            idx = bytes(data).find(old_bytes)
            if idx == -1:
                print(f" [!] {color_string((CY, 'Pattern not found in block'))}")
                return
            for j in range(len(new_bytes)):
                data[idx + j] = new_bytes[j]
            modified = True

        elif args.set_offset is not None and args.set_data is not None:
            offset = args.set_offset
            if offset < 0 or offset > 15:
                print(f" [!] {color_string((CR, 'Offset must be 0-15'))}")
                return
            edit_hex = args.set_data.replace(" ", "")
            if not re.match(r"^[a-fA-F0-9]+$", edit_hex) or len(edit_hex) % 2 != 0:
                print(f" [!] {color_string((CR, 'Data must be valid hex'))}")
                return
            edit_bytes = bytes.fromhex(edit_hex)
            if offset + len(edit_bytes) > 16:
                print(
                    f" [!] {color_string((CR, 'Data exceeds block size (16 bytes)'))}"
                )
                return
            for j in range(len(edit_bytes)):
                data[offset + j] = edit_bytes[j]
            modified = True

        else:
            print(
                f" [*] {color_string((CY, 'No edit operation specified. Use --help for options.'))}"
            )
            return

        if modified:
            print(f"\n [*] Block {block} - New data:")
            self._display_block(block, data)

            try:
                resp = self.cmd.mf1_write_one_block(block, key_type, key_bytes, data)
                if resp.parsed:
                    print(f" [+] {color_string((CG, 'Block written successfully!'))}")
                else:
                    print(f" [!] {color_string((CR, 'Write failed!'))}")
            except Exception as e:
                print(f" [!] {color_string((CR, f'Write error: {e}'))}")

    @staticmethod
    def _display_block(block, data):
        hex_str = " ".join(f"{b:02X}" for b in data)
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
        print(f"   Block {block:03d}: {hex_str}  |{ascii_str}|")
        # Also show as integers (little-endian)
        if len(data) == 16:
            int32s = struct.unpack("<iiii", data)
            print(
                f"   Int32 LE: {int32s[0]:>11d}  {int32s[1]:>11d}  {int32s[2]:>11d}  {int32s[3]:>11d}"
            )


# =====================================================================================
# Auto-Find Value Blocks
# =====================================================================================


@hf_mf.command("vfind")
class HFMFValueFind(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Auto-scan MIFARE Classic for value blocks"

        mifare_type_group = parser.add_mutually_exclusive_group()
        mifare_type_group.add_argument(
            "--mini",
            help="MIFARE Classic Mini / S20",
            action="store_const",
            dest="maxSectors",
            const=5,
        )
        mifare_type_group.add_argument(
            "--1k",
            help="MIFARE Classic 1k / S50 (default)",
            action="store_const",
            dest="maxSectors",
            const=16,
        )
        mifare_type_group.add_argument(
            "--2k",
            help="MIFARE Classic/Plus 2k",
            action="store_const",
            dest="maxSectors",
            const=32,
        )
        mifare_type_group.add_argument(
            "--4k",
            help="MIFARE Classic 4k / S70",
            action="store_const",
            dest="maxSectors",
            const=40,
        )

        parser.add_argument(
            "-k",
            "--key",
            type=str,
            default="FFFFFFFFFFFF",
            metavar="<hex>",
            help="Authentication key (default: FFFFFFFFFFFF)",
        )
        type_group = parser.add_mutually_exclusive_group()
        type_group.add_argument(
            "-a", "-A", action="store_true", help="Use key type A (default)"
        )
        type_group.add_argument("-b", "-B", action="store_true", help="Use key type B")
        parser.add_argument(
            "--keys-file",
            type=argparse.FileType("r", encoding="utf8"),
            help="File with keys (one hex key per line) to try per sector",
        )
        parser.add_argument(
            "--show-all",
            action="store_true",
            help="Show all block data, not just value blocks",
        )
        parser.add_argument(
            "--export",
            type=argparse.FileType("w", encoding="utf8"),
            help="Export found value blocks to file",
        )

        parser.set_defaults(maxSectors=16)
        return parser

    @staticmethod
    def _is_value_block(data):
        """Check if 16-byte block matches MIFARE Classic value block format:
        [value (4B)][~value (4B)][value (4B)][addr][~addr][addr][~addr]
        """
        if len(data) != 16:
            return False, None, None
        val1, val2, val3, adr1, adr2, adr3, adr4 = struct.unpack("<iiiBBBB", data)
        if val1 == val3 and val1 + val2 == -1:
            if adr1 == adr3 and adr2 == adr4 and (adr1 + adr2) & 0xFF == 0xFF:
                return True, val1, adr1
        return False, None, None

    def on_exec(self, args: argparse.Namespace):
        if not re.match(r"^[a-fA-F0-9]{12}$", args.key):
            print(f" [!] {color_string((CR, 'Key must be 12 hex characters'))}")
            return

        key_type = MfcKeyType.B if args.b else MfcKeyType.A
        max_sectors = args.maxSectors

        # Build key list
        keys_to_try = [bytearray.fromhex(args.key)]
        if args.keys_file:
            for line in args.keys_file:
                k = line.strip()
                if re.match(r"^[a-fA-F0-9]{12}$", k):
                    keys_to_try.append(bytearray.fromhex(k))
            args.keys_file.close()

        print(
            f" [*] {color_string((CY, f'Scanning {max_sectors} sectors for value blocks...'))}"
        )
        print(f" [*] Using {len(keys_to_try)} key(s), type {'B' if args.b else 'A'}\n")

        value_blocks_found = []
        blocks_read = 0
        blocks_failed = 0

        print(
            "-----+--------------------------------------------------+---------+----------"
        )
        print(
            " Blk | Data                                             | Value   | Status   "
        )
        print(
            "-----+--------------------------------------------------+---------+----------"
        )

        for sector in range(max_sectors):
            # Determine blocks in sector
            if sector < 32:
                first_block = sector * 4
                num_blocks = 4
            else:
                first_block = 128 + (sector - 32) * 16
                num_blocks = 16

            # Skip sector trailer (last block of sector)
            for blk_offset in range(num_blocks - 1):
                block = first_block + blk_offset
                data = None

                for key in keys_to_try:
                    try:
                        data = bytes(self.cmd.mf1_read_one_block(block, key_type, key))
                        blocks_read += 1
                        break
                    except Exception:
                        continue

                if data is None:
                    blocks_failed += 1
                    if args.show_all:
                        print(
                            f" {block:03d} | {'?? ' * 16:48s} |         | {color_string((CR, 'AUTH FAIL'))}"
                        )
                    continue

                is_val, value, addr = self._is_value_block(data)
                hex_str = " ".join(f"{b:02X}" for b in data)

                if is_val:
                    value_blocks_found.append((block, value, addr, data))
                    print(
                        f" {block:03d} | {hex_str:48s} | {value:>7d} | "
                        f"{color_string((CG, f'VALUE (adr={addr})'))}"
                    )
                    if args.export:
                        args.export.write(
                            f"block={block} value={value} addr={addr} data={data.hex()}\n"
                        )
                elif args.show_all:
                    print(f" {block:03d} | {hex_str:48s} |         | data")

        print(
            "-----+--------------------------------------------------+---------+----------"
        )

        print("\n [*] Scan complete:")
        print(f"     Blocks read:   {color_string((CG, str(blocks_read)))}")
        print(f"     Blocks failed: {color_string((CR, str(blocks_failed)))}")
        print(f"     Value blocks:  {color_string((CG, str(len(value_blocks_found))))}")

        if value_blocks_found:
            print(f"\n [*] {color_string((CG, 'Value block summary:'))}")
            for block, value, addr, _ in value_blocks_found:
                print(
                    f"     Block {block:03d}: value = {color_string((CG, str(value)))}, addr = {addr}"
                )

        if args.export:
            args.export.close()
            print(" [*] Results exported.")


@hw.command("fuzzer")
class HWFuzzer(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = (
            "Easy NFC fuzzer wrapper (runs hf 14a fuzz with sensible defaults)"
        )
        parser.add_argument(
            "-m",
            "--mode",
            type=str,
            choices=["random", "cmd", "auth", "ats", "uid"],
            default="random",
            help="Fuzz mode (default: random)",
        )
        parser.add_argument(
            "-n",
            "--count",
            type=int,
            default=25,
            metavar="<dec>",
            help="Number of fuzz iterations (default: 25)",
        )
        parser.add_argument(
            "-t",
            "--timeout",
            type=int,
            default=120,
            metavar="<dec>",
            help="Response timeout in ms (default: 120)",
        )
        parser.add_argument(
            "-d",
            "--delay",
            type=float,
            default=0.05,
            metavar="<float>",
            help="Delay between fuzz attempts in seconds (default: 0.05)",
        )
        parser.add_argument(
            "--min-len",
            type=int,
            default=1,
            metavar="<dec>",
            help="Minimum payload length (default: 1)",
        )
        parser.add_argument(
            "--max-len",
            type=int,
            default=16,
            metavar="<dec>",
            help="Maximum payload length (default: 16)",
        )
        parser.add_argument(
            "--log",
            type=argparse.FileType("w", encoding="utf8"),
            help="Optional log file",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not self.cmd.is_device_reader_mode():
            self.cmd.set_device_reader_mode(True)
            print(f" [*] {color_string((CG, 'Switched to Reader mode'))}")

        fuzz_unit = HF14AFuzz()
        fuzz_unit.device_com = self.device_com

        fuzz_args = argparse.Namespace(
            mode=args.mode,
            count=args.count,
            min_len=args.min_len,
            max_len=args.max_len,
            timeout=args.timeout,
            log=args.log,
            delay=args.delay,
        )
        fuzz_unit.on_exec(fuzz_args)


# =====================================================================================
# Auto Scan - LF then HF
# =====================================================================================


@hw.command("autoscan")
class HWAutoScan(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = (
            "Auto switch to reader mode, scan LF cards (EM410x, HID, Viking), "
            "then scan HF cards (ISO14443-A). Reports all found tags."
        )
        parser.add_argument(
            "--lf-only",
            action="store_true",
            help="Only scan LF, skip HF",
        )
        parser.add_argument(
            "--hf-only",
            action="store_true",
            help="Only scan HF, skip LF",
        )
        parser.add_argument(
            "-r",
            "--repeat",
            type=int,
            default=1,
            metavar="<dec>",
            help="Number of scan cycles (default: 1, 0=continuous until Ctrl+C)",
        )
        parser.add_argument(
            "-d",
            "--delay",
            type=float,
            default=1.0,
            metavar="<float>",
            help="Delay between scan cycles in seconds (default: 1.0)",
        )
        return parser

    def on_exec(self, args: argparse.Namespace):
        # Step 1: Auto-switch to reader mode
        if not self.cmd.is_device_reader_mode():
            self.cmd.set_device_reader_mode(True)
            print(f" [*] {color_string((CG, 'Switched to Reader mode'))}")
        else:
            print(" [*] Already in Reader mode")

        scan_lf = not args.hf_only
        scan_hf = not args.lf_only
        repeat = args.repeat
        cycle = 0

        print(f" [*] {color_string((CY, 'Auto Scan starting...'))}")
        print(
            f" [*] Scanning: {'LF' if scan_lf else ''}{' + ' if scan_lf and scan_hf else ''}{'HF' if scan_hf else ''}"
        )
        if repeat == 0:
            print(" [*] Continuous mode - press Ctrl+C to stop\n")
        else:
            print(f" [*] Scan cycles: {repeat}\n")

        try:
            while repeat == 0 or cycle < repeat:
                cycle += 1
                if repeat != 1:
                    print(f" {'=' * 60}")
                    print(f" [*] {color_string((CY, f'Scan cycle {cycle}'))}")
                    print(f" {'=' * 60}")

                # ---- HF Scanning ----
                if scan_hf:
                    print(f"\n {'-' * 40}")
                    print(f" [*] {color_string((CC, 'HF Scan (13.56 MHz)'))}")
                    print(f" {'-' * 40}")

                    self._switch_to_hf_antenna()
                    self._scan_hf14a()

                # ---- LF Scanning ----
                if scan_lf:
                    print(f"\n {'-' * 40}")
                    print(f" [*] {color_string((CC, 'LF Scan (125 kHz)'))}")
                    print(f" {'-' * 40}")

                    self._switch_to_lf_antenna()

                    # EM410x
                    self._scan_em410x()

                    # HID Prox
                    self._scan_hid()

                    # Viking
                    self._scan_viking()

                if repeat == 0 or cycle < repeat:
                    time.sleep(args.delay)

        except KeyboardInterrupt:
            print(f"\n [*] {color_string((CY, 'Scan stopped by user'))}")

        print(f"\n [*] {color_string((CG, 'Auto scan complete.'))}")

    def _prepare_hf_antenna(self):
        try:
            # LF commands may leave the RF frontend in a transient state; pulse HF field and send REQA once.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 1,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 1,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=30,
                data=[0x26],
                bitlen=7,
            )
        except Exception:
            pass
        time.sleep(0.05)

    def _switch_to_lf_antenna(self):
        try:
            # Ensure HF field is dropped before LF scans.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 0,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 0,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=20,
                data=[],
            )
        except Exception:
            pass
        time.sleep(0.03)

    def _switch_to_hf_antenna(self):
        try:
            # Drop any previous HF state before re-activating for a clean LF->HF handoff.
            self.cmd.hf14a_raw(
                options={
                    "activate_rf_field": 0,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": 0,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=20,
                data=[],
            )
        except Exception:
            pass
        time.sleep(0.03)
        self._prepare_hf_antenna()

    def _scan_em410x(self):
        print("   [*] EM410x... ", end="", flush=True)
        try:
            resp = self.cmd.em410x_scan()
            if resp is not None:
                tag_type, uid = resp
                uid_hex = uid.hex().upper()
                print(f"{color_string((CG, 'FOUND!'))}")
                print(f"       Type : {TagSpecificType(tag_type)}")
                print(f"       UID  : {color_string((CG, uid_hex))}")

                # Convert to decimal card number
                uid_int = int.from_bytes(uid, byteorder="big")
                print(f"       Dec  : {uid_int}")

                # Facility code + card number (common 26-bit format)
                if len(uid) == 5:
                    fc = (uid[2] << 1) | (uid[3] >> 7)
                    cn = ((uid[3] & 0x7F) << 8) | uid[4]
                    print(f"       FC   : {fc}  CN: {cn}")
                return True
            else:
                print(f"{color_string((CY, 'no tag'))}")
        except Exception:
            print(f"{color_string((CY, 'no tag'))}")
        return False

    def _scan_hid(self):
        print("   [*] HID Prox... ", end="", flush=True)
        try:
            resp = self.cmd.hidprox_scan(0)
            if resp is not None:
                print(f"{color_string((CG, 'FOUND!'))}")
                # resp is tuple from struct.unpack('>BIBIBH', data[:13])
                fmt, fc, cn, _il, _card_len, _raw = (
                    resp[0],
                    resp[1],
                    resp[2],
                    resp[3],
                    resp[4],
                    resp[5],
                )
                print(f"       Format: {fmt}")
                print(f"       FC    : {fc}")
                print(f"       CN    : {cn}")
                return True
            else:
                print(f"{color_string((CY, 'no tag'))}")
        except Exception:
            print(f"{color_string((CY, 'no tag'))}")
        return False

    def _scan_viking(self):
        print("   [*] Viking... ", end="", flush=True)
        try:
            resp = self.cmd.viking_scan()
            if resp is not None:
                uid_hex = resp.hex().upper()
                print(f"{color_string((CG, 'FOUND!'))}")
                print(f"       UID  : {color_string((CG, uid_hex))}")
                return True
            else:
                print(f"{color_string((CY, 'no tag'))}")
        except Exception:
            print(f"{color_string((CY, 'no tag'))}")
        return False

    def _scan_hf14a(self):
        print("   [*] ISO14443-A... ", end="", flush=True)
        try:
            resp = None
            for attempt in range(6):
                resp = self.cmd.hf14a_scan()
                if resp is not None and len(resp) > 0:
                    break
                # Re-prime HF field between attempts to improve unstable tag wakeup.
                if attempt in (1, 3, 5):
                    self._prepare_hf_antenna()
                    self._force_hf_wakeup()
                time.sleep(0.10)

            # Final fallback: force wakeup and perform one more structured scan.
            if resp is None or len(resp) == 0:
                self._prepare_hf_antenna()
                self._force_hf_wakeup()
                resp = self.cmd.hf14a_scan()

            if resp is not None and len(resp) > 0:
                print(f"{color_string((CG, f'FOUND {len(resp)} tag(s)!'))}")
                for idx, tag in enumerate(resp):
                    if len(resp) > 1:
                        print(f"\n       --- Tag {idx + 1} ---")
                    print(
                        f"       UID  : {color_string((CG, tag['uid'].hex().upper()))}"
                    )
                    print(
                        f"       ATQA : {tag['atqa'].hex().upper()} "
                        f"(0x{int.from_bytes(tag['atqa'], byteorder='little'):04x})"
                    )
                    sak_val = tag["sak"][0]
                    print(f"       SAK  : {tag['sak'].hex().upper()}")
                    if len(tag["ats"]) > 0:
                        print(f"       ATS  : {tag['ats'].hex().upper()}")
                    if sak_val in type_id_SAK_dict:
                        print(
                            f"       Type : {color_string((CG, type_id_SAK_dict[sak_val]))}"
                        )

                    # Check for MIFARE Classic
                    if sak_val in (0x08, 0x09, 0x18, 0x19, 0x28, 0x38):
                        print(
                            f"       Note : {color_string((CY, 'MIFARE Classic detected'))}"
                        )
                        if sak_val == 0x09:
                            mfc_size = "Mini"
                        elif sak_val in (0x08, 0x28):
                            mfc_size = "1K"
                        elif sak_val == 0x19:
                            mfc_size = "2K"
                        elif sak_val in (0x18, 0x38):
                            mfc_size = "4K"
                        else:
                            mfc_size = "Unknown"
                        prng = None
                        if len(resp) == 1:
                            try:
                                if self.cmd.mf1_detect_support():
                                    prng = self.cmd.mf1_detect_prng()
                                    print(
                                        f"       PRNG : {MifareClassicPrngType(prng)}"
                                    )
                            except Exception:
                                pass
                        self._recommend_attack(sak_val, mfc_size, prng, tag)
                    elif sak_val == 0x00:
                        self._recommend_attack(sak_val, "UL", None, tag)
                    elif sak_val == 0x20:
                        self._recommend_attack(sak_val, "DESFire", None, tag)
                    else:
                        self._recommend_attack(sak_val, None, None, tag)
                return True
            else:
                print(f"{color_string((CY, 'no tag'))}")
        except Exception:
            print(f"{color_string((CY, 'no tag'))}")
        return False

    def _recommend_attack(self, sak, card_size, prng, tag):
        """Print attack recommendations based on detected card type and PRNG."""
        print(f"\n       {'-' * 44}")
        print(f"       {color_string((CC, 'RECOMMENDED ATTACK STRATEGY'))}")
        print(f"       {'-' * 44}")

        # --- MIFARE Classic ---
        if sak in (0x08, 0x09, 0x18, 0x19, 0x28, 0x38):
            size_flag = {
                "Mini": "--mini",
                "1K": "--1k",
                "2K": "--2k",
                "4K": "--4k",
            }.get(card_size, "--1k")

            if prng is not None:
                prng_type = MifareClassicPrngType(prng)

                if prng_type == MifareClassicPrngType.STATIC:
                    print(f"       Card  : MIFARE Classic {card_size} (Static PRNG)")
                    print(
                        f"       Vuln  : {color_string((CG, 'STATIC NONCE - highly vulnerable'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print(
                        "         2. If a key found, recover all keys via static nested:"
                    )
                    print(
                        f"            {color_string((CY, 'hf mf nested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print("         3. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print("         4. Scan for value blocks:")
                    print(
                        f"            {color_string((CY, f'hf mf vfind {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CG, 'FAST - seconds per key'))}"
                    )

                elif prng_type == MifareClassicPrngType.WEAK:
                    print(f"       Card  : MIFARE Classic {card_size} (Weak PRNG)")
                    print(
                        f"       Vuln  : {color_string((CG, 'WEAK PRNG - vulnerable to nested + darkside'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print("         2a. If a key found, recover remaining via nested:")
                    print(
                        f"            {color_string((CY, 'hf mf nested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print(
                        "         2b. If NO key found, use darkside to get first key:"
                    )
                    print(f"            {color_string((CY, 'hf mf darkside'))}")
                    print(
                        "         3. Then nested to recover all keys from the darkside key"
                    )
                    print("         4. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print("         5. Scan for value blocks:")
                    print(
                        f"            {color_string((CY, f'hf mf vfind {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CG, 'FAST - seconds to minutes'))}"
                    )

                elif prng_type == MifareClassicPrngType.HARD:
                    print(f"       Card  : MIFARE Classic {card_size} (Hard PRNG)")
                    print(
                        f"       Vuln  : {color_string((CY, 'HARDENED PRNG - requires hard nested attack'))}"
                    )
                    print("       Plan  :")
                    print("         1. Try default keys first:")
                    print(
                        f"            {color_string((CY, f'hf mf fchk {size_flag}'))}"
                    )
                    print("         2. If a key found, recover via hard nested:")
                    print(
                        f"            {color_string((CY, 'hf mf hardnested --blk 0 -a -k FFFFFFFFFFFF --tblk <target> --ta'))}"
                    )
                    print(
                        "         3. If NO default key, hard nested is still possible"
                    )
                    print("            but requires a known key from another source")
                    print("         4. Dump the card:")
                    print(
                        f"            {color_string((CY, f'hf mf dump {size_flag}'))}"
                    )
                    print(
                        f"       Speed : {color_string((CY, 'SLOW - minutes to hours per key'))}"
                    )
            else:
                print(f"       Card  : MIFARE Classic {card_size}")
                print("       Plan  :")
                print(
                    f"         1. Try default keys: {color_string((CY, f'hf mf fchk {size_flag}'))}"
                )
                print("         2. Place single tag and reconnect for PRNG detection")

        # --- MIFARE Ultralight / NTAG ---
        elif sak == 0x00:
            print("       Card  : MIFARE Ultralight / NTAG")
            print("       Plan  :")
            print(
                f"         1. Read pages:  {color_string((CY, 'hf mfu rdpg --start 0 --end 44'))}"
            )
            print("         2. Check if password-protected (NTAG21x):")
            print("            Try bruteforce or known default passwords")
            print(f"         3. Dump:  {color_string((CY, 'hf mfu dump'))}")
            print("       Note  : UL-C uses 3DES auth, EV1/NTAG use PWD_AUTH")

        # --- DESFire / MIFARE Plus ---
        elif sak == 0x20:
            ats_hex = tag["ats"].hex().upper() if len(tag["ats"]) > 0 else "N/A"
            print(f"       Card  : DESFire / MIFARE Plus (SAK=0x20, ATS={ats_hex})")
            print(
                f"       Vuln  : {color_string((CR, 'STRONG - AES/3DES crypto, no known passive attacks'))}"
            )
            print("       Plan  :")
            print("         1. Check ATS to distinguish DESFire vs MIFARE Plus")
            print("         2. DESFire: Try default app keys (all zeros)")
            print("         3. MIFARE Plus SL1: May still be Classic-compatible")
            print(f"            {color_string((CY, 'hf mf fchk --1k'))}")
            print("       Note  : Full DESFire attacks require app-level access")

        # --- Unknown SAK ---
        else:
            print(f"       Card  : Unknown type (SAK=0x{sak:02X})")
            print("       Plan  :")
            print(f"         1. Research SAK=0x{sak:02X} for this card type")
            print(f"         2. Try: {color_string((CY, 'hf 14a raw'))}")
            print("         3. Check proprietary command sets")

    def _force_hf_wakeup(self):
        try:
            options = {
                "activate_rf_field": 1,
                "wait_response": 1,
                "append_crc": 0,
                "auto_select": 0,
                "keep_rf_field": 1,
                "check_response_crc": 0,
            }
            # Try both REQA and WUPA short frames.
            for wake_cmd in (0x26, 0x52):
                resp = self.cmd.hf14a_raw(
                    options=options,
                    resp_timeout_ms=40,
                    data=[wake_cmd],
                    bitlen=7,
                )
                if resp is not None and len(resp) > 0:
                    return True
                time.sleep(0.02)
        except Exception:
            pass
        return False
