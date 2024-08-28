import binascii
import os
import re
import subprocess
import argparse
import timeit
import sys
import time
from datetime import datetime
import serial.tools.list_ports
import threading
import struct
from multiprocessing import Pool, cpu_count
from typing import Union
from pathlib import Path
from platform import uname
from datetime import datetime

import chameleon_com
import chameleon_cmd
from chameleon_utils import ArgumentParserNoExit, ArgsParserError, UnexpectedResponseError
from chameleon_utils import CLITree
from chameleon_utils import CR, CG, CB, CC, CY, C0
from chameleon_utils import print_mem_dump
from chameleon_enum import Command, Status, SlotNumber, TagSenseType, TagSpecificType
from chameleon_enum import MifareClassicWriteMode, MifareClassicPrngType, MifareClassicDarksideStatus, MfcKeyType
from chameleon_enum import AnimationMode, ButtonPressFunction, ButtonType, MfcValueBlockOperator

# NXP IDs based on https://www.nxp.com/docs/en/application-note/AN10833.pdf
type_id_SAK_dict = {0x00: "MIFARE Ultralight Classic/C/EV1/Nano | NTAG 2xx",
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


def check_tools():
    tools = ['staticnested', 'nested', 'darkside', 'mfkey32v2']
    if sys.platform == "win32":
        tools = [x+'.exe' for x in tools]
    missing_tools = [tool for tool in tools if not (default_cwd / tool).exists()]
    if len(missing_tools) > 0:
        print(f'{CR}Warning, tools {", ".join(missing_tools)} not found. '
              f'Corresponding commands will not work as intended.{C0}')


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
                self._process = subprocess.Popen(cmd, cwd=cwd, shell=True, stderr=subprocess.PIPE,
                                                 stdout=subprocess.PIPE)
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
        if super().before_exec(args):
            ret = self.cmd.is_device_reader_mode()
            if ret:
                return True
            else:
                self.cmd.set_device_reader_mode(True)
                print("Switch to {  Tag Reader  } mode successfully.")
                return True
        return False


class SlotIndexArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_slot_args(parser: ArgumentParserNoExit, mandatory=False):
        slot_choices = [x.value for x in SlotNumber]
        help_str = f"Slot Index: {slot_choices} Default: active slot"

        parser.add_argument('-s', "--slot", type=int, required=mandatory, help=help_str, metavar="<1-8>",
                            choices=slot_choices)
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
        sense_group.add_argument('--hf', action='store_true', help="HF type")
        sense_group.add_argument('--lf', action='store_true', help="LF type")
        return parser


class MF1AuthArgsUnit(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.add_argument('--blk', '--block', type=int, required=True, metavar="<dec>",
                            help="The block where the key of the card is known")
        type_group = parser.add_mutually_exclusive_group()
        type_group.add_argument('-a', '-A', action='store_true', help="Known key is A key (default)")
        type_group.add_argument('-b', '-B', action='store_true', help="Known key is B key")
        parser.add_argument('-k', '--key', type=str, required=True, metavar="<hex>", help="tag sector key")
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
        parser.add_argument('--uid', type=str, metavar="<hex>", help="Unique ID")
        parser.add_argument('--atqa', type=str, metavar="<hex>", help="Answer To Request")
        parser.add_argument('--sak', type=str, metavar="<hex>", help="Select AcKnowledge")
        ats_group = parser.add_mutually_exclusive_group()
        ats_group.add_argument('--ats', type=str, metavar="<hex>", help="Answer To Select")
        ats_group.add_argument('--delete-ats', action='store_true', help="Delete Answer To Select")
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
                print(f'{CY}Requested UID already set{C0}')
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
                print(f'{CY}Requested ATQA already set{C0}')
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
                print(f'{CY}Requested SAK already set{C0}')
        if (args.ats is not None) or args.delete_ats:
            change_requested = True
            if args.delete_ats:
                new_ats = b''
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
                print(f'{CY}Requested ATS already set{C0}')
        if anti_coll_data_changed:
            self.cmd.hf14a_set_anti_coll_data(uid, atqa, sak, ats)
        return change_requested, anti_coll_data_changed, uid, atqa, sak, ats


class MFUAuthArgsUnit(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()

        def key_parser(key: str) -> bytes:
            try:
                key = bytes.fromhex(key)
            except:
                raise ValueError("Key should be a hex string")
            
            if len(key) not in [4, 16]:
                raise ValueError("Key should either be 4 or 16 bytes long")
            elif len(key) == 16:
                raise ValueError("Ultralight-C authentication isn't supported yet")

            return key
        
        parser.add_argument(
            '-k', '--key', type=key_parser, metavar="<hex>", help="Authentication key (EV1/NTAG 4 bytes)."
        )
        parser.add_argument('-l', action='store_true', dest='swap_endian', help="Swap endianness of the key.")

        return parser

    def get_param(self, args):
        key = args.key

        if key is not None and args.swap_endian:
            key = bytearray(key)
            for i in range(len(key)):
                tmp = key[i]
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
        parser.add_argument("--id", type=str, required=required, help="EM410x tag id", metavar="<hex>")
        return parser

    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            if args.id is not None:
                if not re.match(r"^[a-fA-F0-9]{10}$", args.id):
                    raise ArgsParserError("ID must include 10 HEX symbols")
            return True
        return False

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class TagTypeArgsUnit(DeviceRequiredUnit):
    @staticmethod
    def add_type_args(parser: ArgumentParserNoExit):
        type_names = [t.name for t in TagSpecificType.list()]
        help_str = "Tag Type: " + ", ".join(type_names)
        parser.add_argument('-t', "--type", type=str, required=True, metavar="TAG_TYPE",
                            help=help_str, choices=type_names)
        return parser

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


root = CLITree(root=True)
hw = root.subgroup('hw', 'Hardware-related commands')
hw_slot = hw.subgroup('slot', 'Emulation slots commands')
hw_settings = hw.subgroup('settings', 'Chameleon settings commands')

hf = root.subgroup('hf', 'High Frequency commands')
hf_14a = hf.subgroup('14a', 'ISO14443-a commands')
hf_mf = hf.subgroup('mf', 'MIFARE Classic commands')
hf_mfu = hf.subgroup('mfu', 'MIFARE Ultralight / NTAG commands')

lf = root.subgroup('lf', 'Low Frequency commands')
lf_em = lf.subgroup('em', 'EM commands')
lf_em_410x = lf_em.subgroup('410x', 'EM410x commands')


@root.command('clear')
class RootClear(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Clear screen'
        return parser

    def on_exec(self, args: argparse.Namespace):
        os.system('clear' if os.name == 'posix' else 'cls')


@root.command('rem')
class RootRem(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Timestamped comment'
        parser.add_argument('comment', nargs='*', help='Your comment')
        return parser

    def on_exec(self, args: argparse.Namespace):
        # precision: second
        # iso_timestamp = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')
        # precision: nanosecond (note that the comment will take some time too, ~75ns, check your system)
        iso_timestamp = datetime.utcnow().isoformat() + 'Z'
        comment = ' '.join(args.comment)
        print(f"{iso_timestamp} remark: {comment}")


@root.command('exit')
class RootExit(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Exit client'
        return parser

    def on_exec(self, args: argparse.Namespace):
        print("Bye, thank you.  ^.^ ")
        self.device_com.close()
        sys.exit(996)


@root.command('dump_help')
class RootDumpHelp(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Dump available commands'
        parser.add_argument('-d', '--show-desc', action='store_true', help="Dump full command description")
        parser.add_argument('-g', '--show-groups', action='store_true', help="Dump command groups as well")
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
                cmd_title = f"{CG}{cmd_node.fullname}{C0}"
                print(f"{cmd_title}".ljust(col1_width), end="")
                p.prog = " " * (visual_col1_width - len("usage: ") - 1)
                usage = p.format_usage().removeprefix("usage: ").strip()
                print(f"{CY}{usage}{C0}")
        else:
            if dump_cmd_groups and not cmd_node.root:
                if dump_description:
                    print("=" * 80)
                    print(f"{CR}{cmd_node.fullname}{C0}\n")
                    print(f"{CC}{cmd_node.help_text}{C0}\n")
                else:
                    print(f"{CB}== {cmd_node.fullname} =={C0}")
            for child in cmd_node.children:
                RootDumpHelp.dump_help(child, depth + 1, dump_cmd_groups, dump_description)

    def on_exec(self, args: argparse.Namespace):
        self.dump_help(root, dump_cmd_groups=args.show_groups, dump_description=args.show_desc)


@hw.command('connect')
class HWConnect(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Connect to chameleon by serial port'
        parser.add_argument('-p', '--port', type=str, required=False)
        return parser

    def on_exec(self, args: argparse.Namespace):
        try:
            if args.port is None:  # Chameleon auto-detect if no port is supplied
                platform_name = uname().release
                if 'Microsoft' in platform_name:
                    path = os.environ["PATH"].split(os.pathsep)
                    path.append("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/")
                    powershell_path = None
                    for prefix in path:
                        fn = os.path.join(prefix, "powershell.exe")
                        if not os.path.isdir(fn) and os.access(fn, os.X_OK):
                            powershell_path = fn
                            break
                    if powershell_path:
                        process = subprocess.Popen([powershell_path,
                                                    "Get-PnPDevice -Class Ports -PresentOnly |"
                                                    " where {$_.DeviceID -like '*VID_6868&PID_8686*'} |"
                                                    " Select-Object -First 1 FriendlyName |"
                                                    " % FriendlyName |"
                                                    " select-string COM\\d+ |"
                                                    "% { $_.matches.value }"], stdout=subprocess.PIPE)
                        res = process.communicate()[0]
                        _comport = res.decode('utf-8').strip()
                        if _comport:
                            args.port = _comport.replace('COM', '/dev/ttyS')
                else:
                    # loop through all ports and find chameleon
                    for port in serial.tools.list_ports.comports():
                        if port.vid == 0x6868:
                            args.port = port.device
                            break
                if args.port is None:  # If no chameleon was found, exit
                    print("Chameleon not found, please connect the device or try connecting manually with the -p flag.")
                    return
            self.device_com.open(args.port)
            self.device_com.commands = self.cmd.get_device_capabilities()
            major, minor = self.cmd.get_app_version()
            model = ['Ultra', 'Lite'][self.cmd.get_device_model()]
            print(f" {{ Chameleon {model} connected: v{major}.{minor} }}")

        except Exception as e:
            print(f"{CR}Chameleon Connect fail: {str(e)}{C0}")
            self.device_com.close()


@hw.command('disconnect')
class HWDisconnect(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Disconnect chameleon'
        return parser

    def on_exec(self, args: argparse.Namespace):
        self.device_com.close()


@hw.command('mode')
class HWMode(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get or change device mode: tag reader or tag emulator'
        mode_group = parser.add_mutually_exclusive_group()
        mode_group.add_argument('-r', '--reader', action='store_true', help="Set reader mode")
        mode_group.add_argument('-e', '--emulator', action='store_true', help="Set emulator mode")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.reader:
            self.cmd.set_device_reader_mode(True)
            print("Switch to {  Tag Reader  } mode successfully.")
        elif args.emulator:
            self.cmd.set_device_reader_mode(False)
            print("Switch to { Tag Emulator } mode successfully.")
        else:
            print(f"- Device Mode ( Tag {'Reader' if self.cmd.is_device_reader_mode() else 'Emulator'} )")


@hw.command('chipid')
class HWChipId(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get device chipset ID'
        return parser

    def on_exec(self, args: argparse.Namespace):
        print(' - Device chip ID: ' + self.cmd.get_device_chip_id())


@hw.command('address')
class HWAddress(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get device address (used with Bluetooth)'
        return parser

    def on_exec(self, args: argparse.Namespace):
        print(' - Device address: ' + self.cmd.get_device_address())


@hw.command('version')
class HWVersion(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get current device firmware version'
        return parser

    def on_exec(self, args: argparse.Namespace):
        fw_version_tuple = self.cmd.get_app_version()
        fw_version = f'v{fw_version_tuple[0]}.{fw_version_tuple[1]}'
        git_version = self.cmd.get_git_version()
        model = ['Ultra', 'Lite'][self.cmd.get_device_model()]
        print(f' - Chameleon {model}, Version: {fw_version} ({git_version})')


@hf_14a.command('scan')
class HF14AScan(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Scan 14a tag, and print basic information'
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
        int_sak = data_tag['sak'][0]
        if int_sak in type_id_SAK_dict:
            print(f"- Guessed type(s) from SAK: {type_id_SAK_dict[int_sak]}")

    def scan(self, deep=False):
        resp = self.cmd.hf14a_scan()
        if resp is not None:
            for data_tag in resp:
                print(f"- UID  : {data_tag['uid'].hex().upper()}")
                print(f"- ATQA : {data_tag['atqa'].hex().upper()} "
                      f"(0x{int.from_bytes(data_tag['atqa'], byteorder='little'):04x})")
                print(f"- SAK  : {data_tag['sak'].hex().upper()}")
                if len(data_tag['ats']) > 0:
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


@hf_14a.command('info')
class HF14AInfo(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Scan 14a tag, and print detail information'
        return parser

    def on_exec(self, args: argparse.Namespace):
        scan = HF14AScan()
        scan.device_com = self.device_com
        scan.scan(deep=True)


@hf_mf.command('nested')
class HFMFNested(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Mifare Classic nested recover key'
        parser.add_argument('--blk', '--known-block', type=int, required=True, metavar="<dec>",
                            help="Known key block number")
        srctype_group = parser.add_mutually_exclusive_group()
        srctype_group.add_argument('-a', '-A', action='store_true', help="Known key is A key (default)")
        srctype_group.add_argument('-b', '-B', action='store_true', help="Known key is B key")
        parser.add_argument('-k', '--key', type=str, required=True, metavar="<hex>", help="Known key")
        # tblk required because only single block mode is supported for now
        parser.add_argument('--tblk', '--target-block', type=int, required=True, metavar="<dec>",
                            help="Target key block number")
        dsttype_group = parser.add_mutually_exclusive_group()
        dsttype_group.add_argument('--ta', '--tA', action='store_true', help="Target A key (default)")
        dsttype_group.add_argument('--tb', '--tB', action='store_true', help="Target B key")
        return parser

    def from_nt_level_code_to_str(self, nt_level):
        if nt_level == 0:
            return 'StaticNested'
        if nt_level == 1:
            return 'Nested'
        if nt_level == 2:
            return 'HardNested'

    def recover_a_key(self, block_known, type_known, key_known, block_target, type_target) -> Union[str, None]:
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
        print(f" - NT vulnerable: {CY}{ self.from_nt_level_code_to_str(nt_level) }{C0}")
        if nt_level == 2:
            print(" [!] HardNested has not been implemented yet.")
            return None

        # acquire
        if nt_level == 0:  # It's a staticnested tag?
            nt_uid_obj = self.cmd.mf1_static_nested_acquire(
                block_known, type_known, key_known, block_target, type_target)
            cmd_param = f"{nt_uid_obj['uid']} {int(type_target)}"
            for nt_item in nt_uid_obj['nts']:
                cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']}"
            tool_name = "staticnested"
        else:
            dist_obj = self.cmd.mf1_detect_nt_dist(block_known, type_known, key_known)
            nt_obj = self.cmd.mf1_nested_acquire(block_known, type_known, key_known, block_target, type_target)
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
            msg = f"   [ Time elapsed {process.get_time_distance()/1000:#.1f}s ]\r"
            print(msg, end="")
            time.sleep(0.1)
        # clear \r
        print()

        if process.get_ret_code() == 0:
            output_str = process.get_output_sync()
            key_list = []
            for line in output_str.split('\n'):
                sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                if sea_obj is not None:
                    key_list.append(sea_obj[1])
            # Here you have to verify the password first, and then get the one that is successfully verified
            # If there is no verified password, it means that the recovery failed, you can try again
            print(f" - [{len(key_list)} candidate key(s) found ]")
            for key in key_list:
                key_bytes = bytearray.fromhex(key)
                if self.cmd.mf1_auth_one_key_block(block_target, type_target, key_bytes):
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
            print(f"{CR}Target key already known{C0}")
            return
        print(f" - {C0}Nested recover one key running...{C0}")
        key = self.recover_a_key(block_known, type_known, key_known_bytes, block_target, type_target)
        if key is None:
            print(f"{CY}No key found, you can retry.{C0}")
        else:
            print(f" - Block {block_target} Type {type_target.name} Key Found: {CG}{key}{C0}")
        return


@hf_mf.command('darkside')
class HFMFDarkside(ReaderRequiredUnit):
    def __init__(self):
        super().__init__()
        self.darkside_list = []

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Mifare Classic darkside recover key'
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
            darkside_resp = self.cmd.mf1_darkside_acquire(block_target, type_target, first_recover, 30)
            first_recover = False  # not first run.
            if darkside_resp[0] != MifareClassicDarksideStatus.OK:
                print(f"Darkside error: {MifareClassicDarksideStatus(darkside_resp[0])}")
                break
            darkside_obj = darkside_resp[1]

            if darkside_obj['par'] != 0:  # NXP tag workaround.
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
            if 'key not found' in output_str:
                print(f" - No key found, retrying({retry_count})...")
                retry_count += 1
                continue  # retry
            else:
                key_list = []
                for line in output_str.split('\n'):
                    sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                    if sea_obj is not None:
                        key_list.append(sea_obj[1])
                # auth key
                for key in key_list:
                    key_bytes = bytearray.fromhex(key)
                    if self.cmd.mf1_auth_one_key_block(block_target, type_target, key_bytes):
                        return key
        return None

    def on_exec(self, args: argparse.Namespace):
        key = self.recover_key(0x03, MfcKeyType.A)
        if key is not None:
            print(f" - Key Found: {key}")
        else:
            print(" - Key recover fail.")
        return


@hf_mf.command('fchk')
class HFMFFCHK(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()

        mifare_type_group = parser.add_mutually_exclusive_group()
        mifare_type_group.add_argument('--mini', help='MIFARE Classic Mini / S20', action='store_const', dest='maxSectors', const=5)
        mifare_type_group.add_argument('--1k', help='MIFARE Classic 1k / S50 (default)', action='store_const', dest='maxSectors', const=16)
        mifare_type_group.add_argument('--2k', help='MIFARE Classic/Plus 2k', action='store_const', dest='maxSectors', const=32)
        mifare_type_group.add_argument('--4k', help='MIFARE Classic 4k / S70', action='store_const', dest='maxSectors', const=40)

        parser.add_argument(dest='keys', help='Key (as hex[12] format)', metavar='<hex>', type=str, nargs='*')
        parser.add_argument('--key', dest='import_key', type=argparse.FileType('rb'), help='Read keys from .key format file')
        parser.add_argument('--dic', dest='import_dic', type=argparse.FileType('r', encoding='utf8'), help='Read keys from .dic format file')

        parser.add_argument('--export-key', type=argparse.FileType('wb'), help=f'Export result as .key format, file will be {CR}OVERWRITTEN{C0} if exists')
        parser.add_argument('--export-dic', type=argparse.FileType('w', encoding='utf8'), help=f'Export result as .dic format, file will be {CR}OVERWRITTEN{C0} if exists')

        parser.add_argument('-m', '--mask', help='Which sectorKey to be skip, 1 bit per sectorKey. `0b1` represent to skip to check. (in hex[20] format)', type=str, default='00000000000000000000', metavar='<hex>')

        parser.set_defaults(maxSectors=16)
        return parser
    
    def check_keys(self, mask: bytearray, keys: list[bytes], chunkSize=20):
        sectorKeys = dict()

        for i in range(0, len(keys), chunkSize):
            # print("mask = {}".format(mask.hex(sep=' ', bytes_per_sep=1)))
            chunkKeys = keys[i:i+chunkSize]
            print(f' - progress of checking keys... {CY}{i}{C0} / {len(keys)} ({CY}{100 * i / len(keys):.1f}{C0} %)')
            resp = self.cmd.mf1_check_keys_of_sectors(mask, chunkKeys)
            # print(resp)

            if resp["status"] != Status.HF_TAG_OK:
                print(f' - check interrupted, reason: {CR}{str(Status(resp["status"]))}{C0}')
                break
            elif 'sectorKeys' not in resp:
                print(f' - check interrupted, reason: {CG}All sectorKey is found or masked{C0}')
                break

            for j in range(10):
                mask[j] |= resp['found'][j]
            sectorKeys.update(resp['sectorKeys'])

        return sectorKeys

    def on_exec(self, args: argparse.Namespace):
        # print(args)

        keys = set()

        # keys from args
        for key in args.keys:
            if not re.match(r'^[a-fA-F0-9]{12}$', key):
                print(f' - {CR}Key should in hex[12] format, invalid key is ignored{C0}, key = "{key}"')
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
            print(f' - {CR}No keys{C0}')
            return

        print(f" - loaded {CG}{len(keys)}{C0} keys")

        # mask
        if not re.match(r'^[a-fA-F0-9]{1,20}$', args.mask):
            print(f' - {CR}mask should in hex[20] format{C0}, mask = "{args.mask}"')
            return
        mask = bytearray.fromhex(f'{args.mask:0<20}')
        for i in range(args.maxSectors, 40):
            mask[i // 4] |= 3 << (6 - i % 4 * 2)

        # check keys
        startedAt = datetime.now()
        sectorKeys = self.check_keys(mask, list(keys))
        endedAt = datetime.now()
        duration = endedAt - startedAt
        print(f" - elapsed time: {CY}{duration.total_seconds():.3f}s{C0}")

        if args.export_key is not None:
            unknownkey = bytes(6)
            for sectorNo in range(args.maxSectors):
                args.export_key.write(sectorKeys.get(2 * sectorNo, unknownkey))
                args.export_key.write(sectorKeys.get(2 * sectorNo + 1, unknownkey))
            print(f" - result exported to: {CG}{args.export_key.name}{C0} (as .key format)")

        if args.export_dic is not None:
            uniq_result = set(sectorKeys.values())
            for key in uniq_result:
                args.export_dic.write(key.hex().upper() + '\n')
            print(f" - result exported to: {CG}{args.export_dic.name}{C0} (as .dic format)")

        # print sectorKeys
        print(f"\n - {CG}result of key checking:{C0}\n")
        print("-----+-----+--------------+---+--------------+----")
        print(" Sec | Blk | key A        |res| key B        |res ")
        print("-----+-----+--------------+---+--------------+----")
        for sectorNo in range(args.maxSectors):
            blk = (sectorNo * 4 + 3) if sectorNo < 32 else (sectorNo * 16 - 369)
            keyA = sectorKeys.get(2 * sectorNo, None)
            keyA = f"{CG}{keyA.hex().upper()}{C0} | {CG}1{C0}" if keyA else f"{CR}------------{C0} | {CR}0{C0}"
            keyB = sectorKeys.get(2 * sectorNo + 1, None)
            keyB = f"{CG}{keyB.hex().upper()}{C0} | {CG}1{C0}" if keyB else f"{CR}------------{C0} | {CR}0{C0}"
            print(f" {CY}{sectorNo:03d}{C0} | {blk:03d} | {keyA} | {keyB} ")
        print("-----+-----+--------------+---+--------------+----")
        print(f"( {CR}0{C0}: Failed, {CG}1{C0}: Success )\n\n")


@hf_mf.command('rdbl')
class HFMFRDBL(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'Mifare Classic read one block'
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        resp = self.cmd.mf1_read_one_block(param.block, param.type, param.key)
        print(f" - Data: {resp.hex()}")


@hf_mf.command('wrbl')
class HFMFWRBL(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'Mifare Classic write one block'
        parser.add_argument('-d', '--data', type=str, required=True, metavar="<hex>",
                            help="Your block data, as hex string.")
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        if not re.match(r"^[a-fA-F0-9]{32}$", args.data):
            raise ArgsParserError("Data must include 32 HEX symbols")
        data = bytearray.fromhex(args.data)
        resp = self.cmd.mf1_write_one_block(param.block, param.type, param.key, data)
        if resp:
            print(f" - {CG}Write done.{C0}")
        else:
            print(f" - {CR}Write fail.{C0}")

@hf_mf.command('view')
class HFMFView(MF1AuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Display content from tag memory or dump file'
        mifare_type_group = parser.add_mutually_exclusive_group()
        mifare_type_group.add_argument('--mini', help='MIFARE Classic Mini / S20', action='store_const', dest='maxSectors', const=5)
        mifare_type_group.add_argument('--1k', help='MIFARE Classic 1k / S50 (default)', action='store_const', dest='maxSectors', const=16)
        mifare_type_group.add_argument('--2k', help='MIFARE Classic/Plus 2k', action='store_const', dest='maxSectors', const=32)
        mifare_type_group.add_argument('--4k', help='MIFARE Classic 4k / S70', action='store_const', dest='maxSectors', const=40)
        parser.add_argument('-d', '--dump-file', required=False, type=argparse.FileType("rb"), help="Dump file to read")
        parser.add_argument('-k', '--key-file', required=False, type=argparse.FileType("r"), help="File containing keys of tag to write (exported with fchk --export)")
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
                raise ArgsParserError(f"Invalid key file. Found {len(keys)}, expected {args.maxSectors}")
            # iterate over blocks
            for blk in range(0, args.maxSectors * 4):
                resp = None
                try:
                    # first try with key B
                    resp = self.cmd.mf1_read_one_block(blk, MfcKeyType.B, keys[blk//4][1])
                except UnexpectedResponseError:
                    # ignore read errors at this stage as we want to try key A
                    pass
                if not resp:
                    # try with key A if B was unsuccessful
                    # this will raise an exception if key A fails too
                    resp = self.cmd.mf1_read_one_block(blk, MfcKeyType.A, keys[blk//4][0])
                data.extend(resp)
        else:
            raise ArgsParserError("Missing args. Specify --dump-file (-d) or --key-file (-k)")
        print_mem_dump(data,16)

@hf_mf.command('value')
class HFMFVALUE(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'MIFARE Classic value block commands'

        operator_group = parser.add_mutually_exclusive_group()
        operator_group.add_argument('--get', action='store_true', help="get value from src block")
        operator_group.add_argument('--set', type=int, required=False, metavar="<dec>",
                            help="set value X (-2147483647 ~ 2147483647) to src block")
        operator_group.add_argument('--inc', type=int, required=False, metavar="<dec>",
                            help="increment value by X (0 ~ 2147483647) from src to dst")
        operator_group.add_argument('--dec', type=int, required=False, metavar="<dec>",
                            help="decrement value by X (0 ~ 2147483647) from src to dst")
        operator_group.add_argument('--res', '--cp', action='store_true', help="copy value from src to dst (Restore and Transfer)")

        parser.add_argument('--blk', '--src-block', type=int, required=True, metavar="<dec>",
                            help="block number of src")
        srctype_group = parser.add_mutually_exclusive_group()
        srctype_group.add_argument('-a', '-A', action='store_true', help="key of src is A key (default)")
        srctype_group.add_argument('-b', '-B', action='store_true', help="key of src is B key")
        parser.add_argument('-k', '--src-key', type=str, required=True, metavar="<hex>", help="key of src")

        parser.add_argument('--tblk', '--dst-block', type=int, metavar="<dec>",
                            help="block number of dst (default to src)")
        dsttype_group = parser.add_mutually_exclusive_group()
        dsttype_group.add_argument('--ta', '--tA', action='store_true', help="key of dst is A key (default to src)")
        dsttype_group.add_argument('--tb', '--tB', action='store_true', help="key of dst is B key (default to src)")
        parser.add_argument('--tkey', '--dst-key', type=str, metavar="<hex>", help="key of dst (default to src)")

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
        dst_type = MfcKeyType.A if args.ta is not False else (MfcKeyType.B if args.tb is not False else src_type)
        dst_key = args.tkey if args.tkey is not None else args.src_key
        if not re.match(r"^[a-fA-F0-9]{12}$", dst_key):
            print("dst_key must include 12 HEX symbols")
            return
        dst_key = bytearray.fromhex(dst_key)
        # print(dst_blk, dst_type, dst_key)
        
        if args.inc is not None:
            self.inc_value(src_blk, src_type, src_key, args.inc, dst_blk, dst_type, dst_key)
            return
        elif args.dec is not None:
            self.dec_value(src_blk, src_type, src_key, args.dec, dst_blk, dst_type, dst_key)
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
            print(f" - {CR}Invalid value of value block: {resp.hex()}{C0}")
            return
        if (adr1 != adr3) or (adr2 != adr4) or (adr1 + adr2 != 0xFF):
            print(f" - {CR}Invalid address of value block: {resp.hex()}{C0}")
            return
        print(f" - block[{block}] = {CG}{{ value: {val1}, adr: {adr1} }}{C0}")

    def set_value(self, block, type, key, value):
        if value < -2147483647 or value > 2147483647:
            raise ArgsParserError(f"Set value must be between -2147483647 and 2147483647. Got {value}")
        adr_inverted = 0xFF - block
        data = struct.pack("<iiiBBBB", value, -value - 1, value, block, adr_inverted, block, adr_inverted)
        resp = self.cmd.mf1_write_one_block(block, type, key, data)
        if resp:
            print(f" - {CG}Set done.{C0}")
            self.get_value(block, type, key)
        else:
            print(f" - {CR}Set fail.{C0}")

    def inc_value(self, src_blk, src_type, src_key, value, dst_blk, dst_type, dst_key):
        if value < 0 or value > 2147483647:
            raise ArgsParserError(f"Increment value must be between 0 and 2147483647. Got {value}")
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk, src_type, src_key, 
            MfcValueBlockOperator.INCREMENT, value,
            dst_blk, dst_type, dst_key
        )
        if resp:
            print(f" - {CG}Increment done.{C0}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {CR}Increment fail.{C0}")
    
    def dec_value(self, src_blk, src_type, src_key, value, dst_blk, dst_type, dst_key):
        if value < 0 or value > 2147483647:
            raise ArgsParserError(f"Decrement value must be between 0 and 2147483647. Got {value}")
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk, src_type, src_key, 
            MfcValueBlockOperator.DECREMENT, value,
            dst_blk, dst_type, dst_key
        )
        if resp:
            print(f" - {CG}Decrement done.{C0}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {CR}Decrement fail.{C0}")

    def res_value(self, src_blk, src_type, src_key, dst_blk, dst_type, dst_key):
        resp = self.cmd.mf1_manipulate_value_block(
            src_blk, src_type, src_key, 
            MfcValueBlockOperator.RESTORE, 0,
            dst_blk, dst_type, dst_key
        )
        if resp:
            print(f" - {CG}Restore done.{C0}")
            self.get_value(dst_blk, dst_type, dst_key)
        else:
            print(f" - {CR}Restore fail.{C0}")


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
    def __init__(self, rs, i=0, j=1):
        self.rs = rs
        self.i = 0
        self.j = 1
        self.found = set()
        self.keys = set()

    def __iter__(self):
        return self

    def __next__(self):
        try:
            item_i = self.rs[self.i]
        except IndexError:
            raise StopIteration
        if self.key_from_item(item_i) in self.found:
            self.i += 1
            self.j = self.i + 1
            return next(self)
        try:
            item_j = self.rs[self.j]
        except IndexError:
            self.i += 1
            self.j = self.i + 1
            return next(self)
        self.j += 1
        if self.key_from_item(item_j) in self.found:
            return next(self)
        return item_i, item_j

    @staticmethod
    def key_from_item(item):
        return "{uid}-{nt}-{nr}-{ar}".format(**item)

    def key_found(self, key, items):
        self.keys.add(key)
        for item in items:
            try:
                if item == self.rs[self.i]:
                    self.i += 1
                    self.j = self.i + 1
            except IndexError:
                break
        self.found.update(self.key_from_item(item) for item in items)


@hf_mf.command('elog')
class HFMFELog(DeviceRequiredUnit):
    detection_log_size = 18

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'MF1 Detection log count/decrypt'
        parser.add_argument('--decrypt', action='store_true', help="Decrypt key from MF1 log list")
        return parser

    def decrypt_by_list(self, rs: list):
        """
            Decrypt key from reconnaissance log list

        :param rs:
        :return:
        """
        msg1 = f"  > {len(rs)} records => "
        msg2 = f"/{(len(rs)*(len(rs)-1))//2} combinations. "
        msg3 = " key(s) found"
        n = 1
        gen = ItemGenerator(rs)
        with Pool(cpu_count()) as pool:
            for result in pool.imap(_run_mfkey32v2, gen):
                # TODO: if some keys already recovered, test them on item before running mfkey32 on item
                if result is not None:
                    gen.key_found(*result)
                print(f"{msg1}{n}{msg2}{len(gen.keys)}{msg3}\r", end="")
                n += 1
        print()
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
            print("."*recv_count, end="")
        print()
        print(f" - Download done ({len(result_list)} records), start parse and decrypt")
        # classify
        result_maps = {}
        for item in result_list:
            uid = item['uid']
            if uid not in result_maps:
                result_maps[uid] = {}
            block = item['block']
            if block not in result_maps[uid]:
                result_maps[uid][block] = {}
            type = item['type']
            if type not in result_maps[uid][block]:
                result_maps[uid][block][type] = []

            result_maps[uid][block][type].append(item)

        for uid in result_maps.keys():
            print(f" - Detection log for uid [{uid.upper()}]")
            result_maps_for_uid = result_maps[uid]
            for block in result_maps_for_uid:
                print(f"  > Block {block} detect log decrypting...")
                if 'A' in result_maps_for_uid[block]:
                    # print(f" - A record: { result_maps[block]['A'] }")
                    records = result_maps_for_uid[block]['A']
                    if len(records) > 1:
                        result_maps[uid][block]['A'] = self.decrypt_by_list(records)
                    else:
                        print(f"  > {len(records)} record")
                if 'B' in result_maps_for_uid[block]:
                    # print(f" - B record: { result_maps[block]['B'] }")
                    records = result_maps_for_uid[block]['B']
                    if len(records) > 1:
                        result_maps[uid][block]['B'] = self.decrypt_by_list(records)
                    else:
                        print(f"  > {len(records)} record")
            print("  > Result ---------------------------")
            for block in result_maps_for_uid.keys():
                if 'A' in result_maps_for_uid[block]:
                    print(f"  > Block {block}, A key result: {result_maps_for_uid[block]['A']}")
                if 'B' in result_maps_for_uid[block]:
                    print(f"  > Block {block}, B key result: {result_maps_for_uid[block]['B']}")
        return


@hf_mf.command('eload')
class HFMFELoad(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Load data to emulator memory'
        self.add_slot_args(parser)
        parser.add_argument('-f', '--file', type=str, required=True, help="file path")
        parser.add_argument('-t', '--type', type=str, required=False, help="content type", choices=['bin', 'hex'])
        return parser

    def on_exec(self, args: argparse.Namespace):
        file = args.file
        if args.type is None:
            if file.endswith('.bin'):
                content_type = 'bin'
            elif file.endswith('.eml'):
                content_type = 'hex'
            else:
                raise Exception("Unknown file format, Specify content type with -t option")
        else:
            content_type = args.type
        buffer = bytearray()

        with open(file, mode='rb') as fd:
            if content_type == 'bin':
                buffer.extend(fd.read())
            if content_type == 'hex':
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
            block_data = buffer[index: index + 16*max_blocks]
            n_blocks = len(block_data) // 16
            index += 16*n_blocks
            # load to device
            self.cmd.mf1_write_emu_block_data(block, block_data)
            print('.'*n_blocks, end='')
            block += n_blocks
        print("\n - Load success")


@hf_mf.command('esave')
class HFMFESave(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Read data from emulator memory'
        self.add_slot_args(parser)
        parser.add_argument('-f', '--file', type=str, required=True, help="file path")
        parser.add_argument('-t', '--type', type=str, required=False, help="content type", choices=['bin', 'hex'])
        return parser

    def on_exec(self, args: argparse.Namespace):
        file = args.file
        if args.type is None:
            if file.endswith('.bin'):
                content_type = 'bin'
            elif file.endswith('.eml'):
                content_type = 'hex'
            else:
                raise Exception("Unknown file format, Specify content type with -t option")
        else:
            content_type = args.type

        selected_slot = self.cmd.get_active_slot()
        slot_info = self.cmd.get_slot_info()
        tag_type = TagSpecificType(slot_info[selected_slot]['hf'])
        if tag_type == TagSpecificType.MIFARE_Mini:
            block_count = 20
        elif tag_type == TagSpecificType.MIFARE_1024:
            block_count = 64
        elif tag_type == TagSpecificType.MIFARE_2048:
            block_count = 128
        elif tag_type == TagSpecificType.MIFARE_4096:
            block_count = 256
        else:
            raise Exception("Card in current slot is not Mifare Classic/Plus in SL1 mode")

        index = 0
        data = bytearray(0)
        max_blocks = self.device_com.data_max_length // 16
        while block_count > 0:
            chunk_count = min(block_count, max_blocks)
            data.extend(self.cmd.mf1_read_emu_block_data(index, chunk_count))
            index += chunk_count
            block_count -= chunk_count
            print('.'*chunk_count, end='')

        with open(file, 'wb') as fd:
            if content_type == 'hex':
                for i in range(len(data) // 16):
                    fd.write(binascii.hexlify(data[i*16:(i+1)*16])+b'\n')
            else:
                fd.write(data)
        print("\n - Read success")

@hf_mf.command('eview')
class HFMFEView(SlotIndexArgsAndGoUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'View data from emulator memory'
        self.add_slot_args(parser)
        return parser
    
    def on_exec(self, args: argparse.Namespace):
        selected_slot = self.cmd.get_active_slot()
        slot_info = self.cmd.get_slot_info()
        tag_type = TagSpecificType(slot_info[selected_slot]['hf'])      
        
        if tag_type == TagSpecificType.MIFARE_Mini:
            block_count = 20
        elif tag_type == TagSpecificType.MIFARE_1024:
            block_count = 64
        elif tag_type == TagSpecificType.MIFARE_2048:
            block_count = 128
        elif tag_type == TagSpecificType.MIFARE_4096:
            block_count = 256
        else:
            raise Exception("Card in current slot is not Mifare Classic/Plus in SL1 mode")
        index = 0
        data = bytearray(0)
        max_blocks = self.device_com.data_max_length // 16
        while block_count > 0:
            # read all the blocks
            chunk_count = min(block_count, max_blocks)
            data.extend(self.cmd.mf1_read_emu_block_data(index, chunk_count))
            index += chunk_count
            block_count -= chunk_count
        print_mem_dump(data,16)

@hf_mf.command('econfig')
class HFMFEConfig(SlotIndexArgsAndGoUnit, HF14AAntiCollArgsUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Settings of Mifare Classic emulator'
        self.add_slot_args(parser)
        self.add_hf14a_anticoll_args(parser)
        gen1a_group = parser.add_mutually_exclusive_group()
        gen1a_group.add_argument('--enable-gen1a', action='store_true', help="Enable Gen1a magic mode")
        gen1a_group.add_argument('--disable-gen1a', action='store_true', help="Disable Gen1a magic mode")
        gen2_group = parser.add_mutually_exclusive_group()
        gen2_group.add_argument('--enable-gen2', action='store_true', help="Enable Gen2 magic mode")
        gen2_group.add_argument('--disable-gen2', action='store_true', help="Disable Gen2 magic mode")
        block0_group = parser.add_mutually_exclusive_group()
        block0_group.add_argument('--enable-block0', action='store_true',
                                  help="Use anti-collision data from block 0 for 4 byte UID tags")
        block0_group.add_argument('--disable-block0', action='store_true', help="Use anti-collision data from settings")
        write_names = [w.name for w in MifareClassicWriteMode.list()]
        help_str = "Write Mode: " + ", ".join(write_names)
        parser.add_argument('--write', type=str, help=help_str, metavar="MODE", choices=write_names)
        log_group = parser.add_mutually_exclusive_group()
        log_group.add_argument('--enable-log', action='store_true', help="Enable logging of MFC authentication data")
        log_group.add_argument('--disable-log', action='store_true', help="Disable logging of MFC authentication data")
        return parser

    def on_exec(self, args: argparse.Namespace):
        # collect current settings
        anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
        if len(anti_coll_data) == 0:
            print(f"{CR}Slot {self.slot_num} does not contain any HF 14A config{C0}")
            return
        uid = anti_coll_data['uid']
        atqa = anti_coll_data['atqa']
        sak = anti_coll_data['sak']
        ats = anti_coll_data['ats']
        slotinfo = self.cmd.get_slot_info()
        fwslot = SlotNumber.to_fw(self.slot_num)
        hf_tag_type = TagSpecificType(slotinfo[fwslot]['hf'])
        if hf_tag_type not in [
            TagSpecificType.MIFARE_Mini,
            TagSpecificType.MIFARE_1024,
            TagSpecificType.MIFARE_2048,
            TagSpecificType.MIFARE_4096,
        ]:
            print(f"{CR}Slot {self.slot_num} not configured as MIFARE Classic{C0}")
            return
        mfc_config = self.cmd.mf1_get_emulator_config()
        gen1a_mode = mfc_config["gen1a_mode"]
        gen2_mode = mfc_config["gen2_mode"]
        block_anti_coll_mode = mfc_config["block_anti_coll_mode"]
        write_mode = MifareClassicWriteMode(mfc_config["write_mode"])
        detection = mfc_config["detection"]
        change_requested, change_done, uid, atqa, sak, ats = self.update_hf14a_anticoll(args, uid, atqa, sak, ats)
        if args.enable_gen1a:
            change_requested = True
            if not gen1a_mode:
                gen1a_mode = True
                self.cmd.mf1_set_gen1a_mode(gen1a_mode)
                change_done = True
            else:
                print(f'{CY}Requested gen1a already enabled{C0}')
        elif args.disable_gen1a:
            change_requested = True
            if gen1a_mode:
                gen1a_mode = False
                self.cmd.mf1_set_gen1a_mode(gen1a_mode)
                change_done = True
            else:
                print(f'{CY}Requested gen1a already disabled{C0}')
        if args.enable_gen2:
            change_requested = True
            if not gen2_mode:
                gen2_mode = True
                self.cmd.mf1_set_gen2_mode(gen2_mode)
                change_done = True
            else:
                print(f'{CY}Requested gen2 already enabled{C0}')
        elif args.disable_gen2:
            change_requested = True
            if gen2_mode:
                gen2_mode = False
                self.cmd.mf1_set_gen2_mode(gen2_mode)
                change_done = True
            else:
                print(f'{CY}Requested gen2 already disabled{C0}')
        if args.enable_block0:
            change_requested = True
            if not block_anti_coll_mode:
                block_anti_coll_mode = True
                self.cmd.mf1_set_block_anti_coll_mode(block_anti_coll_mode)
                change_done = True
            else:
                print(f'{CY}Requested block0 anti-coll mode already enabled{C0}')
        elif args.disable_block0:
            change_requested = True
            if block_anti_coll_mode:
                block_anti_coll_mode = False
                self.cmd.mf1_set_block_anti_coll_mode(block_anti_coll_mode)
                change_done = True
            else:
                print(f'{CY}Requested block0 anti-coll mode already disabled{C0}')
        if args.write is not None:
            change_requested = True
            new_write_mode = MifareClassicWriteMode[args.write]
            if new_write_mode != write_mode:
                write_mode = new_write_mode
                self.cmd.mf1_set_write_mode(write_mode)
                change_done = True
            else:
                print(f'{CY}Requested write mode already set{C0}')
        if args.enable_log:
            change_requested = True
            if not detection:
                detection = True
                self.cmd.mf1_set_detection_enable(detection)
                change_done = True
            else:
                print(f'{CY}Requested logging of MFC authentication data already enabled{C0}')
        elif args.disable_log:
            change_requested = True
            if detection:
                detection = False
                self.cmd.mf1_set_detection_enable(detection)
                change_done = True
            else:
                print(f'{CY}Requested logging of MFC authentication data already disabled{C0}')

        if change_done:
            print(' - MF1 Emulator settings updated')
        if not change_requested:
            print(f'- {"Type:":40}{CY}{hf_tag_type}{C0}')
            print(f'- {"UID:":40}{CY}{uid.hex().upper()}{C0}')
            print(f'- {"ATQA:":40}{CY}{atqa.hex().upper()} '
                  f'(0x{int.from_bytes(atqa, byteorder="little"):04x}){C0}')
            print(f'- {"SAK:":40}{CY}{sak.hex().upper()}{C0}')
            if len(ats) > 0:
                print(f'- {"ATS:":40}{CY}{ats.hex().upper()}{C0}')
            print(
                f'- {"Gen1A magic mode:":40}{f"{CG}enabled{C0}" if gen1a_mode else f"{CR}disabled{C0}"}')
            print(
                f'- {"Gen2 magic mode:":40}{f"{CG}enabled{C0}" if gen2_mode else f"{CR}disabled{C0}"}')
            print(
                f'- {"Use anti-collision data from block 0:":40}'
                f'{f"{CG}enabled{C0}" if block_anti_coll_mode else f"{CR}disabled{C0}"}')
            try:
                print(f'- {"Write mode:":40}{CY}{MifareClassicWriteMode(write_mode)}{C0}')
            except ValueError:
                print(f'- {"Write mode:":40}{CR}invalid value!{C0}')
            print(
                f'- {"Log (mfkey32) mode:":40}{f"{CG}enabled{C0}" if detection else f"{CR}disabled{C0}"}')


@hf_mfu.command('ercnt')
class HFMFUVERSION(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Read MIFARE Ultralight / NTAG counter value.'
        parser.add_argument('-c', '--counter', type=int, required=True, help="Counter index.")
        return parser

    def on_exec(self, args: argparse.Namespace):
        value, no_tearing = self.cmd.mfu_read_emu_counter_data(args.counter)
        print(f" - Value: {value:06x}")
        if no_tearing:
            print(f" - Tearing: {CG}not set{C0}")
        else:
            print(f" - Tearing: {CR}set{C0}")


@hf_mfu.command('ewcnt')
class HFMFUVERSION(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Read MIFARE Ultralight / NTAG counter value.'
        parser.add_argument('-c', '--counter', type=int, required=True, help="Counter index.")
        parser.add_argument('-v', '--value', type=int, required=True, help="Counter value (24-bit).")
        parser.add_argument('-t', '--reset-tearing', action='store_true', help="Reset tearing event flag.")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.value > 0xFFFFFF:
            print(f"{CR}Counter value {args.value:#x} is too large.{C0}")
            return

        self.cmd.mfu_write_emu_counter_data(args.counter, args.value, args.reset_tearing)

        print('- Ok')


@hf_mfu.command('rdpg')
class HFMFURDPG(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'MIFARE Ultralight / NTAG read one page'
        parser.add_argument('-p', '--page', type=int, required=True, metavar="<dec>",
                            help="The page where the key will be used against")
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 0,
            'check_response_crc': 1,
        }

        if param.key is not None:
            options['keep_rf_field'] = 1
            try:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x1B)+param.key)

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception as e:
                # failed auth may cause tags to be lost
                failed_auth = True

            options['keep_rf_field'] = 0
            options['auto_select'] = 0
        else:
            failed_auth = False

        if not failed_auth:
            resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x30, args.page))
            print(f" - Data: {resp[:4].hex()}")
        else:
            try:
                self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x30, args.page))
            except:
                # we may lose the tag again here
                pass
            print(f" {CR}- Auth failed{C0}")


@hf_mfu.command('wrpg')
class HFMFUWRPG(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'MIFARE Ultralight / NTAG write one page'
        parser.add_argument('-p', '--page', type=int, required=True, metavar="<dec>",
                            help="The index of the page to write to.")
        parser.add_argument('-d', '--data', type=bytes.fromhex, required=True, metavar="<hex>",
                            help="Your page data, as a 4 byte (8 character) hex string.")
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        data = args.data
        if len(data) != 4:
            print(f"{CR}Page data should be a 4 byte (8 character) hex string{C0}")
            return

        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 0,
            'check_response_crc': 0,
        }
        
        if param.key is not None:
            options['keep_rf_field'] = 1
            options['check_response_crc'] = 1
            try:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x1B)+param.key)

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception as e:
                # failed auth may cause tags to be lost
                failed_auth = True

            options['keep_rf_field'] = 0
            options['auto_select'] = 0
            options['check_response_crc'] = 0
        else:
            failed_auth = False
        
        if not failed_auth:
            resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0xA2, args.page)+data)

            if resp[0] == 0x0A:
                print(f" - Ok")
            else:
                print(f"{CR}Write failed ({resp[0]:#04x}).{C0}")
        else:
            # send a command just to disable the field. use read to avoid corrupting the data
            try:
                self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x30, args.page))
            except:
                # we may lose the tag again here
                pass
            print(f" {CR}- Auth failed{C0}")


@hf_mfu.command('eview')
class HFMFUEVIEW(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'MIFARE Ultralight / NTAG view emulator data'
        return parser

    def get_param(self, args):
        class Param:
            def __init__(self):
                pass

        return Param()

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        nr_pages = self.cmd.mfu_get_emu_pages_count()
        page = 0
        while page < nr_pages:
            count = min(nr_pages - page, 16)
            data = self.cmd.mfu_read_emu_page_data(page, count)
            for i in range(0, len(data), 4):
                print(f"#{page+(i>>2):02x}: {data[i:i+4].hex()}")
            page += count


@hf_mfu.command('eload')
class HFMFUELOAD(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'MIFARE Ultralight / NTAG load emulator data'
        parser.add_argument(
            '-f', '--file', required=True, type=str, help="File to load data from."
        )
        parser.add_argument(
            '-t', '--type', type=str, required=False, help="Force writing as either raw binary or hex.", choices=['bin', 'hex']
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
            if args.file.endswith('.eml') or args.file.endswith('.txt'):
                file_type = 'hex'
            else:
                file_type = 'bin'
        
        if file_type == 'hex':
            with open(args.file, 'r') as f:
                data = f.read()
            data = re.sub('#.*$', '', data, flags=re.MULTILINE)
            data = bytes.fromhex(data)
        else:
            with open(args.file, 'rb') as f:
                data = f.read()

        # this will throw an exception on incorrect slot type
        nr_pages = self.cmd.mfu_get_emu_pages_count()
        size = nr_pages * 4
        if len(data) > size:
            print(f"{CR}Dump file is too large for the current slot (expected {size} bytes).{C0}")
            return
        elif (len(data) % 4) > 0:
            print(f"{CR}Dump file's length is not a multiple of 4 bytes.{C0}")
            return
        elif len(data) < size:
            print(f"{CY}Dump file is smaller than the current slot's memory ({len(data)} < {size}).{C0}")
        
        nr_pages = len(data) >> 2
        page = 0
        while page < nr_pages:
            offset = page * 4
            cur_count = min(16, nr_pages - page)

            if offset >= len(data):
                page_data = bytes.fromhex("00000000") * cur_count
            else:
                page_data = data[offset:offset + 4 * cur_count]
            
            self.cmd.mfu_write_emu_page_data(page, page_data)
            page += cur_count
        
        print(f" - Ok")


@hf_mfu.command('esave')
class HFMFUESAVE(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'MIFARE Ultralight / NTAG save emulator data'
        parser.add_argument(
            '-f', '--file', required=True, type=str, help='File to save data to.'
        )
        parser.add_argument(
            '-t', '--type', type=str, required=False, help="Force writing as either raw binary or hex.", choices=['bin', 'hex']
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
            if args.file.endswith('.eml') or args.file.endswith('.txt'):
                file_type = 'hex'
            else:
                file_type = 'bin'

        if file_type == 'hex':
            fd = open(args.file, 'w+')
            save_as_eml = True
        else:
            fd = open(args.file, 'wb+')

        with fd:
            # this will throw an exception on incorrect slot type
            nr_pages = self.cmd.mfu_get_emu_pages_count()

            fd.truncate(0)
            
            # write version and signature as comments if saving as .eml
            if save_as_eml:
                try:
                    version = self.cmd.mf0_ntag_get_version_data()

                    fd.write(f"# Version: {version.hex()}\n")
                except:
                    pass # slot does not have version data
                
                try:
                    signature = self.cmd.mf0_ntag_get_signature_data()

                    if signature != b"\x00" * 32:
                        fd.write(f"# Signature: {signature.hex()}\n")
                except:
                    pass # slot does not have signature data
            
            page = 0
            while page < nr_pages:
                cur_count = min(32, nr_pages - page)
                
                data = self.cmd.mfu_read_emu_page_data(page, cur_count)
                if save_as_eml:
                    for i in range(0, len(data), 4):
                        fd.write(data[i:i+4].hex() + "\n")
                else:
                    fd.write(data)
                
                page += cur_count
        
        print(f" - Ok")


@hf_mfu.command('rcnt')
class HFMFURCNT(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'MIFARE Ultralight / NTAG read counter'
        parser.add_argument('-c', '--counter', type=int, required=True, metavar="<dec>",
                            help="Index of the counter to read (always 0 for NTAG, 0-2 for Ultralight EV1).")
        return parser

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 0,
            'check_response_crc': 1,
        }
        
        if param.key is not None:
            options['keep_rf_field'] = 1
            try:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x1B)+param.key)

                failed_auth = len(resp) < 2
                if not failed_auth:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception as e:
                # failed auth may cause tags to be lost
                failed_auth = True

            options['keep_rf_field'] = 0
            options['auto_select'] = 0
        else:
            failed_auth = False
        
        if not failed_auth:
            resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x39, args.counter))
            print(f" - Data: {resp[:3].hex()}")
        else:
            try:
                self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x39, args.counter))
            except:
                # we may lose the tag again here
                pass
            print(f" {CR}- Auth failed{C0}")


@hf_mfu.command('dump')
class HFMFUDUMP(MFUAuthArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = super().args_parser()
        parser.description = 'MIFARE Ultralight dump pages'
        parser.add_argument('-p', '--page', type=int, required=False, metavar="<dec>", default=0,
                            help="Manually set number of pages to dump")
        parser.add_argument('-q', '--qty', type=int, required=False, metavar="<dec>",
                            help="Manually set number of pages to dump")
        parser.add_argument('-f', '--file', type=str, required=False, default="",
                            help="Specify a filename for dump file")
        parser.add_argument('-t', '--type', type=str, required=False, choices=['bin', 'hex'], 
                            help="Force writing as either raw binary or hex.")
        return parser
    
    def do_dump(self, args: argparse.Namespace, param, fd, save_as_eml):
        if args.qty is not None:
            stop_page = min(args.page + args.qty, 256)
        else:
            stop_page = None
        
        tags = self.cmd.hf14a_scan()
        if len(tags) > 1:
            print(f'- {CR}Collision detected, leave only one tag.{C0}')
            return
        elif len(tags) == 0:
            print(f'- {CR}No tag detected.{C0}')
            return
        elif tags[0]['atqa'] != b'\x44\x00' or tags[0]['sak'] != b'\x00':
            print(f'- {CR}Tag is not Mifare Ultralight compatible (ATQA {tags[0]["atqa"].hex()} SAK {tags[0]["sak"].hex()}).{C0}')
            return
        
        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 1,
            'check_response_crc': 1,
        }
        
        # if stop page isn't set manually, try autodetection
        if stop_page is None:
            tag_name = None

            # first try sending the GET_VERSION command
            try:
                version = self.cmd.hf14a_raw(options=options, resp_timeout_ms=100, data=struct.pack('!B', 0x60))
                if len(version) == 0:
                    version = None
            except:
                version = None
            
            # try sending AUTHENTICATE command and observe the result
            try:
                supports_auth = len(self.cmd.hf14a_raw(options=options, resp_timeout_ms=100, data=struct.pack('!B', 0x1A))) != 0
            except:
                supports_auth = False
            
            if version is not None and not supports_auth:
                # either ULEV1 or NTAG
                assert len(version) == 8

                is_mikron_ulev1 = version[1] == 0x34 and version[2] == 0x21
                if (version[2] == 3 or is_mikron_ulev1) and version[4] == 1 and version[5] == 0:
                    # Ultralight EV1 V0
                    size_map = {
                        0x0B: ('Mifare Ultralight EV1 48b', 20),
                        0x0E: ('Mifare Ultralight EV1 128b', 41),
                    }
                elif version[2] == 4 and version[4] == 1 and version[5] == 0:
                    # NTAG 210/212/213/215/216 V0
                    size_map = {
                        0x0B: ('NTAG 210', 20),
                        0x0E: ('NTAG 212', 41),
                        0x0F: ('NTAG 213', 45),
                        0x11: ('NTAG 215', 135),
                        0x13: ('NTAG 216', 231),
                    }
                else:
                    size_map = {}
                
                if version[6] in size_map:
                    tag_name, stop_page = size_map[version[6]]
            elif version is None and supports_auth:
                # Ultralight C
                tag_name = 'Mifare Ultralight C'
                stop_page = 48
            elif version is None and not supports_auth:
                try:
                    # Invalid command returning a NAK means that's some old type of NTAG.
                    self.cmd.hf14a_raw(options=options, resp_timeout_ms=100, data=struct.pack('!B', 0xFF))

                    print(f' - {CY}Tag is likely NTAG 20x, reading until first error.{C0}')
                    stop_page = 256
                except:
                    # Regular Ultralight
                    tag_name = 'Mifare Ultralight'
                    stop_page = 16
            else:
                # This is probably Ultralight AES, but we don't support this one yet.
                pass
            
            if tag_name is not None:
                print(f' - Detected tag type as {tag_name}.')

            if stop_page is None:
                print(f' - {CY}Couldn\'t autodetect the expected card size, reading until first error.{C0}')
                stop_page = 256
        
        needs_stop = False

        if param.key is not None:
            try:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x1B)+param.key)

                needs_stop = len(resp) < 2
                if not needs_stop:
                    print(f" - PACK: {resp[:2].hex()}")
            except Exception as e:
                # failed auth may cause tags to be lost
                needs_stop = True

            options['auto_select'] = 0
        
        # this handles auth failure
        if needs_stop:
            print(f" - {CR}Auth failed{C0}")
            if fd is not None:
                fd.close()
                fd = None

        for i in range(args.page, stop_page):
            # this could be done once in theory but the command would need to be optimized properly
            if param.key is not None and not needs_stop:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x1B)+param.key)
                options['auto_select'] = 0  # prevent resets
                pack = resp[:2].hex()
            
            # disable the rf field after the last command
            if i == (stop_page - 1) or needs_stop:
                options['keep_rf_field'] = 0

            try:
                resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x30, i))
            except:
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
            options['auto_select'] = 0

            # TODO: can be optimized as we get 4 pages at once but beware of wrapping
            # in case of end of memory or LOCK on ULC and no key provided
            data = resp[:4]
            print(f" - Page {i:2}: {data.hex()}")
            if fd is not None:
                if save_as_eml:
                    fd.write(data.hex()+'\n')
                else:
                    fd.write(data)
        
        if needs_stop and stop_page != 256:
            print(f' - {CY}Dump is shorter than expected.{C0}')
        if args.file != '':
            print(f" - {CG}Dump written in {args.file}.{C0}")

    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)

        file_type = args.type
        fd = None
        save_as_eml = False

        if args.file != '':
            if file_type is None:
                if args.file.endswith('.eml') or args.file.endswith('.txt'):
                    file_type = 'hex'
                else:
                    file_type = 'bin'

            if file_type == 'hex':
                fd = open(args.file, 'w+')
                save_as_eml = True
            else:
                fd = open(args.file, 'wb+')

        if fd is not None:
            with fd:
                fd.truncate(0)
                self.do_dump(args, param, fd, save_as_eml)
        else:
            self.do_dump(args, param, fd, save_as_eml)


@hf_mfu.command('version')
class HFMFUVERSION(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Request MIFARE Ultralight / NTAG version data.'
        return parser

    def on_exec(self, args: argparse.Namespace):
        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 0,
            'check_response_crc': 1,
        }

        resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!B', 0x60))
        print(f" - Data: {resp[:8].hex()}")


@hf_mfu.command('signature')
class HFMFUSIGNATURE(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Request MIFARE Ultralight / NTAG ECC signature data.'
        return parser

    def on_exec(self, args: argparse.Namespace):
        options = {
            'activate_rf_field': 0,
            'wait_response': 1,
            'append_crc': 1,
            'auto_select': 1,
            'keep_rf_field': 0,
            'check_response_crc': 1,
        }

        resp = self.cmd.hf14a_raw(options=options, resp_timeout_ms=200, data=struct.pack('!BB', 0x3C, 0x00))
        print(f" - Data: {resp[:32].hex()}")


@hf_mfu.command('econfig')
class HFMFUEConfig(SlotIndexArgsAndGoUnit, HF14AAntiCollArgsUnit, DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Settings of Mifare Ultralight / NTAG emulator'
        self.add_slot_args(parser)
        self.add_hf14a_anticoll_args(parser)
        uid_magic_group = parser.add_mutually_exclusive_group()
        uid_magic_group.add_argument('--enable-uid-magic', action='store_true', help="Enable UID magic mode")
        uid_magic_group.add_argument('--disable-uid-magic', action='store_true', help="Disable UID magic mode")
        parser.add_argument('--set-version', type=bytes.fromhex, help="Set data to be returned by the GET_VERSION command.")
        parser.add_argument('--set-signature', type=bytes.fromhex, help="Set data to be returned by the READ_SIG command.")
        parser.add_argument('--reset-auth-cnt', action='store_true', help="Resets the counter of unsuccessful authentication attempts.")
        return parser

    def on_exec(self, args: argparse.Namespace):
        aux_data_changed = False
        aux_data_change_requested = False
        
        if args.set_version is not None:
            aux_data_change_requested = True
            aux_data_changed = True

            if len(args.set_version) != 8:
                print(f"{CR}Version data should be 8 bytes long.{C0}")
                return
            
            try:
                self.cmd.mf0_ntag_set_version_data(args.set_version)
            except:
                print(f"{CR}Tag type does not support GET_VERSION command.{C0}")
                return

        if args.set_signature is not None:
            aux_data_change_requested = True
            aux_data_changed = True

            if len(args.set_signature) != 32:
                print(f"{CR}Signature data should be 32 bytes long.{C0}")
                return
            
            try:
                self.cmd.mf0_ntag_set_signature_data(args.set_signature)
            except:
                print(f"{CR}Tag type does not support READ_SIG command.{C0}")
                return
        
        if args.reset_auth_cnt:
            aux_data_change_requested = True
            old_value = self.cmd.mfu_reset_auth_cnt()
            if old_value != 0:
                aux_data_changed = True
                print(f"- Unsuccessful auth counter has been reset from {old_value} to 0.")

        # collect current settings
        anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
        if len(anti_coll_data) == 0:
            print(f"{CR}Slot {self.slot_num} does not contain any HF 14A config{C0}")
            return
        uid = anti_coll_data['uid']
        atqa = anti_coll_data['atqa']
        sak = anti_coll_data['sak']
        ats = anti_coll_data['ats']
        slotinfo = self.cmd.get_slot_info()
        fwslot = SlotNumber.to_fw(self.slot_num)
        hf_tag_type = TagSpecificType(slotinfo[fwslot]['hf'])
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
            print(f"{CR}Slot {self.slot_num} not configured as MIFARE Ultralight / NTAG{C0}")
            return
        change_requested, change_done, uid, atqa, sak, ats = self.update_hf14a_anticoll(args, uid, atqa, sak, ats)

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

        if change_done or aux_data_changed:
            print(' - MFU/NTAG Emulator settings updated')
        if not (change_requested or aux_data_change_requested):
            print(f'- {"Type:":40}{CY}{hf_tag_type}{C0}')
            print(f'- {"UID:":40}{CY}{uid.hex().upper()}{C0}')
            print(f'- {"ATQA:":40}{CY}{atqa.hex().upper()} '
                  f'(0x{int.from_bytes(atqa, byteorder="little"):04x}){C0}')
            print(f'- {"SAK:":40}{CY}{sak.hex().upper()}{C0}')
            if len(ats) > 0:
                print(f'- {"ATS:":40}{CY}{ats.hex().upper()}{C0}')
            if magic_mode: 
                print(f'- {"UID Magic:":40}{CY}enabled{C0}')
            else:
                print(f'- {"UID Magic:":40}{CY}disabled{C0}')
            
            try:
                version = self.cmd.mf0_ntag_get_version_data()
                print(f'- {"Version:":40}{CY}{version.hex().upper()}{C0}')
            except:
                pass
            
            try:
                signature = self.cmd.mf0_ntag_get_signature_data()
                print(f'- {"Signature:":40}{CY}{signature.hex().upper()}{C0}')
            except:
                pass


@lf_em_410x.command('read')
class LFEMRead(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Scan em410x tag and print id'
        return parser

    def on_exec(self, args: argparse.Namespace):
        id = self.cmd.em410x_scan()
        print(f" - EM410x ID(10H): {CG}{id.hex()}{C0}")


@lf_em_410x.command('write')
class LFEM410xWriteT55xx(LFEMIdArgsUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Write em410x id to t55xx'
        return self.add_card_arg(parser, required=True)

    def before_exec(self, args: argparse.Namespace):
        b1 = super(LFEMIdArgsUnit, self).before_exec(args)
        b2 = super(ReaderRequiredUnit, self).before_exec(args)
        return b1 and b2

    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytes.fromhex(id_hex)
        self.cmd.em410x_write_to_t55xx(id_bytes)
        print(f" - EM410x ID(10H): {id_hex} write done.")


@hw_slot.command('list')
class HWSlotList(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get information about slots'
        parser.add_argument('--short', action='store_true',
                            help="Hide slot nicknames and Mifare Classic emulator settings")
        return parser

    def get_slot_name(self, slot, sense):
        try:
            name = self.cmd.get_slot_tag_nick(slot, sense)
            return {'baselen': len(name), 'metalen': len(CC+C0), 'name': f'{CC}{name}{C0}'}
        except UnexpectedResponseError:
            return {'baselen': 0, 'metalen': 0, 'name': ''}
        except UnicodeDecodeError:
            name = "UTF8 Err"
            return {'baselen': len(name), 'metalen': len(CC+C0), 'name': f'{CC}{name}{C0}'}

    def on_exec(self, args: argparse.Namespace):
        slotinfo = self.cmd.get_slot_info()
        selected = SlotNumber.from_fw(self.cmd.get_active_slot())
        current = selected
        enabled = self.cmd.get_enabled_slots()
        maxnamelength = 0
        slotnames = []
        for slot in SlotNumber:
            hfn = self.get_slot_name(slot, TagSenseType.HF)
            lfn = self.get_slot_name(slot, TagSenseType.LF)
            m = max(hfn['baselen'], lfn['baselen'])
            maxnamelength = m if m > maxnamelength else maxnamelength
            slotnames.append({'hf': hfn, 'lf': lfn})
        for slot in SlotNumber:
            fwslot = SlotNumber.to_fw(slot)
            hf_tag_type = TagSpecificType(slotinfo[fwslot]['hf'])
            lf_tag_type = TagSpecificType(slotinfo[fwslot]['lf'])
            print(f' - {f"Slot {slot}:":{4+maxnamelength+1}}'
                  f'{f"({CG}active{C0})" if slot == selected else ""}')

            # HF
            field_length = maxnamelength+slotnames[fwslot]["hf"]["metalen"]+1
            print(f'   HF: '
                  f'{slotnames[fwslot]["hf"]["name"]:{field_length}}', end='')
            print(f'{f"({CR}disabled{C0}) " if not enabled[fwslot]["hf"] else ""}', end='')
            if hf_tag_type != TagSpecificType.UNDEFINED:
                print(f"{CY if enabled[fwslot]['hf'] else C0}{hf_tag_type}{C0}")
            else:
                print("undef")
            if (not args.short) and enabled[fwslot]['hf']:
                if current != slot:
                    self.cmd.set_active_slot(slot)
                    current = slot
                anti_coll_data = self.cmd.hf14a_get_anti_coll_data()
                uid = anti_coll_data['uid']
                atqa = anti_coll_data['atqa']
                sak = anti_coll_data['sak']
                ats = anti_coll_data['ats']
                # print('    - ISO14443A emulator settings:')
                print(f'      {"UID:":40}{CY}{uid.hex().upper()}{C0}')
                print(f'      {"ATQA:":40}{CY}{atqa.hex().upper()} '
                      f'(0x{int.from_bytes(atqa, byteorder="little"):04x}){C0}')
                print(f'      {"SAK:":40}{CY}{sak.hex().upper()}{C0}')
                if len(ats) > 0:
                    print(f'      {"ATS:":40}{CY}{ats.hex().upper()}{C0}')
                if hf_tag_type in [
                    TagSpecificType.MIFARE_Mini,
                    TagSpecificType.MIFARE_1024,
                    TagSpecificType.MIFARE_2048,
                    TagSpecificType.MIFARE_4096,
                ]:
                    config = self.cmd.mf1_get_emulator_config()
                    # print('    - Mifare Classic emulator settings:')
                    print(
                        f'      {"Gen1A magic mode:":40}'
                        f'{f"{CG}enabled{C0}" if config["gen1a_mode"] else f"{CR}disabled{C0}"}')
                    print(
                        f'      {"Gen2 magic mode:":40}'
                        f'{f"{CG}enabled{C0}" if config["gen2_mode"] else f"{CR}disabled{C0}"}')
                    print(
                        f'      {"Use anti-collision data from block 0:":40}'
                        f'{f"{CG}enabled{C0}" if config["block_anti_coll_mode"] else f"{CR}disabled{C0}"}')
                    try:
                        print(f'      {"Write mode:":40}{CY}'
                              f'{MifareClassicWriteMode(config["write_mode"])}{C0}')
                    except ValueError:
                        print(f'      {"Write mode:":40}{CR}invalid value!{C0}')
                    print(
                        f'      {"Log (mfkey32) mode:":40}'
                        f'{f"{CG}enabled{C0}" if config["detection"] else f"{CR}disabled{C0}"}')

            # LF
            field_length = maxnamelength+slotnames[fwslot]["lf"]["metalen"]+1
            print(f'   LF: '
                  f'{slotnames[fwslot]["lf"]["name"]:{field_length}}', end='')
            print(f'{f"({CR}disabled{C0}) " if not enabled[fwslot]["lf"] else ""}', end='')
            if lf_tag_type != TagSpecificType.UNDEFINED:
                print(f"{CY if enabled[fwslot]['lf'] else C0}{lf_tag_type}{C0}")
            else:
                print("undef")
            if (not args.short) and enabled[fwslot]['lf']:
                if current != slot:
                    self.cmd.set_active_slot(slot)
                    current = slot
                id = self.cmd.em410x_get_emu_id()
                # print('    - EM 410X emulator settings:')
                print(f'      {"ID:":40}{CY}{id.hex().upper()}{C0}')
        if current != selected:
            self.cmd.set_active_slot(selected)


@hw_slot.command('change')
class HWSlotSet(SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Set emulation tag slot activated'
        return self.add_slot_args(parser, mandatory=True)

    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd.set_active_slot(slot_index)
        print(f" - Set slot {slot_index} activated success.")


@hw_slot.command('type')
class HWSlotType(TagTypeArgsUnit, SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Set emulation tag type'
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
        print(f' - Set slot {slot_num} tag type success.')


@hw_slot.command('delete')
class HWDeleteSlotSense(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Delete sense type data for a specific slot'
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
        print(f' - Delete slot {slot_num} {sense_type.name} tag type success.')


@hw_slot.command('init')
class HWSlotInit(TagTypeArgsUnit, SlotIndexArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Set emulation tag data to default'
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
        print(' - Set slot tag data init success.')


@hw_slot.command('enable')
class HWSlotEnable(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Enable tag slot'
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
        print(f' - Enable slot {slot_num} {sense_type.name} success.')


@hw_slot.command('disable')
class HWSlotDisable(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Disable tag slot'
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
        print(f' - Disable slot {slot_num} {sense_type.name} success.')


@lf_em_410x.command('econfig')
class LFEM410xEconfig(SlotIndexArgsAndGoUnit, LFEMIdArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Set simulated em410x card id'
        self.add_slot_args(parser)
        self.add_card_arg(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.id is not None:
            self.cmd.em410x_set_emu_id(bytes.fromhex(args.id))
            print(' - Set em410x tag id success.')
        else:
            response = self.cmd.em410x_get_emu_id()
            print(' - Get em410x tag id success.')
            print(f'ID: {response.hex()}')


@hw_slot.command('nick')
class HWSlotNick(SlotIndexArgsUnit, SenseTypeArgsUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get/Set/Delete tag nick name for slot'
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        action_group = parser.add_mutually_exclusive_group()
        action_group.add_argument('-n', '--name', type=str, required=False, help="Set tag nick name for slot")
        action_group.add_argument('-d', '--delete', action='store_true', help="Delete tag nick name for slot")
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
            print(f' - Set tag nick name for slot {slot_num} {sense_type.name}: {name}')
        elif args.delete:
            self.cmd.delete_slot_tag_nick(slot_num, sense_type)
            print(f' - Delete tag nick name for slot {slot_num} {sense_type.name}')
        else:
            res = self.cmd.get_slot_tag_nick(slot_num, sense_type)
            print(f' - Get tag nick name for slot {slot_num} {sense_type.name}'
                  f': {res}')


@hw_slot.command('store')
class HWSlotUpdate(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Store slots config & data to device flash'
        return parser

    def on_exec(self, args: argparse.Namespace):
        self.cmd.slot_data_config_save()
        print(' - Store slots config and data from device memory to flash success.')


@hw_slot.command('openall')
class HWSlotOpenAll(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Open all slot and set to default data'
        return parser

    def on_exec(self, args: argparse.Namespace):
        # what type you need set to default?
        hf_type = TagSpecificType.MIFARE_1024
        lf_type = TagSpecificType.EM410X

        # set all slot
        for slot in SlotNumber:
            print(f' Slot {slot} setting...')
            # first to set tag type
            self.cmd.set_slot_tag_type(slot, hf_type)
            self.cmd.set_slot_tag_type(slot, lf_type)
            # to init default data
            self.cmd.set_slot_data_default(slot, hf_type)
            self.cmd.set_slot_data_default(slot, lf_type)
            # finally, we can enable this slot.
            self.cmd.set_slot_enable(slot, TagSenseType.HF, True)
            self.cmd.set_slot_enable(slot, TagSenseType.LF, True)
            print(f' Slot {slot} setting done.')

        # update config and save to flash
        self.cmd.slot_data_config_save()
        print(' - Succeeded opening all slots and setting data to default.')


@hw.command('dfu')
class HWDFU(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Restart application to bootloader/DFU mode'
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


@hw_settings.command('animation')
class HWSettingsAnimation(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get or change current animation mode value'
        mode_names = [m.name for m in list(AnimationMode)]
        help_str = "Mode: " + ", ".join(mode_names)
        parser.add_argument('-m', '--mode', type=str, required=False,
                            help=help_str, metavar="MODE", choices=mode_names)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.mode is not None:
            mode = AnimationMode[args.mode]
            self.cmd.set_animation_mode(mode)
            print("Animation mode change success.")
            print(f"{CY}Do not forget to store your settings in flash!{C0}")
        else:
            print(AnimationMode(self.cmd.get_animation_mode()))


@hw_settings.command('bleclearbonds')
class HWSettingsBleClearBonds(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Clear all BLE bindings. Warning: effect is immediate!'
        parser.add_argument("--force", default=False, action="store_true", help="Just to be sure")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print("If you are you really sure, read the command documentation to see how to proceed.")
            return
        self.cmd.delete_all_ble_bonds()
        print(" - Successfully clear all bonds")


@hw_settings.command('store')
class HWSettingsStore(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Store current settings to flash'
        return parser

    def on_exec(self, args: argparse.Namespace):
        print("Storing settings...")
        if self.cmd.save_settings():
            print(" - Store success @.@~")
        else:
            print(" - Store failed")


@hw_settings.command('reset')
class HWSettingsReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Reset settings to default values'
        parser.add_argument("--force", default=False, action="store_true", help="Just to be sure")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print("If you are you really sure, read the command documentation to see how to proceed.")
            return
        print("Initializing settings...")
        if self.cmd.reset_settings():
            print(" - Reset success @.@~")
        else:
            print(" - Reset failed")


@hw.command('factory_reset')
class HWFactoryReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Wipe all slot data and custom settings and return to factory settings'
        parser.add_argument("--force", default=False, action="store_true", help="Just to be sure")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.force:
            print("If you are you really sure, read the command documentation to see how to proceed.")
            return
        if self.cmd.wipe_fds():
            print(" - Reset successful! Please reconnect.")
            # let time for comm thread to close port
            time.sleep(0.1)
        else:
            print(" - Reset failed!")


@hw.command('battery')
class HWBatteryInfo(DeviceRequiredUnit):
    # How much remaining battery is considered low?
    BATTERY_LOW_LEVEL = 30

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get battery information, voltage and level'
        return parser

    def on_exec(self, args: argparse.Namespace):
        voltage, percentage = self.cmd.get_battery_info()
        print(" - Battery information:")
        print(f"   voltage    -> {voltage} mV")
        print(f"   percentage -> {percentage}%")
        if percentage < HWBatteryInfo.BATTERY_LOW_LEVEL:
            print(f"{CR}[!] Low battery, please charge.{C0}")


@hw_settings.command('btnpress')
class HWButtonSettingsGet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get or set button press function of Button A and Button B'
        button_group = parser.add_mutually_exclusive_group()
        button_group.add_argument('-a', '-A', action='store_true', help="Button A")
        button_group.add_argument('-b', '-B', action='store_true', help="Button B")
        duration_group = parser.add_mutually_exclusive_group()
        duration_group.add_argument('-s', '--short', action='store_true', help="Short-press (default)")
        duration_group.add_argument('-l', '--long', action='store_true', help="Long-press")
        function_names = [f.name for f in list(ButtonPressFunction)]
        function_descs = [f"{f.name} ({f})" for f in list(ButtonPressFunction)]
        help_str = "Function: " + ", ".join(function_descs)
        parser.add_argument('-f', '--function', type=str, required=False,
                            help=help_str, metavar="FUNCTION", choices=function_names)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.function is not None:
            function = ButtonPressFunction[args.function]
            if not args.a and not args.b:
                print(f"{CR}You must specify which button you want to change{C0}")
                return
            if args.a:
                button = ButtonType.A
            else:
                button = ButtonType.B
            if args.long:
                self.cmd.set_long_button_press_config(button, function)
            else:
                self.cmd.set_button_press_config(button, function)
            print(f" - Successfully set function '{function}'"
                  f" to Button {button.name} {'long-press' if args.long else 'short-press'}")
            print(f"{CY}Do not forget to store your settings in flash!{C0}")
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
                    print(f" - {CG}{button.name} short{C0}: {button_fn}")
                if not args.short:
                    resp_long = self.cmd.get_long_button_press_config(button)
                    button_long_fn = ButtonPressFunction(resp_long)
                    print(f" - {CG}{button.name} long {C0}: {button_long_fn}")
                print("")


@hw_settings.command('blekey')
class HWSettingsBLEKey(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Get or set the ble connect key'
        parser.add_argument('-k', '--key', required=False, help="Ble connect key for your device")
        return parser

    def on_exec(self, args: argparse.Namespace):
        key = self.cmd.get_ble_pairing_key()
        print(" - The current key of the device(ascii): "
              f"{CG}{key}{C0}")

        if args.key is not None:
            if len(args.key) != 6:
                print(f" - {CR}The ble connect key length must be 6{C0}")
                return
            if re.match(r'[0-9]{6}', args.key):
                self.cmd.set_ble_connect_key(args.key)
                print(" - Successfully set ble connect key to :", end='')
                print(f"{CG}"
                      f" { args.key }"
                      f"{C0}"
                      )
                print(f"{CY}Do not forget to store your settings in flash!{C0}")
            else:
                print(f" - {CR}Only 6 ASCII characters from 0 to 9 are supported.{C0}")


@hw_settings.command('blepair')
class HWBlePair(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Show or configure BLE pairing'
        set_group = parser.add_mutually_exclusive_group()
        set_group.add_argument('-e', '--enable', action='store_true', help="Enable BLE pairing")
        set_group.add_argument('-d', '--disable', action='store_true', help="Disable BLE pairing")
        return parser

    def on_exec(self, args: argparse.Namespace):
        is_pairing_enable = self.cmd.get_ble_pairing_enable()
        if not args.enable and not args.disable:
            if is_pairing_enable:
                print(f" - BLE pairing: {CG} Enabled{C0}")
            else:
                print(f" - BLE pairing: {CR} Disabled{C0}")
        elif args.enable:
            if is_pairing_enable:
                print(f"{CY} BLE pairing is already enabled.{C0}")
                return
            self.cmd.set_ble_pairing_enable(True)
            print(f" - Successfully change ble pairing to {CG}Enabled{C0}.")
            print(f"{CY}Do not forget to store your settings in flash!{C0}")
        elif args.disable:
            if not is_pairing_enable:
                print(f"{CY} BLE pairing is already disabled.{C0}")
                return
            self.cmd.set_ble_pairing_enable(False)
            print(f" - Successfully change ble pairing to {CR}Disabled{C0}.")
            print(f"{CY}Do not forget to store your settings in flash!{C0}")


@hw.command('raw')
class HWRaw(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Send raw command'
        cmd_names = sorted([c.name for c in list(Command)])
        help_str = "Command: " + ", ".join(cmd_names)
        command_group = parser.add_mutually_exclusive_group(required=True)
        command_group.add_argument('-c', '--command', type=str, metavar="COMMAND", help=help_str, choices=cmd_names)
        command_group.add_argument('-n', '--num_command', type=int, metavar="<dec>", help="Numeric command ID: <dec>")
        parser.add_argument('-d', '--data', type=str, help="Data to send", default="", metavar="<hex>")
        parser.add_argument('-t', '--timeout', type=int, help="Timeout in seconds", default=3, metavar="<dec>")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.command is not None:
            command = Command[args.command]
        else:
            # We accept not-yet-known command ids as "hw raw" is meant for debugging
            command = args.num_command
        response = self.cmd.device.send_cmd_sync(
            command, data=bytes.fromhex(args.data), status=0x0, timeout=args.timeout)
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


@hf_14a.command('raw')
class HF14ARaw(ReaderRequiredUnit):

    def bool_to_bit(self, value):
        return 1 if value else 0

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.formatter_class = argparse.RawDescriptionHelpFormatter
        parser.description = 'Send raw command'
        parser.add_argument('-a', '--activate-rf', help="Active signal field ON without select",
                            action='store_true', default=False,)
        parser.add_argument('-s', '--select-tag', help="Active signal field ON with select",
                            action='store_true', default=False,)
        # TODO: parser.add_argument('-3', '--type3-select-tag',
        #           help="Active signal field ON with ISO14443-3 select (no RATS)", action='store_true', default=False,)
        parser.add_argument('-d', '--data', type=str, metavar="<hex>", help="Data to be sent")
        parser.add_argument('-b', '--bits', type=int, metavar="<dec>",
                            help="Number of bits to send. Useful for send partial byte")
        parser.add_argument('-c', '--crc', help="Calculate and append CRC", action='store_true', default=False,)
        parser.add_argument('-r', '--no-response', help="Do not read response", action='store_true', default=False,)
        parser.add_argument('-cc', '--crc-clear', help="Verify and clear CRC of received data",
                            action='store_true', default=False,)
        parser.add_argument('-k', '--keep-rf', help="Keep signal field ON after receive",
                            action='store_true', default=False,)
        parser.add_argument('-t', '--timeout', type=int, metavar="<dec>", help="Timeout in ms", default=100)
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
            'activate_rf_field': self.bool_to_bit(args.activate_rf),
            'wait_response': self.bool_to_bit(not args.no_response),
            'append_crc': self.bool_to_bit(args.crc),
            'auto_select': self.bool_to_bit(args.select_tag),
            'keep_rf_field': self.bool_to_bit(args.keep_rf),
            'check_response_crc': self.bool_to_bit(args.crc_clear),
            # 'auto_type3_select': self.bool_to_bit(args.type3-select-tag),
        }
        data: str = args.data
        if data is not None:
            data = data.replace(' ', '')
            if re.match(r'^[0-9a-fA-F]+$', data):
                if len(data) % 2 != 0:
                    print(f" [!] {CR}The length of the data must be an integer multiple of 2.{C0}")
                    return
                else:
                    data_bytes = bytes.fromhex(data)
            else:
                print(f" [!] {CR}The data must be a HEX string{C0}")
                return
        else:
            data_bytes = []
        if args.bits is not None and args.crc:
            print(f" [!] {CR}--bits and --crc are mutually exclusive{C0}")
            return

        # Exec 14a raw cmd.
        resp = self.cmd.hf14a_raw(options, args.timeout, data_bytes, args.bits)
        if len(resp) > 0:
            print(
                # print head
                " - " +
                # print data
                ' '.join([hex(byte).replace('0x', '').rjust(2, '0') for byte in resp])
            )
        else:
            print(F" [*] {CY}No response{C0}")
