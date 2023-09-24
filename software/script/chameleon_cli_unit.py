import binascii
import os
import re
import subprocess
import argparse
import colorama
import timeit
import sys
import time
import serial.tools.list_ports
import threading
from platform import uname

import chameleon_com
import chameleon_cmd
import chameleon_status
from chameleon_utils import ArgumentParserNoExit, ArgsParserError, UnexpectedResponseError
from chameleon_utils import CLITree

# Colorama shorthands
CR = colorama.Fore.RED
CG = colorama.Fore.GREEN
CC = colorama.Fore.CYAN
CY = colorama.Fore.YELLOW
C0 = colorama.Style.RESET_ALL

# NXP IDs based on https://www.nxp.com/docs/en/application-note/AN10833.pdf
type_id_SAK_dict = {0x00: "MIFARE Ultralight Classic/C/EV1/Nano | NTAG 2xx",
                    0x08: "MIFARE Classic 1K | Plus SE 1K | Plug S 2K | Plus X 2K",
                    0x09: "MIFARE Mini 0.3k",
                    0x10: "MIFARE Plus 2K",
                    0x11: "MIFARE Plus 4K",
                    0x18: "MIFARE Classic 4K | Plus S 4K | Plus X 4K",
                    0x19: "MIFARE Classic 2K",
                    0x20: "MIFARE Plus EV1/EV2 | DESFire EV1/EV2/EV3 | DESFire Light | NTAG 4xx | MIFARE Plus S 2/4K | MIFARE Plus X 2/4K | MIFARE Plus SE 1K",
                    0x28: "SmartMX with MIFARE Classic 1K",
                    0x38: "SmartMX with MIFARE Classic 4K",
                    }


class BaseCLIUnit:

    def __init__(self):
        # new a device command transfer and receiver instance(Send cmd and receive response)
        self._device_com: chameleon_com.ChameleonCom | None = None
        self._device_cmd: chameleon_cmd.ChameleonCMD = chameleon_cmd.ChameleonCMD(self._device_com)

    @property
    def device_com(self) -> chameleon_com.ChameleonCom:
        return self._device_com

    @device_com.setter
    def device_com(self, com):
        self._device_com = com
        self._device_cmd = chameleon_cmd.ChameleonCMD(self._device_com)

    @property
    def cmd(self) -> chameleon_cmd.ChameleonCMD:
        return self._device_cmd

    def args_parser(self) -> ArgumentParserNoExit or None:
        """
            CMD unit args
        :return:
        """
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        """
            Call a function before exec cmd.
        :return: function references
        """
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        """
            Call a function on cmd match
        :return: function references
        """
        raise NotImplementedError("Please implement this")

    @staticmethod
    def sub_process(cmd, cwd=os.path.abspath("bin/")):
        class ShadowProcess:
            def __init__(self):
                self.output = ""
                self.time_start = timeit.default_timer()
                self._process = subprocess.Popen(cmd, cwd=cwd, shell=True, stderr=subprocess.PIPE,
                                                 stdout=subprocess.PIPE)
                threading.Thread(target=self.thread_read_output).start()

            def thread_read_output(self):
                while self._process.poll() is None:
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

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        ret = self.device_com.isOpen()
        if ret:
            return True
        else:
            print("Please connect to chameleon device first(use 'hw connect').")
            return False

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class ReaderRequiredUnit(DeviceRequiredUnit):
    """
        Make sure of device enter to reader mode.
    """

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        if super(ReaderRequiredUnit, self).before_exec(args):
            ret = self.cmd.is_device_reader_mode()
            if ret:
                return True
            else:
                print("Please switch chameleon to reader mode(use 'hw mode').")
                return False
        return False

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


hw = CLITree('hw', 'hardware controller')
hw_chipid = hw.subgroup('chipid', 'Device chipset ID get')
hw_address = hw.subgroup('address', 'Device address get')
hw_mode = hw.subgroup('mode', 'Device mode get/set')
hw_slot = hw.subgroup('slot', 'Emulation tag slot.')
hw_slot_nick = hw_slot.subgroup('nick', 'Get/Set tag nick name for slot')
hw_ble = hw.subgroup('ble', 'Bluetooth low energy')
hw_ble_bonds = hw_ble.subgroup('bonds', 'All devices bound by chameleons.')
hw_settings = hw.subgroup('settings', 'Chameleon settings management')
hw_settings_animation = hw_settings.subgroup('animation', 'Manage wake-up and sleep animation modes')
hw_settings_button_press = hw_settings.subgroup('btnpress', 'Manage button press function')

hf = CLITree('hf', 'high frequency tag/reader')
hf_14a = hf.subgroup('14a', 'ISO14443-a tag read/write/info...')
hf_mf = hf.subgroup('mf', 'Mifare Classic mini/1/2/4, attack/read/write')
hf_mf_detection = hf.subgroup('detection', 'Mifare Classic detection log')

lf = CLITree('lf', 'low frequency tag/reader')
lf_em = lf.subgroup('em', 'EM410x read/write/emulator')
lf_em_sim = lf_em.subgroup('sim', 'Manage EM410x emulation data for selected slot')

root_commands: dict[str, CLITree] = {'hw': hw, 'hf': hf, 'lf': lf}


