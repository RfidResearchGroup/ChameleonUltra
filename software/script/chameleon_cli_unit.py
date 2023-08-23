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
from platform import uname

import chameleon_com
import chameleon_cmd
import chameleon_cstruct
import chameleon_status
from chameleon_utils import *


class BaseCLIUnit:

    def __init__(self):
        # new a device command transfer and receiver instance(Send cmd and receive response)
        self._device_com: chameleon_com.ChameleonCom | None = None

    @property
    def device_com(self) -> chameleon_com.ChameleonCom:
        return self._device_com

    @device_com.setter
    def device_com(self, com):
        self._device_com = com

    @property
    def cmd(self) -> chameleon_cmd.ChameleonCMD:
        return chameleon_cmd.ChameleonCMD(self.device_com)

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
                self.time_start = timeit.default_timer()
                self._process = subprocess.Popen(cmd, cwd=cwd, shell=True, stderr=subprocess.PIPE,
                                                 stdout=subprocess.PIPE)

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

            def get_output_sync(self, encoding='utf-8'):
                buffer = bytearray()
                while True:
                    data = self._process.stdout.read(1024)
                    if len(data) > 0:
                        buffer.extend(data)
                    else:
                        break
                return buffer.decode(encoding)

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
            ret = self.cmd.is_reader_device_mode()
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
hw_settings = hw.subgroup('settings', 'Chameleon settings management')
hw_settings_animation = hw_settings.subgroup('animation', 'Manage wake-up and sleep animation modes')
hw_settings_button_press = hw_settings.subgroup('btnpress', 'Manage button press function')

hf = CLITree('hf', 'high frequency tag/reader')
hf_14a = hf.subgroup('14a', 'ISO14443-a tag read/write/info...')
hf_mf = hf.subgroup('mf', 'Mifare Classic mini/1/2/4, attack/read/write')
hf_mf_detection = hf.subgroup(
    'detection', 'Mifare Classic detection log')

lf = CLITree('lf', 'low frequency tag/reader')
lf_em = lf.subgroup('em', 'EM410x read/write/emulator')
lf_em_sim = lf_em.subgroup('sim', 'Manage EM410x emulation data for selected slot')

root_commands: dict[str, CLITree] = {
    'hw': hw,
    'hf': hf,
    'lf': lf,
}

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
                platformname = uname().release
                if 'Microsoft' in platformname:
                    path = os.environ["PATH"].split(os.pathsep)
                    path.append("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/")
                    for prefix in path:
                        fn = os.path.join(prefix, "powershell.exe")
                        if not os.path.isdir(fn) and os.access(fn, os.X_OK):
                            PSHEXE=fn
                            break
                    if PSHEXE:
                        #process = subprocess.Popen([PSHEXE,"Get-CimInstance -ClassName Win32_serialport | Where-Object {$_.PNPDeviceID -like '*VID_6868&PID_8686*'} | Select -expandproperty DeviceID"],stdout=subprocess.PIPE);
                        process = subprocess.Popen([PSHEXE,"Get-PnPDevice -Class Ports -PresentOnly | where {$_.DeviceID -like '*VID_6868&PID_8686*'} | Select-Object FriendlyName | % FriendlyName | select-string COM\d+ |% { $_.matches.value }"],stdout=subprocess.PIPE);
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
            print(" { Chameleon connected } ")
        except Exception as e:
            print(f"Chameleon Connect fail: {str(e)}")


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
            self.cmd.set_reader_device_mode(True)
            print("Switch to {  Tag Reader  } mode successfully.")
        else:
            self.cmd.set_reader_device_mode(False)
            print("Switch to { Tag Emulator } mode successfully.")


@hw_mode.command('get', 'Get current device mode')
class HWModeGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def on_exec(self, args: argparse.Namespace):
        print(f"- Device Mode ( Tag {'Reader' if self.cmd.is_reader_device_mode() else 'Emulator'} )")


@hw_chipid.command('get', 'Get device chipset ID')
class HWChipIdGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print(f' - Device chip ID: ' + self.cmd.get_device_chip_id())


@hw_address.command('get', 'Get device address (used with Bluetooth)')
class HWAddressGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print(f' - Device address: ' + self.cmd.get_device_address())


