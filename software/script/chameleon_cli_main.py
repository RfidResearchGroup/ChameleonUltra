import argparse
import os
import platform
import sys
import traceback
import types
import chameleon_com
import chameleon_cmd
import colorama
import chameleon_cli_unit


ULTRA = r"""
                                                                ╦ ╦╦ ╔╦╗╦═╗╔═╗
                                                   ███████      ║ ║║  ║ ╠╦╝╠═╣
                                                                ╚═╝╩═╝╩ ╩╚═╩ ╩
"""

LITE = r"""
                                                                ╦  ╦╔╦╗╔═╗
                                                   ███████      ║  ║ ║ ║╣ 
                                                                ╩═╝╩ ╩ ╚═╝                                                
"""

# create by http://patorjk.com/software/taag/#p=display&f=ANSI%20Shadow&t=Chameleon%20Ultra
BANNER = f"""
 ██████╗██╗  ██╗ █████╗ ███╗   ███╗███████╗██╗     ███████╗ ██████╗ ███╗   ██╗
██╔════╝██║  ██║██╔══██╗████╗ ████║██╔════╝██║     ██╔════╝██╔═══██╗████╗  ██║
██║     ███████║███████║██╔████╔██║█████╗  ██║     █████╗  ██║   ██║██╔██╗ ██║
██║     ██╔══██║██╔══██║██║╚██╔╝██║██╔══╝  ██║     ██╔══╝  ██║   ██║██║╚██╗██║
╚██████╗██║  ██║██║  ██║██║ ╚═╝ ██║███████╗███████╗███████╗╚██████╔╝██║ ╚████║
 ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝

"""


def new_uint(unit_clz, help_msg):
    """
        new a uint dict object
    :param unit_clz: unit implement class
    :param help_msg: unit usage
    :return: a dict...
    """
    return {
        'unit': unit_clz,
        'help': help_msg,
    }


