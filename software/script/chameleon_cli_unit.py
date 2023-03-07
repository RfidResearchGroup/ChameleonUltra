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


class HFMFDetectionEnable(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-e', '--enable', type=int, required=True, choices=[1, 0],
                            help="1 = enable, 0 = disable")
        return parser

    # hf mf detection enable -e 1
    def on_exec(self, args: argparse.Namespace):
        enable = True if args.enable == 1 else False
        self.cmd_positive.set_mf1_detection_enable(enable)
        print(f" - Set mf1 detection { 'enable' if enable else 'disable'}.")


class HFMFDetectionLogCount(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hf mf detection count
    def on_exec(self, args: argparse.Namespace):
        data_bytes = self.cmd_standard.get_mf1_detection_count().data
        count = int.from_bytes(data_bytes, "little", signed=False)
        print(f" - MF1 detection log count = {count}")


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
                cmd_recover = f"mfkey32v2.exe {cmd_base}"
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
        count = int.from_bytes(self.cmd_standard.get_mf1_detection_count().data, "little", signed=False)
        if count == 0:
            print(" - No detection log to download")
            return
        print(f" - MF1 detection log count = {count}, start download", end="")
        while index < count:
            tmp = self.cmd_positive.get_mf1_detection_log(index).data
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


class HFMFELoad(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        parser.add_argument('-f', '--file', type=str, required=True, help="file path")
        parser.add_argument('-t', '--type', type=str, required=True, help="content type", choices=['bin', 'hex'])
        return parser

    # hf mf eload -f test.bin -t bin
    # hf mf eload -f test.eml -t hex
    def on_exec(self, args: argparse.Namespace):
        file = args.file
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
            self.cmd_positive.set_mf1_block_data(block, block_data)
            print('.', end='')
            block += 1
        print("\n - Load success")


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

        if re.match('[a-fA-F0-9]{2}', sak_str) is not None:
            sak = bytearray.fromhex(sak_str)
        else:
            raise Exception("SAK must be hex(2byte)")

        if re.match('[a-fA-F0-9]{4}', atqa_str) is not None:
            atqa = bytearray.fromhex(atqa_str)
        else:
            raise Exception("ATQA must be hex(4byte)")

        if re.match('[a-fA-F0-9]+', uid_str) is not None:
            uid_len = len(uid_str)
            if uid_len != 8 and uid_len != 14 and uid_len != 20:
                raise Exception("UID length error")
            uid = bytearray.fromhex(uid_str)
        else:
            raise Exception("UID must be hex")

        self.cmd_positive.set_mf1_anti_collision_res(sak, atqa, uid)
        print(" - Set anti-collision resources success")


class LFEMRead(ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd_positive.read_em_410x()
        id_hex = resp.data.hex()
        print(f" - EM410x ID(10H): {colorama.Fore.GREEN}{id_hex}{colorama.Style.RESET_ALL}")


class LFEMCardRequiredUint(DeviceRequiredUnit):

    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit):
        parser.add_argument("--id", type=str, required=True, help="EM410x tag id", metavar="hex")
        return parser

    def before_exec(self, args: argparse.Namespace):
        if super(LFEMCardRequiredUint, self).before_exec(args):
            if not re.match(r"^[a-fA-F0-9]{10}$", args.id):
                raise ArgsParserError("ID must include 10 HEX symbols")
            return True
        return False

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError("Please implement this")

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")


class LFEMWriteT55xx(LFEMCardRequiredUint, ReaderRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_card_arg(parser)

    def before_exec(self, args: argparse.Namespace):
        b1 = super(LFEMCardRequiredUint, self).before_exec(args)
        b2 = super(ReaderRequiredUint, self).before_exec(args)
        return b1 and b2

    # lf em write --id 4400999559
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytearray.fromhex(id_hex)
        self.cmd_positive.write_em_410x_to_t55xx(id_bytes)
        print(f" - EM410x ID(10H): {id_hex} write done.")


class SlotIndexRequireUint(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()

    @staticmethod
    def add_slot_args(parser: ArgumentParserNoExit):
        slot_choices = [1, 2, 3, 4, 5, 6, 7, 8]
        parser.add_argument('-s', "--slot", type=int, required=True,
                            help="Slot index", metavar="number", choices=slot_choices)
        return parser
    
class SenseTypeRequireUint(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()

    @staticmethod
    def add_sense_type_args(parser: ArgumentParserNoExit):
        slot_choices = [1, 2]
        parser.add_argument('-st', "--sense_type", type=int, required=True,
                            help="Sense type", metavar="number", choices=slot_choices)
        return parser


class HWSlotSet(SlotIndexRequireUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_slot_args(parser)

    # hw slot change -s 1
    def on_exec(self, args: argparse.Namespace):
        slot_index = args.slot
        self.cmd_positive.set_slot_activated(slot_index)
        print(f" - Set slot {slot_index} activated success.")


class TagTypeRequiredUint(DeviceRequiredUnit):

    @staticmethod
    def add_type_args(parser: ArgumentParserNoExit):
        type_choices = chameleon_cmd.TagSpecificType.list()
        help_str = ""
        for name, value in chameleon_cmd.TagSpecificType.__members__.items():
            if value == chameleon_cmd.TagSpecificType.TAG_TYPE_UNKNOWN:
                continue
            help_str += f"{value} = {name.replace('TAG_TYPE_', '')}, "
        parser.add_argument('-t', "--type", type=int, required=True, help=help_str,
                            metavar="number", choices=type_choices)
        return parser

    def args_parser(self) -> ArgumentParserNoExit or None:
        raise NotImplementedError()

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError()


class HWSlotTagType(TagTypeRequiredUint, SlotIndexRequireUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_type_args(parser)
        self.add_slot_args(parser)
        return parser

    # hw slot tagtype -t 2
    def on_exec(self, args: argparse.Namespace):
        tag_type = args.type
        slot_index = args.slot
        self.cmd_positive.set_slot_tag_type(slot_index, tag_type)
        print(f' - Set slot tag type success.')


class HWSlotDataDefault(TagTypeRequiredUint, SlotIndexRequireUint):

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
        self.cmd_positive.set_slot_data_default(slot_num, tag_type)
        print(f' - Set slot tag data init success.')


class HWSlotEnableSet(SlotIndexRequireUint):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        parser.add_argument('-e', '--enable', type=int, required=True, help="1 is Enable or 0 Disable", choices=[0, 1])
        return parser

    # hw slot enable -s 1 -e 0
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        enable = args.enable
        self.cmd_positive.set_slot_enable(slot_num, enable)
        print(f' - Set slot {slot_num} {"enable" if enable else "disable"} success.')


class LFEMSim(LFEMCardRequiredUint):

    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        return self.add_card_arg(parser)

    # lf em sim --id 4545454545
    def on_exec(self, args: argparse.Namespace):
        id_hex = args.id
        id_bytes = bytearray.fromhex(id_hex)
        self.cmd_positive.set_em140x_sim_id(id_bytes)
        print(f' - Set em410x tag id success.')


class HWSlotNickSet(SlotIndexRequireUint, SenseTypeRequireUint):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        parser.add_argument('-n', '--name', type=str, required=True, help="Yout tag nick name for slot")
        return parser

    # hw slot nick set -s 1 -st 1 -n 测试名称保存
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        name: str = args.name
        if len(name.encode(encoding="gbk")) > 32:
            raise ValueError("Your tag nick name too long.")
        self.cmd_positive.set_slot_tag_nick_name(slot_num, sense_type, name)
        print(f' - Set tag nick name for slot {slot_num} success.')


class HWSlotNickGet(SlotIndexRequireUint, SenseTypeRequireUint):
    def args_parser(self) -> ArgumentParserNoExit or None:
        parser = ArgumentParserNoExit()
        self.add_slot_args(parser)
        self.add_sense_type_args(parser)
        return parser

    # hw slot nick get -s 1 -st 1
    def on_exec(self, args: argparse.Namespace):
        slot_num = args.slot
        sense_type = args.sense_type
        res = self.cmd_positive.get_slot_tag_nick_name(slot_num, sense_type)
        print(f' - Get tag nick name for slot {slot_num}: {res.data.decode(encoding="gbk")}')


class HWSlotUpdate(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None

    # hw slot update
    def on_exec(self, args: argparse.Namespace):
        self.cmd_positive.update_slot_data_config()
        print(f' - Update config and data from device memory to flash success.')


class HWSlotOpenAll(DeviceRequiredUnit):

    def args_parser(self) -> ArgumentParserNoExit or None:
        return None
    
    # hw slot openall
    def on_exec(self, args: argparse.Namespace):
        # what type you need set to default?
        hf_type = chameleon_cmd.TagSpecificType.TAG_TYPE_MIFARE_1024
        lf_type = chameleon_cmd.TagSpecificType.TAG_TYPE_EM410X

        # set all slot
        for slot in range(8):
            # the slot not from 0 offset, so we can inc 1.
            slot = slot + 1
            print(f' Slot{slot} setting...')
            # first to set tag type
            self.cmd_positive.set_slot_tag_type(slot, hf_type)
            self.cmd_positive.set_slot_tag_type(slot, lf_type)
            # to init default data
            self.cmd_positive.set_slot_data_default(slot, hf_type)
            self.cmd_positive.set_slot_data_default(slot, lf_type)
            # finally, we can enable this slot.
            self.cmd_positive.set_slot_enable(slot, True)
            print(f' Open slot{slot} finish')

        # update config and save to flash
        self.cmd_positive.update_slot_data_config()
        print(f' - Open all slot and set data to default success.')