@hw.command('version', 'Get current device firmware version')
class HWVersion(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        fw_version_int = self.cmd.get_firmware_version()
        fw_version = f'v{fw_version_int // 256}.{fw_version_int % 256}'
        git_version = self.cmd.get_git_version()
        print(f' - Version: {fw_version} ({git_version})')


@hf_14a.command('scan', 'Scan 14a tag, and print basic information')
class HF14AScan(ReaderRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def scan(self):
        resp: chameleon_com.Response = self.cmd.scan_tag_14a()
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            info = chameleon_cstruct.parse_14a_scan_tag_result(resp.data)
            print(f"- UID  Size: {info['uid_size']}")
            print(f"- UID  Hex : {info['uid_hex'].upper()}")
            print(f"- SAK  Hex : {info['sak_hex'].upper()}")
            print(f"- ATQA Hex : {info['atqa_hex'].upper()}")
            return True
        else:
            print("ISO14443-A Tag no found")
            return False

    def on_exec(self, args: argparse.Namespace):
        return self.scan()


@hf_14a.command('info', 'Scan 14a tag, and print detail information')
class HF14AInfo(ReaderRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def info(self):
        # detect mf1 support
        resp = self.cmd.detect_mf1_support()
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            # detect prng
            print("- Mifare Classic technology")
            resp = self.cmd.detect_mf1_nt_level()
            if resp.status == 0x00:
                prng_level = "Weak"
            elif resp.status == 0x24:
                prng_level = "Static"
            elif resp.status == 0x25:
                prng_level = "Hard"
            else:
                prng_level = "Unknown"
            print(f"  # Prng attack: {prng_level}")

    def on_exec(self, args: argparse.Namespace):
        # reused
        scan = HF14AScan()
        scan.device_com = self.device_com
        if scan.scan():
            self.info()


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
        # acquire
        dist_resp = self.cmd.detect_nt_distance(block_known, type_known, key_known)
        nt_resp = self.cmd.acquire_nested(block_known, type_known, key_known, block_target, type_target)
        # parse
        dist_obj = chameleon_cstruct.parse_nt_distance_detect_result(dist_resp.data)
        nt_obj = chameleon_cstruct.parse_nested_nt_acquire_group(nt_resp.data)
        # create cmd
        cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
        for nt_item in nt_obj:
            cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']} {nt_item['par']}"
        if sys.platform == "win32":
            cmd_recover = f"nested.exe {cmd_param}"
        else:
            cmd_recover = f"./nested {cmd_param}"
        # start a decrypt process
        process = self.sub_process(cmd_recover)

        # wait end
        while process.is_running():
            msg = f"   [ Time elapsed {process.get_time_distance()}ms ]\r"
            print(msg, end="")
        # clear \r
        print()

        if process.get_ret_code() == 0:
            output_str = process.get_output_sync()
            key_list = []
            for line in output_str.split('\n'):
                sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                if sea_obj is not None:
                    key_list.append(sea_obj[1])
            # 此处得先去验证一下密码，然后获得验证成功的那个
            # 如果没有验证成功的密码，则说明此次恢复失败了，可以重试一下
            print(f" - [{len(key_list)} candidate keys found ]")
            for key in key_list:
                key_bytes = bytearray.fromhex(key)
                ret = self.cmd.auth_mf1_key(block_target, type_target, key_bytes)
                if ret.status == chameleon_status.Device.HF_TAG_OK:
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
                print(f" - {colorama.Fore.CYAN}Nested recover one key running...{colorama.Style.RESET_ALL}")
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
            执行darkside采集与解密
        :param block_target:
        :param type_target:
        :return:
        """
        first_recover = True
        retry_count = 0
        while retry_count < 0xFF:
            darkside_resp = self.cmd.acquire_darkside(block_target, type_target, first_recover, 15)
            first_recover = False   # not first run.
            darkside_obj = chameleon_cstruct.parse_darkside_acquire_result(darkside_resp.data)
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
            # print(cmd_recover)
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
                    auth_ret = self.cmd.auth_mf1_key(block_target, type_target, key_bytes)
                    if auth_ret.status == chameleon_status.Device.HF_TAG_OK:
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
        resp = self.cmd.read_mf1_block(param.block, param.type, param.key)
        print(f" - Data: {resp.data.hex()}")


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
        resp = self.cmd.write_mf1_block(param.block, param.type, param.key, param.data)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            print(f" - {colorama.Fore.GREEN}Write done.{colorama.Style.RESET_ALL}")
        else:
            print(f" - {colorama.Fore.RED}Write fail.{colorama.Style.RESET_ALL}")


@hf_mf_detection.command('enable', 'Detection enable')
class HFMFDetectionEnable(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-e', '--enable', type=int, required=True, choices=[1, 0], help="1 = enable, 0 = disable")
        return parser

    # hf mf detection enable -e 1
    def on_exec(self, args: argparse.Namespace):
        enable = True if args.enable == 1 else False
        self.cmd.set_mf1_detection_enable(enable)
        print(f" - Set mf1 detection { 'enable' if enable else 'disable'}.")


@hf_mf_detection.command('count', 'Detection log count')
class HFMFDetectionLogCount(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hf mf detection count
    def on_exec(self, args: argparse.Namespace):
        data_bytes = self.cmd.get_mf1_detection_count().data
        count = int.from_bytes(data_bytes, "little", signed=False)
        print(f" - MF1 detection log count = {count}")


@hf_mf_detection.command('decrypt', 'Download log and decrypt keys')
class HFMFDetectionDecrypt(DeviceRequiredUnit):
    detection_log_size = 18

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def decrypt_by_list(self, rs: list):
        """
            从侦测日志列表中解密秘钥
        :param rs:
        :return:
        """
        keys = []
        for i in range(len(rs)):
            item0 = rs[i]
            for j in range(i + 1, len(rs)):
                item1 = rs[j]
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
                    keys.append(sea_obj[1])

        return keys

    # hf mf detection decrypt
    def on_exec(self, args: argparse.Namespace):
        buffer = bytearray()
        index = 0
        count = int.from_bytes(self.cmd.get_mf1_detection_count().data, "little", signed=False)
        if count == 0:
            print(" - No detection log to download")
            return
        print(f" - MF1 detection log count = {count}, start download", end="")
        while index < count:
            tmp = self.cmd.get_mf1_detection_log(index).data
            recv_count = int(len(tmp) / HFMFDetectionDecrypt.detection_log_size)
            index += recv_count
            buffer.extend(tmp)
            print(f".", end="")
        print()
        print(f" - Download done ({len(buffer)}bytes), start parse and decrypt")

        result_maps = chameleon_cstruct.parse_mf1_detection_result(buffer)
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
                if 'B' in result_maps_for_uid[block]:
                    # print(f" - B record: { result_maps[block]['B'] }")
                    records = result_maps_for_uid[block]['B']
                    if len(records) > 1:
                        result_maps[uid][block]['B'] = self.decrypt_by_list(records)
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
        while index < len(buffer):
            # split a block from buffer
            block_data = buffer[index: index + 16]
            index += 16
            # load to device
            self.cmd.set_mf1_block_data(block, block_data)
            print('.', end='')
            block += 1
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

        selected_slot = self.cmd.get_active_slot().data[0]
        slot_info = self.cmd.get_slot_info().data
        tag_type = chameleon_cmd.TagSpecificType(slot_info[selected_slot * 2])
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

        with open(file, 'wb') as fd:
            block = 0
            while block < block_count:
                response = self.cmd.get_mf1_block_data(block, 1)
                print('.', end='')
                block += 1
                if content_type == 'hex':
                    hex_char_repr = binascii.hexlify(response.data)
                    fd.write(hex_char_repr)
                    fd.write(bytes([0x0a]))
                else:
                    fd.write(response.data)

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
        parser.add_argument('--write', type=int, required=False,
                            help=f"Write mode: {help_str}",
                            default=-1, choices=chameleon_cmd.MifareClassicWriteMode.list())
        return parser

    # hf mf settings
    def on_exec(self, args: argparse.Namespace):
        if args.gen1a != -1:
            self.cmd.set_mf1_gen1a_mode(args.gen1a)
            print(f' - Set gen1a mode to {"enabled" if args.gen1a else "disabled"} success')
        if args.gen2 != -1:
            self.cmd.set_mf1_gen2_mode(args.gen2)
            print(f' - Set gen2 mode to {"enabled" if args.gen2 else "disabled"} success')
        if args.coll != -1:
            self.cmd.set_mf1_block_anti_coll_mode(args.coll)
            print(f' - Set anti-collision mode to {"enabled" if args.coll else "disabled"} success')
        if args.write != -1:
            self.cmd.set_mf1_write_mode(args.write)
            print(f' - Set write mode to {chameleon_cmd.MifareClassicWriteMode(args.write)} success')
        print(f' - Emulator settings updated')


@hf_mf.command('sim', 'Simulate a Mifare Classic card')
class HFMFSim(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('--sak', type=str, required=True, help="Select AcKnowledge(hex)", metavar="hex")
        parser.add_argument('--atqa', type=str, required=True, help="Answer To Request(hex)", metavar="hex")
        parser.add_argument('--uid', type=str, required=True, help="Unique ID(hex)", metavar="hex")
        return parser

    # hf mf sim --sak 08 --atqa 0400 --uid DEADBEEF
    def on_exec(self, args: argparse.Namespace):
        sak_str: str = args.sak.strip()
        atqa_str: str = args.atqa.strip()
        uid_str: str = args.uid.strip()

        if re.match(r"[a-fA-F0-9]{2}", sak_str) is not None:
            sak = bytearray.fromhex(sak_str)
        else:
            raise Exception("SAK must be hex(2byte)")

        if re.match(r"[a-fA-F0-9]{4}", atqa_str) is not None:
            atqa = bytearray.fromhex(atqa_str)
        else:
            raise Exception("ATQA must be hex(4byte)")

        if re.match(r"[a-fA-F0-9]+", uid_str) is not None:
            uid_len = len(uid_str)
            if uid_len != 8 and uid_len != 14 and uid_len != 20:
                raise Exception("UID length error")
            uid = bytearray.fromhex(uid_str)
        else:
            raise Exception("UID must be hex")

        self.cmd.set_mf1_anti_collision_res(sak, atqa, uid)
        print(" - Set anti-collision resources success")


@lf_em.command('read', 'Scan em410x tag and print id')
class LFEMRead(ReaderRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.read_em_410x()
        id_hex = resp.data.hex()
        print(f" - EM410x ID(10H): {colorama.Fore.GREEN}{id_hex}{colorama.Style.RESET_ALL}")


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
        id_bytes = bytearray.fromhex(id_hex)
        self.cmd.write_em_410x_to_t55xx(id_bytes)
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
                            help="Show slot nicknames and Mifare Classic emulator settings. 0 - skip, 1 - show ("
                                 "default)", choices=[0, 1], default=1)
        return parser

    def get_slot_name(self, slot, sense):
        try:
            return self.cmd.get_slot_tag_nick_name(slot, sense).data.decode()
        except UnexpectedResponseError:
            return "Empty"
        except UnicodeDecodeError:
            return "Non UTF-8"

    # hw slot list
    def on_exec(self, args: argparse.Namespace):
        data = self.cmd.get_slot_info().data
        selected = chameleon_cmd.SlotNumber.from_fw(self.cmd.get_active_slot().data[0])
        enabled = self.cmd.get_enabled_slots().data
        for slot in chameleon_cmd.SlotNumber:
            print(
                f' - Slot {slot} data{" (active)" if slot == selected else ""}'
                f'{" (disabled)" if not enabled[chameleon_cmd.SlotNumber.to_fw(slot)] else ""}:')
            print(
                f'   HF: '
                f'{(self.get_slot_name(slot, chameleon_cmd.TagSenseType.TAG_SENSE_HF) + " - ") if args.extend else ""}'
                f'{chameleon_cmd.TagSpecificType(data[chameleon_cmd.SlotNumber.to_fw(slot) * 2])}')
            print(
                f'   LF: '
                f'{(self.get_slot_name(slot, chameleon_cmd.TagSenseType.TAG_SENSE_LF) + " - ") if args.extend else ""}'
                f'{chameleon_cmd.TagSpecificType(data[chameleon_cmd.SlotNumber.to_fw(slot) * 2 + 1])}')
        if args.extend:
            config = self.cmd.get_mf1_emulator_settings().data
            print(' - Mifare Classic emulator settings:')
            print(f'   Detection (mfkey32) mode: {"enabled" if config[0] else "disabled"}')
            print(f'   Gen1A magic mode: {"enabled" if config[1] else "disabled"}')
            print(f'   Gen2 magic mode: {"enabled" if config[2] else "disabled"}')
            print(f'   Use anti-collision data from block 0: {"enabled" if config[3] else "disabled"}')
            print(f'   Write mode: {chameleon_cmd.MifareClassicWriteMode(config[4])}')


@hw_slot.command('change', 'Set emulation tag slot activated.')
class HWSlotSet(SlotIndexRequireUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_slot_args(parser)

    # hw slot change -s 1
    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd.set_slot_activated(slot_index)
        print(f" - Set slot {slot_index} activated success.")


class TagTypeRequiredUnit(DeviceRequiredUnit):

    @staticmethod
    def add_type_args(parser: ArgumentParserNoExit):
        type_choices = chameleon_cmd.TagSpecificType.list()
        help_str = ""
        for t in chameleon_cmd.TagSpecificType:
            if t == chameleon_cmd.TagSpecificType.TAG_TYPE_UNKNOWN:
                continue
            help_str += f"{t.value} = {t}, "
        help_str = help_str[:-2]
        parser.add_argument('-t', "--type", type=int, required=True, help=help_str, metavar="number",
                            choices=type_choices)
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
        print(f' - Set slot tag type success.')


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

    # m1 1k卡模拟 hw slot init -s 1 -t 3
    # em id卡模拟 hw slot init -s 1 -t 1
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        slot_num = args.slot
        self.cmd.set_slot_data_default(slot_num, tag_type)
        print(f' - Set slot tag data init success.')


@hw_slot.command('enable', 'Set emulation tag slot enable or disable')
class HWSlotEnableSet(SlotIndexRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        parser.add_argument('-e', '--enable', type=int, required=True, help="1 is Enable or 0 Disable", choices=[0, 1])
        return parser

    # hw slot enable -s 1 -e 0
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        enable = args.enable
        self.cmd.set_slot_enable(slot_num, enable)
        print(f' - Set slot {slot_num} {"enable" if enable else "disable"} success.')


@lf_em_sim.command('set', 'Set simulated em410x card id')
class LFEMSimSet(LFEMCardRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_card_arg(parser)

    # lf em sim set --id 4545454545
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytearray.fromhex(id_hex)
        self.cmd.set_em410x_sim_id(id_bytes)
        print(f' - Set em410x tag id success.')


@lf_em_sim.command('get', 'Get simulated em410x card id')
class LFEMSimGet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # lf em sim get
    def on_exec(self, args: argparse.Namespace):
        response = self.cmd.get_em410x_sim_id()
        print(f' - Get em410x tag id success.')
        print(f'ID: {response.data.hex()}')


@hw_slot_nick.command('set', 'Set tag nick name for slot')
class HWSlotNickSet(SlotIndexRequireUnit, SenseTypeRequireUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        parser.add_argument('-n', '--name', type=str, required=True, help="Your tag nick name for slot")
        return parser

    # hw slot nick set -s 1 -st 1 -n 测试名称保存
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        name: str = args.name
        uname = name.encode(encoding="utf8")
        if len(uname) > 32:
            raise ValueError("Your tag nick name too long.")
        self.cmd.set_slot_tag_nick_name(slot_num, sense_type, uname)
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
        res = self.cmd.get_slot_tag_nick_name(slot_num, sense_type)
        print(f' - Get tag nick name for slot {slot_num}: {res.data.decode()}')


@hw_slot.command('update', 'Update config & data to device flash')
class HWSlotUpdate(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw slot update
    def on_exec(self, args: argparse.Namespace):
        self.cmd.update_slot_data_config()
        print(f' - Update config and data from device memory to flash success.')


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
            self.cmd.set_slot_enable(slot, True)
            print(f' Slot {slot} setting done.')

        # update config and save to flash
        self.cmd.update_slot_data_config()
        print(f' - Succeeded opening all slots and setting data to default.')


@hw.command('dfu', 'Restart application to bootloader mode(Not yet implement dfu).')
class HWDFU(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw dfu
    def on_exec(self, args: argparse.Namespace):
        print("Application restarting...")
        self.cmd.enter_dfu_mode()
        # 理论上，上面的指令执行完成后，dfu模式会进入，然后USB会重启，
        # 我们判断是否成功进入USB，只需要判断USB是否变成DFU设备的VID和PID即可，
        # 同时我们记得确认设备的信息，一致时才是同一个设备。
        print(" - Enter success @.@~")
        # let time for comm thread to send dfu cmd and close port
        time.sleep(0.1)


@hw_settings_animation.command('get', 'Get current animation mode value')
class HWSettingsAnimationGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp: chameleon_com.Response = self.cmd.get_settings_animation()
        if resp.data[0] == 0:
            print("Full animation")
        elif resp.data[0] == 1:
            print("Minimal animation")
        elif resp.data[0] == 2:
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
        self.cmd.set_settings_animation(mode)
        print("Animation mode change success. Do not forget to store your settings in flash!")


@hw_settings.command('store', 'Store current settings to flash')
class HWSettingsStore(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print("Storing settings...")
        resp: chameleon_com.Response = self.cmd.store_settings()
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            print(" - Store success @.@~")
        else:
            print(" - Store failed")


@hw_settings.command('reset', 'Reset settings to default values')
class HWSettingsReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        print("Initializing settings...")
        resp: chameleon_com.Response = self.cmd.reset_settings()
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            print(" - Reset success @.@~")
        else:
            print(" - Reset failed")


@hw.command('factory_reset', 'Wipe all data and return to factory settings')
class HWFactoryReset(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit:
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
        resp = self.cmd.factory_reset()
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            print(" - Reset successful! Please reconnect.")
            print(" - A Serial Error below is normal, please ignore it")
        else:
            print(" - Reset failed!")


@hw.command('battery', 'Get battery information, voltage and level.')
class HWBatteryInfo(DeviceRequiredUnit):
    # How much remaining battery is considered low?
    BATTERY_LOW_LEVEL = 30

    def args_parser(self) -> ArgumentParserNoExit:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.battery_informartion()
        voltage =  int.from_bytes(resp.data[:2], 'big')
        percentage = resp.data[2]
        print(" - Battery informartion:")
        print(f"   voltage    -> {voltage}mV")
        print(f"   percentage -> {percentage}%")
        if percentage < HWBatteryInfo.BATTERY_LOW_LEVEL:
            print(f"{colorama.Fore.RED}[!] Low battery, please charge.{colorama.Style.RESET_ALL}")


@hw_settings_button_press.command('get', 'Get button press function of Button A and Button B.')
class HWButtonSettingsGet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        return None

    def on_exec(self, args: argparse.Namespace):
        # all button in here.
        button_list = [
            chameleon_cmd.ButtonType.ButtonA,
            chameleon_cmd.ButtonType.ButtonB,
        ]
        print("")
        for button in button_list:
            resp = self.cmd.get_button_press_fun(button)
            resp_long = self.cmd.get_long_button_press_fun(button)
            button_fn = chameleon_cmd.ButtonPressFunction.from_int(resp.data[0])
            button_long_fn = chameleon_cmd.ButtonPressFunction.from_int(resp_long.data[0])
            print(f" - {colorama.Fore.GREEN}{button}{colorama.Style.RESET_ALL}: {button_fn}")
            print(f"      usage: {button_fn.usage()}")
            print(f"      long press usage: {button_long_fn.usage()}")
            print("")
        print(" - Successfully get button function from settings")


@hw_settings_button_press.command('set', 'Set button press function of Button A and Button B.')
class HWButtonSettingsSet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.add_argument('-l', '--long', type=int, required=True, help="1 is Long or 0 Short", choices=[0, 1])
        parser.add_argument('-b', type=str, required=True,
                            help="Change the function of the pressed button(?).",
                            choices=chameleon_cmd.ButtonType.list_str())
        function_usage = ""
        for fun in chameleon_cmd.ButtonPressFunction:
            function_usage += f"{int(fun)} = {fun.usage()}, "
        function_usage = function_usage.rstrip(' ').rstrip(',')
        parser.add_argument('-f', type=int, required=True,
                            help=function_usage, choices=chameleon_cmd.ButtonPressFunction.list())
        return parser

    def on_exec(self, args: argparse.Namespace):
        button = chameleon_cmd.ButtonType.from_str(args.b)
        function = chameleon_cmd.ButtonPressFunction.from_int(args.f)
        long = args.long == 1
        if long:
            self.cmd.set_long_button_press_fun(button, function)
        else:
            self.cmd.set_button_press_fun(button, function)
        print(" - Successfully set button function to settings")