class ChameleonCLI:
    """
        CLI for chameleon
    """

    def __init__(self):
        self.cmd_maps = {
            'hw': {
                'connect': new_uint(chameleon_cli_unit.HWConnect, "Connect to chameleon by serial port"),
                'mode': {
                    'set': new_uint(chameleon_cli_unit.HWModeSet, "Change device mode to tag reader or tag emulator"),
                    'get': new_uint(chameleon_cli_unit.HWModeGet, "Get current device mode"),
                    'help': "Device mode get/set"
                },
                'slot': {
                    'change': new_uint(chameleon_cli_unit.HWSlotSet, "Set emulation tag slot activated."),
                    'type': new_uint(chameleon_cli_unit.HWSlotTagType, "Set emulation tag type"),
                    'init': new_uint(chameleon_cli_unit.HWSlotDataDefault, "Set emulation tag data to default"),
                    'enable': new_uint(chameleon_cli_unit.HWSlotEnableSet, "Set emulation tag slot enable or disable"),
                    'nick': {
                        'set': new_uint(chameleon_cli_unit.HWSlotNickSet, "Set tag nick name for slot"),
                        'get': new_uint(chameleon_cli_unit.HWSlotNickGet, "Get tag nick name for slot"),
                        'help': "Get/Set tag nick name for slot",
                    },
                    'update': new_uint(chameleon_cli_unit.HWSlotUpdate, "Update config & data to device flash"),
                    'help': "Emulation tag slot.",
                },
                'help': "hardware controller",
            },
            'hf': {
                '14a': {
                    'scan': new_uint(chameleon_cli_unit.HF14AScan, "Scan 14a tag, and print basic information"),
                    'info': new_uint(chameleon_cli_unit.HF14AInfo, "Scan 14a tag, and print detail information"),
                    'help': "ISO14443-a tag read/write/info...",
                },
                'mf': {
                    'nested': new_uint(chameleon_cli_unit.HFMFNested, "Mifare Classic nested recover key"),
                    'darkside': new_uint(chameleon_cli_unit.HFMFDarkside, "Mifare Classic darkside recover key"),
                    'rdbl': new_uint(chameleon_cli_unit.HFMFRDBL, "MiFARE Classic read one block"),
                    'wrbl': new_uint(chameleon_cli_unit.HFMFWRBL, "MiFARE Classic write one block"),
                    'detection': {
                        'enable': new_uint(chameleon_cli_unit.HFMFDetectionEnable, "Detection enable"),
                        'count': new_uint(chameleon_cli_unit.HFMFDetectionLogCount, "Detection log count"),
                        'decrypt': new_uint(chameleon_cli_unit.HFMFDetectionDecrypt, "Download log and decrypt keys"),
                        'help': "Mifare Classic detection log"
                    },
                    'sim': new_uint(chameleon_cli_unit.HFMFSim, "Simulation a mifare classic card"),
                    'eload': new_uint(chameleon_cli_unit.HFMFELoad, "Load data to emulator memory"),
                    'help': "Mifare Classic mini/1/2/4, attack/read/write"
                },
                'help': "high frequency tag/reader",
            },
            'lf': {
                'em': {
                    'read': new_uint(chameleon_cli_unit.LFEMRead, "Scan em410x tag and print id"),
                    'write': new_uint(chameleon_cli_unit.LFEMWriteT55xx, "Write em410x id to t55xx"),
                    'sim': new_uint(chameleon_cli_unit.LFEMSim, "Simulation a em410x id card"),
                    'help': "EM410x read/write/emulator",
                },
                'help': "low frequency tag/reader",
            }
        }

        # new a device communication instance(only communication)
        self.device_com = chameleon_com.ChameleonCom()

    @staticmethod
    def print_banner():
        """
            print chameleon ascii banner
        :return:
        """
        print(colorama.Fore.YELLOW + BANNER)

    def parse_cli_cmd(self, cmd_str):
        """
            parse cmd from str
        :param cmd_str:
        :return:
        """
        cmds = cmd_str.split(" ")
        cmd_maps: dict or types.FunctionType = self.cmd_maps
        cmd_end = ""
        for cmd in cmds:
            if cmd in cmd_maps:  # CMD found in map, we can continue find next
                cmd_maps = cmd_maps[cmd]
                cmd_end = cmd
            else:  # CMD not found
                break
        cmd_end_position = cmd_str.index(cmd_end) + len(cmd_end) + 1
        return cmd_maps, (cmd_str[:cmd_end_position], cmd_str[cmd_end_position:])

    def startCLI(self):
        """
            start listen input.
        :return:
        """
        self.print_banner()
        while True:
            # wait user input
            status = f"{colorama.Fore.GREEN}USB" if self.device_com.isOpen() else f"{colorama.Fore.RED}Offline"
            print(f"[{status}{colorama.Style.RESET_ALL}] chameleon --> ", end="")
            cmd_str = input().strip()

            # clear screen
            if cmd_str == "clear":
                if platform.system() == 'Windows':
                    os.system("cls")
                elif platform.system() == 'Linux':
                    os.system("clear")
                else:
                    print("No screen clear implement")
                continue

            if cmd_str == "exit":
                print("Bye, thanks for you.  ^.^ ")
                sys.exit(996)

            # parse cmd
            cmd_map, args_str = self.parse_cli_cmd(cmd_str)
            is_exec_map = 'unit' in cmd_map
            if is_exec_map:
                # new a unit instance
                unit_clz = cmd_map['unit']
                if callable(unit_clz):
                    unit: chameleon_cli_unit.BaseCLIUnit = unit_clz()
                else:
                    raise TypeError("CMD unit is not a 'BaseCLIUnit'")
                # set variables of required
                unit.device_com = self.device_com
                # parse args
                args_parse_result = unit.args_parser()
                if args_parse_result is not None:
                    args: argparse.ArgumentParser = args_parse_result
                    args.prog = args_str[0]
                    try:
                        args_parse_result = args.parse_args(args_str[1].split())
                    except chameleon_cli_unit.ArgsParserError as e:
                        args.print_usage()
                        print(str(e).strip(), end="\n\n")
                        continue
                    except chameleon_cli_unit.ParserExitIntercept:
                        # don't exit process.
                        continue
                # noinspection PyBroadException
                try:
                    # before process cmd, we need to do something...
                    if not unit.before_exec(args_parse_result):
                        continue
                    # start process cmd
                    unit.on_exec(args_parse_result)
                except (chameleon_cmd.NegativeResponseError, chameleon_cli_unit.ArgsParserError) as e:
                    print(f"{colorama.Fore.RED}{str(e)}{colorama.Style.RESET_ALL}")
                except Exception:
                    print(f"CLI exception: {colorama.Fore.RED}{traceback.format_exc()}{colorama.Style.RESET_ALL}")
            elif isinstance(cmd_map, dict):
                print("".ljust(18, "-") + "".ljust(10) + "".ljust(30, "-"))
                for map_key in cmd_map:
                    map_item = cmd_map[map_key]
                    if 'help' in map_item:
                        cmd_title = f"{colorama.Fore.GREEN}{map_key}{colorama.Style.RESET_ALL}"
                        help_line = (f" - {cmd_title}".ljust(37)) + f"[ {map_item['help']} ]"
                        print(help_line)


if __name__ == '__main__':
    colorama.init(autoreset=True)
    ChameleonCLI().startCLI()
