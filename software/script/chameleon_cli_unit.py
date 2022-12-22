import os
import re
import subprocess
import argparse
import colorama
import timeit

import chameleon_com
import chameleon_cmd
import chameleon_cstruct
import chameleon_status

description_public = "Please enter correct parameters"


class ArgsParserError(Exception):
    pass


class ParserExitIntercept(Exception):
    pass


class ArgumentParserNoExit(argparse.ArgumentParser):
    """
        If arg ArgumentParser parse error, we can't exit process,
        we must raise exception to stop parse
    """

    def __init__(self, **args):
        super().__init__(*args)
        self.add_help = False
        self.description = description_public

    def exit(self, status: int = ..., message: str or None = ...):
        if message:
            raise ParserExitIntercept(message)

    def error(self, message: str):
        args = {'prog': self.prog, 'message': message}
        raise ArgsParserError('%(prog)s: error: %(message)s\n' % args)


class BaseCLIUnit:

    def __init__(self):
        # new a device command transfer and receiver instance(Send cmd and receive response)
        self._device_com: chameleon_com.ChameleonCom = None

    @property
    def device_com(self) -> chameleon_com.ChameleonCom:
        return self._device_com

    @device_com.setter
    def device_com(self, com):
        self._device_com = com

    @property
    def cmd_positive(self) -> chameleon_cmd.BaseChameleonCMD:
        return chameleon_cmd.PositiveChameleonCMD(self.device_com)

    @property
    def cmd_standard(self) -> chameleon_cmd.BaseChameleonCMD:
        return chameleon_cmd.BaseChameleonCMD(self.device_com)

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
    def sub_process(cmd, cwd=os.path.abspath("../bin/"), ):
        class ShadowProcess:
            def __init__(self):
                self.time_start = timeit.default_timer()
                self._process = subprocess.Popen(
                    cmd, cwd=cwd, shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
                )

            def get_time_distance(self, ms=True):
                ret = 0
                if ms:
                    ret = (timeit.default_timer() - self.time_start) * 1000
                else:
                    ret = timeit.default_timer() - self.time_start
                return round(ret, 2)

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