@hw.command('connect', 'Connect to chameleon by serial port')
class HWConnect(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-p', '--port', type=str, required=False)
        return parser

    def before_exec(self, args: argparse.Namespace):
        return True

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
                        process = subprocess.Popen([powershell_path, "Get-PnPDevice -Class Ports -PresentOnly |"
                                                                     " where {$_.DeviceID -like '*VID_6868&PID_8686*'} |"
                                                                     " Select-Object -First 1 FriendlyName |"
                                                                     " % FriendlyName |"
                                                                     " select-string COM\d+ |"
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


@hw_mode.command('set', 'Change device mode to tag reader or tag emulator')
class HWModeSet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        help_str = "reader or r = reader mode, emulator or e = tag emulator mode."
        parser.add_argument('-m', '--mode', type=str, required=True, choices=['reader', 'r', 'emulator', 'e'],
                            help=help_str)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.mode == 'reader' or args.mode == 'r':
            self.cmd.set_device_reader_mode(True)
            print("Switch to {  Tag Reader  } mode successfully.")
        else:
            self.cmd.set_device_reader_mode(False)
            print("Switch to { Tag Emulator } mode successfully.")


@hw_mode.command('get', 'Get current device mode')
class HWModeGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def on_exec(self, args: argparse.Namespace):
        print(f"- Device Mode ( Tag {'Reader' if self.cmd.is_device_reader_mode() else 'Emulator'} )")


@hw_chipid.command('get', 'Get device chipset ID')
class HWChipIdGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print(' - Device chip ID: ' + self.cmd.get_device_chip_id())


@hw_address.command('get', 'Get device address (used with Bluetooth)')
class HWAddressGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print(' - Device address: ' + self.cmd.get_device_address())


@hw.command('version', 'Get current device firmware version')
class HWVersion(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        fw_version_tuple = self.cmd.get_app_version()
        fw_version = f'v{fw_version_tuple[0]}.{fw_version_tuple[1]}'
        git_version = self.cmd.get_git_version()
        model = ['Ultra', 'Lite'][self.cmd.get_device_model()]
        print(f' - Chameleon {model}, Version: {fw_version} ({git_version})')


@hf_14a.command('scan', 'Scan 14a tag, and print basic information')
class HF14AScan(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def check_mf1_nt(self):
        # detect mf1 support
        if self.cmd.mf1_detect_support():
            # detect prng
            print("- Mifare Classic technology")
            prng_type = self.cmd.mf1_detect_prng()
            print(f"  # Prng: {chameleon_cmd.MifareClassicPrngType(prng_type)}")

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
                print(f"- ATQA : {data_tag['atqa'].hex().upper()}")
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


@hf_14a.command('info', 'Scan 14a tag, and print detail information')
class HF14AInfo(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def on_exec(self, args: argparse.Namespace):
        scan = HF14AScan()
        scan.device_com = self.device_com
        scan.scan(deep=1)


@hf_mf.command('nested', 'Mifare Classic nested recover key')
class HFMFNested(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        type_choices = ['A', 'B', 'a', 'b']
        parser = ArgumentParserNoExit()
        parser.add_argument('-o', '--one', action='store_true', default=False,
                            help="one sector key recovery. Use block 0 Key A to find block 4 Key A")
        parser.add_argument('--block-known', type=int, required=True, metavar="decimal",
                            help="The block where the key of the card is known")
        parser.add_argument('--type-known', type=str, required=True, choices=type_choices,
                            help="The key type of the tag")
        parser.add_argument('--key-known', type=str, required=True, metavar="hex", help="tag sector key")
        parser.add_argument('--block-target', type=int, metavar="decimal",
                            help="The key of the target block to recover")
        parser.add_argument('--type-target', type=str, choices=type_choices,
                            help="The type of the target block to recover")
        # hf mf nested -o --block-known 0 --type-known A --key FFFFFFFFFFFF --block-target 4 --type-target A
        return parser

    def from_nt_level_code_to_str(self, nt_level):
        if nt_level == 0:
            return 'StaticNested'
        if nt_level == 1:
            return 'Nested'
        if nt_level == 2:
            return 'HardNested'

    def recover_a_key(self, block_known, type_known, key_known, block_target, type_target) -> str or None:
        """
            recover a key from key known
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
            cmd_param = f"{nt_uid_obj['uid']} {str(type_target)}"
            for nt_item in nt_uid_obj['nts']:
                cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']}"
            decryptor_name = "staticnested"
        else:
            dist_obj = self.cmd.mf1_detect_nt_dist(block_known, type_known, key_known)
            nt_obj = self.cmd.mf1_nested_acquire(block_known, type_known, key_known, block_target, type_target)
            # create cmd
            cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
            for nt_item in nt_obj:
                cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']} {nt_item['par']}"
            decryptor_name = "nested"

        # Cross-platform compatibility
        if sys.platform == "win32":
            cmd_recover = f"{decryptor_name}.exe {cmd_param}"
        else:
            cmd_recover = f"./{decryptor_name} {cmd_param}"

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
        block_known = args.block_known

        type_known = args.type_known
        type_known = 0x60 if type_known == 'A' or type_known == 'a' else 0x61

        key_known: str = args.key_known
        if not re.match(r"^[a-fA-F0-9]{12}$", key_known):
            print("key must include 12 HEX symbols")
            return
        key_known: bytearray = bytearray.fromhex(key_known)

        if args.one:
            block_target = args.block_target
            type_target = args.type_target
            if block_target is not None and type_target is not None:
                type_target = 0x60 if type_target == 'A' or type_target == 'a' else 0x61
                print(f" - {C0}Nested recover one key running...{C0}")
                key = self.recover_a_key(block_known, type_known, key_known, block_target, type_target)
                if key is None:
                    print("No keys found, you can retry recover.")
                else:
                    print(f" - Key Found: {key}")
            else:
                print("Please input block_target and type_target")
                self.args_parser().print_help()
        else:
            raise NotImplementedError("hf mf nested recover all key not implement.")

        return


@hf_mf.command('darkside', 'Mifare Classic darkside recover key')
class HFMFDarkside(ReaderRequiredUnit):
    def __init__(self):
        super().__init__()
        self.darkside_list = []

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def recover_key(self, block_target, type_target):
        """
            Execute darkside acquisition and decryption
        :param block_target:
        :param type_target:
        :return:
        """
        first_recover = True
        retry_count = 0
        while retry_count < 0xFF:
            darkside_resp = self.cmd.mf1_darkside_acquire(block_target, type_target, first_recover, 30)
            first_recover = False  # not first run.
            if darkside_resp[0] != chameleon_cmd.MifareClassicDarksideStatus.OK:
                print(f"Darkside error: {chameleon_cmd.MifareClassicDarksideStatus(darkside_resp[0])}")
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
        key = self.recover_key(0x03, 0x60)
        if key is not None:
            print(f" - Key Found: {key}")
        else:
            print(" - Key recover fail.")
        return


class BaseMF1AuthOpera(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        type_choices = ['A', 'B', 'a', 'b']
        parser = ArgumentParserNoExit()
        parser.add_argument('-b', '--block', type=int, required=True, metavar="decimal",
                            help="The block where the key of the card is known")
        parser.add_argument('-t', '--type', type=str, required=True, choices=type_choices,
                            help="The key type of the tag")
        parser.add_argument('-k', '--key', type=str, required=True, metavar="hex", help="tag sector key")
        return parser

    def get_param(self, args):
        class Param:
            def __init__(self):
                self.block = args.block
                self.type = 0x60 if args.type == 'A' or args.type == 'a' else 0x61
                key: str = args.key
                if not re.match(r"^[a-fA-F0-9]{12}$", key):
                    raise ArgsParserError("key must include 12 HEX symbols")
                self.key: bytearray = bytearray.fromhex(key)

        return Param()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


@hf_mf.command('rdbl', 'Mifare Classic read one block')
class HFMFRDBL(BaseMF1AuthOpera):
    # hf mf rdbl -b 2 -t A -k FFFFFFFFFFFF
    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        resp = self.cmd.mf1_read_one_block(param.block, param.type, param.key)
        print(f" - Data: {resp.hex()}")


@hf_mf.command('wrbl', 'Mifare Classic write one block')
class HFMFWRBL(BaseMF1AuthOpera):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = super(HFMFWRBL, self).args_parser()
        parser.add_argument('-d', '--data', type=str, required=True, metavar="Your block data",
                            help="Your block data, a hex string.")
        return parser

    # hf mf wrbl -b 2 -t A -k FFFFFFFFFFFF -d 00000000000000000000000000000122
    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        if not re.match(r"^[a-fA-F0-9]{32}$", args.data):
            raise ArgsParserError("Data must include 32 HEX symbols")
        param.data = bytearray.fromhex(args.data)
        resp = self.cmd.mf1_write_one_block(param.block, param.type, param.key, param.data)
        if resp:
            print(f" - {CG}Write done.{C0}")
        else:
            print(f" - {CR}Write fail.{C0}")


@hf_mf_detection.command('enable', 'Detection enable')
class HFMFDetectionEnable(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-e', '--enable', type=int, required=True, choices=[1, 0], help="1 = enable, 0 = disable")
        return parser

    # hf mf detection enable -e 1
    def on_exec(self, args: argparse.Namespace):
        enable = True if args.enable == 1 else False
        self.cmd.mf1_set_detection_enable(enable)
        print(f" - Set mf1 detection {'enable' if enable else 'disable'}.")


@hf_mf_detection.command('count', 'Detection log count')
class HFMFDetectionLogCount(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hf mf detection count
    def on_exec(self, args: argparse.Namespace):
        count = self.cmd.mf1_get_detection_count()
        print(f" - MF1 detection log count = {count}")


@hf_mf_detection.command('decrypt', 'Download log and decrypt keys')
class HFMFDetectionDecrypt(DeviceRequiredUnit):
    detection_log_size = 18

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def decrypt_by_list(self, rs: list):
        """
            Decrypt key from reconnaissance log list
        :param rs:
        :return:
        """
        msg1 = f"  > {len(rs)} records => "
        msg2 = f"/{(len(rs)*(len(rs)-1))//2} combinations. "
        msg3 = f" key(s) found"
        n = 1
        keys = set()
        for i in range(len(rs)):
            item0 = rs[i]
            for j in range(i + 1, len(rs)):
                item1 = rs[j]
                # TODO: if some keys already recovered, test them on item before running mfkey32 on item
                # TODO: if some keys already recovered, remove corresponding items
                cmd_base = f"{item0['uid']} {item0['nt']} {item0['nr']} {item0['ar']}"
                cmd_base += f" {item1['nt']} {item1['nr']} {item1['ar']}"
                if sys.platform == "win32":
                    cmd_recover = f"mfkey32v2.exe {cmd_base}"
                else:
                    cmd_recover = f"./mfkey32v2 {cmd_base}"
                # print(cmd_recover)
                # Found Key: [e899c526c5cd]
                # subprocess.run(cmd_final, cwd=os.path.abspath("../bin/"), shell=True)
                process = self.sub_process(cmd_recover)
                # wait end
                process.wait_process()
                # get output
                output_str = process.get_output_sync()
                # print(output_str)
                sea_obj = re.search(r"([a-fA-F0-9]{12})", output_str, flags=re.MULTILINE)
                if sea_obj is not None:
                    keys.add(sea_obj[1])
                print(f"{msg1}{n}{msg2}{len(keys)}{msg3}\r", end="")
                n += 1
        print()
        return keys

    # hf mf detection decrypt
    def on_exec(self, args: argparse.Namespace):
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


@hf_mf.command('eload', 'Load data to emulator memory')
class HFMFELoad(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-f', '--file', type=str, required=True, help="file path")
        parser.add_argument('-t', '--type', type=str, required=False, help="content type", choices=['bin', 'hex'])
        return parser

    # hf mf eload -f test.bin -t bin
    # hf mf eload -f test.eml -t hex
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


@hf_mf.command('eread', 'Read data from emulator memory')
class HFMFERead(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
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
        tag_type = chameleon_cmd.TagSpecificType(slot_info[selected_slot]['hf'])
        if tag_type == chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_Mini:
            block_count = 20
        elif tag_type == chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_1024:
            block_count = 64
        elif tag_type == chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_2048:
            block_count = 128
        elif tag_type == chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_4096:
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


@hf_mf.command('settings', 'Settings of Mifare Classic emulator')
class HFMFSettings(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()

        help_str = ""
        for s in chameleon_cmd.MifareClassicWriteMode:
            help_str += f"{s.value} = {s}, "
        help_str = help_str[:-2]

        parser.add_argument('--gen1a', type=int, required=False, help="Gen1a magic mode, 1 - enable, 0 - disable",
                            default=-1, choices=[1, 0])
        parser.add_argument('--gen2', type=int, required=False, help="Gen2 magic mode, 1 - enable, 0 - disable",
                            default=-1, choices=[1, 0])
        parser.add_argument('--coll', type=int, required=False,
                            help="Use anti-collision data from block 0 for 4 byte UID tags, 1 - enable, 0 - disable",
                            default=-1, choices=[1, 0])
        parser.add_argument('--write', type=int, required=False, help=f"Write mode: {help_str}", default=-1,
                            choices=chameleon_cmd.MifareClassicWriteMode.list())
        return parser

    # hf mf settings
    def on_exec(self, args: argparse.Namespace):
        if args.gen1a != -1:
            self.cmd.mf1_set_gen1a_mode(args.gen1a)
            print(f' - Set gen1a mode to {"enabled" if args.gen1a else "disabled"} success')
        if args.gen2 != -1:
            self.cmd.mf1_set_gen2_mode(args.gen2)
            print(f' - Set gen2 mode to {"enabled" if args.gen2 else "disabled"} success')
        if args.coll != -1:
            self.cmd.mf1_set_block_anti_coll_mode(args.coll)
            print(f' - Set anti-collision mode to {"enabled" if args.coll else "disabled"} success')
        if args.write != -1:
            self.cmd.mf1_set_write_mode(args.write)
            print(f' - Set write mode to {chameleon_cmd.MifareClassicWriteMode(args.write)} success')
        print(' - Emulator settings updated')


@hf_mf.command('sim', 'Simulate a Mifare Classic card')
class HFMFSim(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('--uid', type=str, required=True, help="Unique ID(hex)", metavar="hex")
        parser.add_argument('--atqa', type=str, required=True, help="Answer To Request(hex)", metavar="hex")
        parser.add_argument('--sak', type=str, required=True, help="Select AcKnowledge(hex)", metavar="hex")
        parser.add_argument('--ats', type=str, required=False, help="Answer To Select(hex)", metavar="hex")
        return parser

    # hf mf sim --sak 08 --atqa 0400 --uid DEADBEEF
    def on_exec(self, args: argparse.Namespace):
        uid_str: str = args.uid.strip()
        if re.match(r"[a-fA-F0-9]+", uid_str) is not None:
            uid = bytes.fromhex(uid_str)
            if len(uid) not in [4, 7, 10]:
                raise Exception("UID length error")
        else:
            raise Exception("UID must be hex")

        atqa_str: str = args.atqa.strip()
        if re.match(r"[a-fA-F0-9]{4}", atqa_str) is not None:
            atqa = bytes.fromhex(atqa_str)
        else:
            raise Exception("ATQA must be hex(4byte)")

        sak_str: str = args.sak.strip()
        if re.match(r"[a-fA-F0-9]{2}", sak_str) is not None:
            sak = bytes.fromhex(sak_str)
        else:
            raise Exception("SAK must be hex(2byte)")

        if args.ats is not None:
            ats_str: str = args.ats.strip()
            if re.match(r"[a-fA-F0-9]+", ats_str) is not None:
                ats = bytes.fromhex(ats_str)
            else:
                raise Exception("ATS must be hex")
        else:
            ats = b''

        self.cmd.hf14a_set_anti_coll_data(uid, atqa, sak, ats)
        print(" - Set anti-collision resources success")


@hf_mf.command('info', 'Get information about current slot (UID/SAK/ATQA)')
class HFMFInfo(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def scan(self):
        resp = self.cmd.hf14a_get_anti_coll_data()
        print(f"- UID  : {resp['uid'].hex().upper()}")
        print(f"- ATQA : {resp['atqa'].hex().upper()}")
        print(f"- SAK  : {resp['sak'].hex().upper()}")
        if len(resp['ats']) > 0:
            print(f"- ATS  : {resp['ats'].hex().upper()}")

    def on_exec(self, args: argparse.Namespace):
        return self.scan()


@lf_em.command('read', 'Scan em410x tag and print id')
class LFEMRead(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        id = self.cmd.em410x_scan()
        print(f" - EM410x ID(10H): {CG}{id.hex()}{C0}")


class LFEMCardRequiredUnit(DeviceRequiredUnit):
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit):
        parser.add_argument("--id", type=str, required=True, help="EM410x tag id", metavar="hex")
        return parser

    def before_exec(self, args: argparse.Namespace):
        if super(LFEMCardRequiredUnit, self).before_exec(args):
            if not re.match(r"^[a-fA-F0-9]{10}$", args.id):
                raise ArgsParserError("ID must include 10 HEX symbols")
            return True
        return False

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


@lf_em.command('write', 'Write em410x id to t55xx')
class LFEMWriteT55xx(LFEMCardRequiredUnit, ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_card_arg(parser)

    def before_exec(self, args: argparse.Namespace):
        b1 = super(LFEMCardRequiredUnit, self).before_exec(args)
        b2 = super(ReaderRequiredUnit, self).before_exec(args)
        return b1 and b2

    # lf em write --id 4400999559
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytes.fromhex(id_hex)
        self.cmd.em410x_write_to_t55xx(id_bytes)
        print(f" - EM410x ID(10H): {id_hex} write done.")


class SlotIndexRequireUnit(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()

    @staticmethod
    def add_slot_args(parser: ArgumentParserNoExit):
        slot_choices = [x.value for x in chameleon_cmd.SlotNumber]
        help_str = f"Slot Indexes: {slot_choices}"

        parser.add_argument('-s', "--slot", type=int, required=True, help=help_str, metavar="number",
                            choices=slot_choices)
        return parser


class SenseTypeRequireUnit(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()

    @staticmethod
    def add_sense_type_args(parser: ArgumentParserNoExit):
        sense_choices = chameleon_cmd.TagSenseType.list()

        help_str = ""
        for s in chameleon_cmd.TagSenseType:
            if s == chameleon_cmd.TagSenseType.TAG_SENSE_NO:
                continue
            help_str += f"{s.value} = {s}, "

        parser.add_argument('-st', "--sense_type", type=int, required=True, help=help_str, metavar="number",
                            choices=sense_choices)
        return parser


@hw_slot.command('list', 'Get information about slots')
class HWSlotList(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-e', '--extend', type=int, required=False,
                            help="Show slot nicknames and Mifare Classic emulator settings. 0 - skip, 1 - show (default)", choices=[0, 1], default=1)
        return parser

    def get_slot_name(self, slot, sense):
        try:
            name = self.cmd.get_slot_tag_nick(slot, sense).decode(encoding="utf8")
            return {'baselen':len(name), 'metalen':len(CC+C0), 'name':f'{CC}{name}{C0}'}
        except UnexpectedResponseError:
            return {'baselen':0, 'metalen':0, 'name':f''}
        except UnicodeDecodeError:
            name = "UTF8 Err"
            return {'baselen':len(name), 'metalen':len(CC+C0), 'name':f'{CC}{name}{C0}'}

    # hw slot list
    def on_exec(self, args: argparse.Namespace):
        slotinfo = self.cmd.get_slot_info()
        selected = chameleon_cmd.SlotNumber.from_fw(self.cmd.get_active_slot())
        enabled = self.cmd.get_enabled_slots()
        maxnamelength = 0
        if args.extend:
            slotnames = []
            for slot in chameleon_cmd.SlotNumber:
                hfn = self.get_slot_name(slot, chameleon_cmd.TagSenseType.TAG_SENSE_HF)
                lfn = self.get_slot_name(slot, chameleon_cmd.TagSenseType.TAG_SENSE_LF)
                m = max(hfn['baselen'], lfn['baselen'])
                maxnamelength = m if m > maxnamelength else maxnamelength
                slotnames.append({'hf':hfn, 'lf':lfn})
        for slot in chameleon_cmd.SlotNumber:
            fwslot = chameleon_cmd.SlotNumber.to_fw(slot)
            hf_tag_type = chameleon_cmd.TagSpecificType(slotinfo[fwslot]['hf'])
            lf_tag_type = chameleon_cmd.TagSpecificType(slotinfo[fwslot]['lf'])
            print(f' - {f"Slot {slot}:":{4+maxnamelength+1}}'
                  f'{f"({CG}active{C0})" if slot == selected else ""}')
            print(f'   HF: '
                  f'{(slotnames[fwslot]["hf"]["name"] if args.extend else ""):{maxnamelength+slotnames[fwslot]["hf"]["metalen"]+1 if args.extend else maxnamelength+1}}', end='')
            print(f'{f"({CR}disabled{C0}) " if not enabled[fwslot]["hf"] else ""}', end='')
            if hf_tag_type != chameleon_cmd.TagSpecificType.TAG_TYPE_UNDEFINED:
                print(f"{CY if enabled[fwslot]['hf'] else C0}{hf_tag_type}{C0}")
            else:
                print("undef")
            if args.extend == 1 and \
                    enabled[fwslot]['hf'] and \
                    slot == selected and \
                    hf_tag_type in [
                        chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_Mini,
                        chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_1024,
                        chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_2048,
                        chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_4096,
                    ]:
                config = self.cmd.mf1_get_emulator_config()
                print('    - Mifare Classic emulator settings:')
                print(
                    f'      {"Detection (mfkey32) mode:":40}{f"{CG}enabled{C0}" if config["detection"] else f"{CR}disabled{C0}"}')
                print(
                    f'      {"Gen1A magic mode:":40}{f"{CG}enabled{C0}" if config["gen1a_mode"] else f"{CR}disabled{C0}"}')
                print(
                    f'      {"Gen2 magic mode:":40}{f"{CG}enabled{C0}" if config["gen2_mode"] else f"{CR}disabled{C0}"}')
                print(
                    f'      {"Use anti-collision data from block 0:":40}{f"{CG}enabled{C0}" if config["block_anti_coll_mode"] else f"{CR}disabled{C0}"}')
                print(f'      {"Write mode:":40}{CY}{chameleon_cmd.MifareClassicWriteMode(config["write_mode"])}{C0}')
            print(f'   LF: '
                  f'{(slotnames[fwslot]["lf"]["name"] if args.extend else ""):{maxnamelength+slotnames[fwslot]["lf"]["metalen"]+1 if args.extend else maxnamelength+1}}', end='')
            print(f'{f"({CR}disabled{C0}) " if not enabled[fwslot]["lf"] else ""}', end='')
            if lf_tag_type != chameleon_cmd.TagSpecificType.TAG_TYPE_UNDEFINED:
                print(f"{CY if enabled[fwslot]['lf'] else C0}{lf_tag_type}{C0}")
            else:
                print("undef")

@hw_slot.command('change', 'Set emulation tag slot activated.')
class HWSlotSet(SlotIndexRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_slot_args(parser)

    # hw slot change -s 1
    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd.set_active_slot(slot_index)
        print(f" - Set slot {slot_index} activated success.")


class TagTypeRequiredUnit(DeviceRequiredUnit):
    @staticmethod
    def add_type_args(parser: ArgumentParserNoExit):
        type_choices = chameleon_cmd.TagSpecificType.list()
        help_str = ""
        for t in type_choices:
            help_str += f"{t.value} = {t}, "
        help_str = help_str[:-2]
        parser.add_argument('-t', "--type", type=int, required=True, help=help_str, metavar="number",
                            choices=[t.value for t in type_choices])
        return parser

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


@hw_slot.command('type', 'Set emulation tag type')
class HWSlotTagType(TagTypeRequiredUnit, SlotIndexRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_type_args(parser)
        self.add_slot_args(parser)
        return parser

    # hw slot tagtype -t 2
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        slot_index = args.slot
        self.cmd.set_slot_tag_type(slot_index, tag_type)
        print(' - Set slot tag type success.')


@hw_slot.command('delete', 'Delete sense type data for slot')
class HWDeleteSlotSense(SlotIndexRequireUnit, SenseTypeRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = "Delete sense type data for a specific slot."
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    def on_exec(self, args: argparse.Namespace):
        slot = args.slot
        sense_type = args.sense_type
        self.cmd.delete_slot_sense_type(slot, sense_type)


@hw_slot.command('init', 'Set emulation tag data to default')
class HWSlotDataDefault(TagTypeRequiredUnit, SlotIndexRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_type_args(parser)
        self.add_slot_args(parser)
        return parser

    # m1 1k card emulation hw slot init -s 1 -t 3
    # em id card simulation hw slot init -s 1 -t 1
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        slot_num = args.slot
        self.cmd.set_slot_data_default(slot_num, tag_type)
        print(' - Set slot tag data init success.')


@hw_slot.command('enable', 'Set emulation tag slot enable or disable')
class HWSlotEnableSet(SlotIndexRequireUnit, SenseTypeRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        parser.add_argument('-e', '--enable', type=int, required=True, help="1 is Enable or 0 Disable", choices=[0, 1])
        return parser

    # hw slot enable -s 1 -st 0 -e 0
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        enable = args.enable
        self.cmd.set_slot_enable(slot_num, sense_type, enable)
        print(f' - Set slot {slot_num} {"LF" if sense_type==chameleon_cmd.TagSenseType.TAG_SENSE_LF else "HF"} {"enable" if enable else "disable"} success.')

@lf_em_sim.command('set', 'Set simulated em410x card id')
class LFEMSimSet(LFEMCardRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_card_arg(parser)

    # lf em sim set --id 4545454545
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        self.cmd.em410x_set_emu_id(bytes.fromhex(id_hex))
        print(' - Set em410x tag id success.')


@lf_em_sim.command('get', 'Get simulated em410x card id')
class LFEMSimGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # lf em sim get
    def on_exec(self, args: argparse.Namespace):
        response = self.cmd.em410x_get_emu_id()
        print(' - Get em410x tag id success.')
        print(f'ID: {response.hex()}')


@hw_slot_nick.command('set', 'Set tag nick name for slot')
class HWSlotNickSet(SlotIndexRequireUnit, SenseTypeRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        parser.add_argument('-n', '--name', type=str, required=True, help="Your tag nick name for slot")
        return parser

    # hw slot nick set -s 1 -st 1 -n Save the test name
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        name: str = args.name
        encoded_name = name.encode(encoding="utf8")
        if len(encoded_name) > 32:
            raise ValueError("Your tag nick name too long.")
        self.cmd.set_slot_tag_nick(slot_num, sense_type, encoded_name)
        print(f' - Set tag nick name for slot {slot_num} success.')


@hw_slot_nick.command('get', 'Get tag nick name for slot')
class HWSlotNickGet(SlotIndexRequireUnit, SenseTypeRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    # hw slot nick get -s 1 -st 1
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        res = self.cmd.get_slot_tag_nick(slot_num, sense_type)
        print(f' - Get tag nick name for slot {slot_num}: {res.decode(encoding="utf8")}')


@hw_slot.command('update', 'Update config & data to device flash')
class HWSlotUpdate(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw slot update
    def on_exec(self, args: argparse.Namespace):
        self.cmd.slot_data_config_save()
        print(' - Update config and data from device memory to flash success.')


@hw_slot.command('openall', 'Open all slot and set to default data')
class HWSlotOpenAll(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw slot openall
    def on_exec(self, args: argparse.Namespace):
        # what type you need set to default?
        hf_type = chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_1024
        lf_type = chameleon_cmd.TagSpecificType.TAG_TYPE_EM410X

        # set all slot
        for slot in chameleon_cmd.SlotNumber:
            print(f' Slot {slot} setting...')
            # first to set tag type
            self.cmd.set_slot_tag_type(slot, hf_type)
            self.cmd.set_slot_tag_type(slot, lf_type)
            # to init default data
            self.cmd.set_slot_data_default(slot, hf_type)
            self.cmd.set_slot_data_default(slot, lf_type)
            # finally, we can enable this slot.
            self.cmd.set_slot_enable(slot, chameleon_cmd.TagSenseType.TAG_SENSE_HF, True)
            self.cmd.set_slot_enable(slot, chameleon_cmd.TagSenseType.TAG_SENSE_LF, True)
            print(f' Slot {slot} setting done.')

        # update config and save to flash
        self.cmd.slot_data_config_save()
        print(' - Succeeded opening all slots and setting data to default.')


@hw.command('dfu', 'Restart application to bootloader mode(Not yet implement dfu).')
class HWDFU(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw dfu
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


@hw_settings_animation.command('get', 'Get current animation mode value')
class HWSettingsAnimationGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.get_animation_mode()
        if resp == 0:
            print("Full animation")
        elif resp == 1:
            print("Minimal animation")
        elif resp == 2:
            print("No animation")
        else:
            print("Unknown setting value, something failed.")


@hw_settings_animation.command('set', 'Change chameleon animation mode')
class HWSettingsAnimationSet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-m', '--mode', type=int, required=True,
                            help="0 is full (default), 1 is minimal (only single pass on button wakeup), 2 is none",
                            choices=[0, 1, 2])
        return parser

    def on_exec(self, args: argparse.Namespace):
        mode = args.mode
        self.cmd.set_animation_mode(mode)
        print("Animation mode change success. Do not forget to store your settings in flash!")


@hw_settings.command('store', 'Store current settings to flash')
class HWSettingsStore(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print("Storing settings...")
        if self.cmd.save_settings():
            print(" - Store success @.@~")
        else:
            print(" - Store failed")


@hw_settings.command('reset', 'Reset settings to default values')
class HWSettingsReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print("Initializing settings...")
        if self.cmd.reset_settings():
            print(" - Reset success @.@~")
        else:
            print(" - Reset failed")


@hw.command('factory_reset', 'Wipe all data and return to factory settings')
class HWFactoryReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.description = "Permanently wipes Chameleon to factory settings. " \
                             "This will delete all your slot data and custom settings. " \
                             "There's no going back."
        parser.add_argument("--i-know-what-im-doing", default=False, action="store_true", help="Just to be sure :)")
        return parser

    def on_exec(self, args: argparse.Namespace):
        if not args.i_know_what_im_doing:
            print("This time your data's safe. Read the command documentation next time.")
            return
        if self.cmd.wipe_fds():
            print(" - Reset successful! Please reconnect.")
            # let time for comm thread to close port
            time.sleep(0.1)
        else:
            print(" - Reset failed!")


@hw.command('battery', 'Get battery information, voltage and level.')
class HWBatteryInfo(DeviceRequiredUnit):
    # How much remaining battery is considered low?
    BATTERY_LOW_LEVEL = 30

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        voltage, percentage = self.cmd.get_battery_info()
        print(" - Battery information:")
        print(f"   voltage    -> {voltage} mV")
        print(f"   percentage -> {percentage}%")
        if percentage < HWBatteryInfo.BATTERY_LOW_LEVEL:
            print(f"{CR}[!] Low battery, please charge.{C0}")


@hw_settings_button_press.command('get', 'Get button press function of Button A and Button B.')
class HWButtonSettingsGet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        # all button in here.
        button_list = [chameleon_cmd.ButtonType.ButtonA, chameleon_cmd.ButtonType.ButtonB, ]
        print("")
        for button in button_list:
            resp = self.cmd.get_button_press_config(button)
            resp_long = self.cmd.get_long_button_press_config(button)
            button_fn = chameleon_cmd.ButtonPressFunction.from_int(resp)
            button_long_fn = chameleon_cmd.ButtonPressFunction.from_int(resp_long)
            print(f" - {CG}{button} {CY}short{C0}:"
                  f" {button_fn}")
            print(f"      usage: {button_fn.usage()}")
            print(f" - {CG}{button} {CY}long {C0}:"
                  f" {button_long_fn}")
            print(f"      usage: {button_long_fn.usage()}")
            print("")
        print(" - Successfully get button function from settings")


@hw_settings_button_press.command('set', 'Set button press function of Button A and Button B.')
class HWButtonSettingsSet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-l', '--long', action='store_true', default=False, help="set keybinding for long-press")
        parser.add_argument('-b', type=str, required=True, help="Change the function of the pressed button(?).",
                            choices=chameleon_cmd.ButtonType.list_str())
        function_usage = ""
        for fun in chameleon_cmd.ButtonPressFunction:
            function_usage += f"{int(fun)} = {fun.usage()}, "
        function_usage = function_usage.rstrip(' ').rstrip(',')
        parser.add_argument('-f', type=int, required=True, help=function_usage,
                            choices=chameleon_cmd.ButtonPressFunction.list())
        return parser

    def on_exec(self, args: argparse.Namespace):
        button = chameleon_cmd.ButtonType.from_str(args.b)
        function = chameleon_cmd.ButtonPressFunction.from_int(args.f)
        if args.long:
            self.cmd.set_long_button_press_config(button, function)
        else:
            self.cmd.set_button_press_config(button, function)
        print(" - Successfully set button function to settings")


@hw_settings.command('blekey', 'Get or set the ble connect key')
class HWSettingsBLEKey(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-k', '--key', required=False, help="Ble connect key for your device")
        return parser

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.get_ble_pairing_key()
        print(" - The current key of the device(ascii): "
              f"{CG}{resp.decode(encoding='ascii')}{C0}")

        if args.key != None:
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
            else:
                print(f" - {CR}Only 6 ASCII characters from 0 to 9 are supported.{C0}")


@hw_settings.command('blepair', 'Check if BLE pairing is enabled, or set the enable switch for BLE pairing.')
class HWBlePair(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-e', '--enable', type=int, required=False, help="Enable = 1 or Disable = 0")
        return parser

    def on_exec(self, args: argparse.Namespace):
        is_pairing_enable = self.cmd.get_ble_pairing_enable()
        print(f" - Is ble pairing enable: ", end='')
        color = CG if is_pairing_enable else CR
        print(
            f"{color}"
            f"{ 'Yes' if is_pairing_enable else 'No' }"
            f"{C0}"
        )
        if args.enable is not None:
            if args.enable == 1 and is_pairing_enable:
                print(f"{CY} It is already in an enabled state.{C0}")
                return
            if args.enable == 0 and not is_pairing_enable:
                print(f"{CY} It is already in a non enabled state.{C0}")
                return
            self.cmd.set_ble_pairing_enable(args.enable)
            print(f" - Successfully change ble pairing to "
                  f"{CG if args.enable else CR}"
                  f"{ 'Enable' if args.enable else 'Disable' } "
                  f"{C0}"
                  "state.")


@hw_ble_bonds.command('clear', 'Clear all bindings')
class HWBLEBondsClear(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        self.cmd.delete_ble_all_bonds()
        print(" - Successfully clear all bonds")


@hw.command('raw', 'Send raw command')
class HWRaw(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-c', '--command', type=int, required=True, help="Command (Int) to send")
        parser.add_argument('-d', '--data', type=str, help="Data (HEX) to send", default="")
        parser.add_argument('-t', '--timeout', type=int, help="Timeout in seconds", default=3)
        return parser

    def on_exec(self, args: argparse.Namespace):
        response = self.cmd.device.send_cmd_sync(
            args.command, data=bytes.fromhex(args.data), status=0x0, timeout=args.timeout)
        print(" - Received:")
        print(f"   Command: {response.cmd}")
        status_string = f"   Status:  {response.status:#02x}"
        if response.status in chameleon_status.Device:
            status_string += f" {chameleon_status.Device[response.status]}"
            if response.status in chameleon_status.message:
                status_string += f": {chameleon_status.message[response.status]}"
                print(status_string)
        else:
            print(f"   Status: {response.status:#02x}")
        print(f"   Data (HEX): {response.data.hex()}")


@hf_14a.command('raw', 'Send raw command')
class HF14ARaw(ReaderRequiredUnit):

    def bool_to_bit(self, value):
        return 1 if value else 0 

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-a', '--activate-rf', help="Active signal field ON without select", action='store_true', default=False,)
        parser.add_argument('-s', '--select-tag', help="Active signal field ON with select", action='store_true', default=False,)
        # TODO: parser.add_argument('-3', '--type3-select-tag', help="Active signal field ON with ISO14443-3 select (no RATS)", action='store_true', default=False,)
        parser.add_argument('-d', '--data', type=str, help="Data to be sent")
        parser.add_argument('-b', '--bits', type=int, help="Number of bits to send. Useful for send partial byte")
        parser.add_argument('-c', '--crc', help="Calculate and append CRC", action='store_true', default=False,)
        parser.add_argument('-r', '--response', help="Do not read response", action='store_true', default=False,)
        parser.add_argument('-cc', '--crc-clear', help="Verify and clear CRC of received data", action='store_true', default=False,)
        parser.add_argument('-k', '--keep-rf', help="Keep signal field ON after receive", action='store_true', default=False,)
        parser.add_argument('-t', '--timeout', type=int, help="Timeout in ms", default=100)
        # TODO: need support for carriage returns in parser, why are they mangled?
        # parser.description = 'Examples:\n' \
        #                      '  hf 14a raw -b 7 -d 40 -k\n' \
        #                      '  hf 14a raw -d 43 -k\n' \
        #                      '  hf 14a raw -d 3000 -c\n' \
        #                      '  hf 14a raw -sc -d 6000\n'
        return parser


    def on_exec(self, args: argparse.Namespace):
        options = {
            'activate_rf_field': self.bool_to_bit(args.activate_rf),
            'wait_response': self.bool_to_bit(not args.response),
            'append_crc': self.bool_to_bit(args.crc),
            'auto_select': self.bool_to_bit(args.select_tag),
            'keep_rf_field': self.bool_to_bit(args.keep_rf),
            'check_response_crc': self.bool_to_bit(args.crc_clear),
            #'auto_type3_select': self.bool_to_bit(args.type3-select-tag),
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