class ReaderRequiredUint(DeviceRequiredUnit):
    """
        Make sure of device enter to reader mode.
    """

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        if super(ReaderRequiredUint, self).before_exec(args):
            ret = self.cmd_standard.is_reader_device_mode()
            if ret:
                return True
            else:
                print("Please switch chameleon to reader mode(use 'hw mode').")
                return False
        return False

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class HWConnect(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-p', '--port', type=str, required=True)
        return parser

    def before_exec(self, args: argparse.Namespace):
        return True

    def on_exec(self, args: argparse.Namespace):
        try:
            self.device_com.open(args.port)
            print(" { Chameleon connected } ")
        except Exception as e:
            print(f"Chameleon Connect fail: {str(e)}")


class HWModeSet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        help_str = "reader or r = reader mode, emulator or e = tag emulator mode."
        parser.add_argument('-m', '--mode', type=str, required=True, choices=['reader', 'r', 'emulator', 'e'],
                            help=help_str)
        return parser

    def on_exec(self, args: argparse.Namespace):
        if args.mode == 'reader' or args.mode == 'r':
            self.cmd_standard.set_reader_device_mode(True)
            print("Switch to {  Tag Reader  } mode successfully.")
        else:
            self.cmd_standard.set_reader_device_mode(False)
            print("Switch to { Tag Emulator } mode successfully.")


class HWModeGet(DeviceRequiredUnit):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def on_exec(self, args: argparse.Namespace):
        print(f"- Device Mode ( Tag {'Reader' if self.cmd_standard.is_reader_device_mode() else 'Emulator'} )")


class HF14AScan(ReaderRequiredUint):
    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def scan(self):
        resp: chameleon_com.Response = self.cmd_standard.scan_tag_14a()
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


class HF14AInfo(ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        pass

    def info(self):
        # detect mf1 support
        resp = self.cmd_positive.detect_mf1_support()
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            # detect prng
            print("- Mifare Classic technology")
            resp = self.cmd_standard.detect_mf1_nt_level()
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


class HFMFNested(ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        type_choices = ['A', 'B', 'a', 'b']
        parser = ArgumentParserNoExit()
        parser.add_argument('-o', '--one', action='store_true', default=False,
                            help="one sector key recovery. Use block 0 Key A to find block 4 Key A")
        parser.add_argument('--block-known', type=int, required=True, metavar="decimal",
                            help="The block where the key of the card is known")
        parser.add_argument('--type-known', type=str, required=True, choices=type_choices,
                            help="The key type of the tag")
        parser.add_argument('--key-known', type=str, required=True, metavar="hex",
                            help="tag sector key")
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
        dist_resp = self.cmd_positive.detect_nt_distance(block_known, type_known, key_known)
        nt_resp = self.cmd_positive.acquire_nested(block_known, type_known, key_known, block_target, type_target)
        # parse
        dist_obj = chameleon_cstruct.parse_nt_distance_detect_result(dist_resp.data)
        nt_obj = chameleon_cstruct.parse_nested_nt_acquire_group(nt_resp.data)
        # create cmd
        cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
        for nt_item in nt_obj:
            cmd_param += f" {nt_item['nt']} {nt_item['nt_enc']} {nt_item['par']}"
        cmd_recover = f"nested.exe {cmd_param}"
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
                ret = self.cmd_standard.auth_mf1_key(block_target, type_target, key_bytes)
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


class HFMFDarkside(ReaderRequiredUint):

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
            darkside_resp = self.cmd_positive.acquire_darkside(block_target, type_target, first_recover, 15)
            first_recover = False   # not first run.
            darkside_obj = chameleon_cstruct.parse_darkside_acquire_result(darkside_resp.data)
            self.darkside_list.append(darkside_obj)
            recover_params = f"{darkside_obj['uid']}"
            for darkside_item in self.darkside_list:
                recover_params += f" {darkside_item['nt1']} {darkside_item['ks1']} {darkside_item['par']}"
                recover_params += f" {darkside_item['nr']} {darkside_item['ar']}"
            cmd_recover = f"darkside.exe {recover_params}"
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
                continue    # retry
            else:
                key_list = []
                for line in output_str.split('\n'):
                    sea_obj = re.search(r"([a-fA-F0-9]{12})", line)
                    if sea_obj is not None:
                        key_list.append(sea_obj[1])
                # auth key
                for key in key_list:
                    key_bytes = bytearray.fromhex(key)
                    auth_ret = self.cmd_positive.auth_mf1_key(block_target, type_target, key_bytes)
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


class BaseMF1AuthOpera(ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        type_choices = ['A', 'B', 'a', 'b']
        parser = ArgumentParserNoExit()
        parser.add_argument('-b', '--block', type=int, required=True, metavar="decimal",
                            help="The block where the key of the card is known")
        parser.add_argument('-t', '--type', type=str, required=True, choices=type_choices,
                            help="The key type of the tag")
        parser.add_argument('-k', '--key', type=str, required=True, metavar="hex",
                            help="tag sector key")
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


class HFMFRDBL(BaseMF1AuthOpera):

    # hf mf rdbl -b 2 -t A -k FFFFFFFFFFFF
    def on_exec(self, args: argparse.Namespace):
        param = self.get_param(args)
        resp = self.cmd_positive.read_mf1_block(param.block, param.type, param.key)
        print(f" - Data: {resp.data.hex()}")


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
        resp = self.cmd_standard.write_mf1_block(param.block, param.type, param.key, param.data)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            print(f" - {colorama.Fore.GREEN}Write done.{colorama.Style.RESET_ALL}")
        else:
            print(f" - {colorama.Fore.RED}Write fail.{colorama.Style.RESET_ALL}")


class LFEMRead(ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd_positive.read_em_410x()
        id_hex = resp.data.hex()
        print(f" - EM410x ID(10H): {colorama.Fore.GREEN}{id_hex}{colorama.Style.RESET_ALL}")


class LFEMWriteT55xx(ReaderRequiredUint):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument("--id", type=str, required=True, help="EM410x tag id", metavar="hex")
        return parser

    # lf em write --id 4400999559
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        if not re.match(r"^[a-fA-F0-9]{10}$", id_hex):
            raise ArgsParserError("ID must include 10 HEX symbols")
        id_bytes = bytearray.fromhex(id_hex)
        self.cmd_positive.write_em_410x_to_t55xx(id_bytes)
        print(f" - EM410x ID(10H): {id_hex} write done.")


class HWSlotSet(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        slot_choices = [1, 2, 3, 4, 5, 6, 7, 8]
        parser.add_argument('-s', "--slot", type=int, required=True,
                            help="Slot index", metavar="number", choices=slot_choices)
        return parser

    # hw slot set -s 1
    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd_positive.set_slot_activated(slot_index)
        print(f" - Set slot {slot_index} activated success.")


class TagTypeRequiredUint(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        type_choices = chameleon_cmd.TagSpecificType.list()
        help_str = ""
        for name, value in chameleon_cmd.TagSpecificType.__members__.items():
            if value == chameleon_cmd.TagSpecificType.TAG_TYPE_UNKNOWN:
                continue
            help_str += f"{value} = {name.replace('TAG_TYPE_', '')}, "
        parser.add_argument('-t', "--type", type=int, required=True, help=help_str,
                            metavar="number", choices=type_choices)
        return parser

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


class HWSlotTagType(TagTypeRequiredUint):

    # hw slot tagtype -t 2
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        self.cmd_positive.set_slot_tag_type(tag_type)
        print(f' - Set slot tag type success.')


class HWSlotDataDefault(TagTypeRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = super(HWSlotDataDefault, self).args_parser()
        slot_choices = [1, 2, 3, 4, 5, 6, 7, 8]
        parser.add_argument('-s', "--slot", type=int, required=True,
                            help="Slot index", metavar="number", choices=slot_choices)
        return parser

    # hw slot init -s 1 -t 2
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        slot_num = args.slot
        self.cmd_positive.set_slot_data_default(slot_num, tag_type)
        print(f' - Set slot tag data init success.')
