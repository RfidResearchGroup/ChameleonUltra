#!/usr/bin/env python3
import argparse
import sys
import traceback
import chameleon_com
import colorama
import chameleon_cli_unit
import chameleon_utils
import pathlib
import prompt_toolkit
from prompt_toolkit.formatted_text import ANSI
from prompt_toolkit.history import FileHistory

# Colorama shorthands
CR = colorama.Fore.RED
CG = colorama.Fore.GREEN
CB = colorama.Fore.BLUE
CC = colorama.Fore.CYAN
CY = colorama.Fore.YELLOW
CM = colorama.Fore.MAGENTA
C0 = colorama.Style.RESET_ALL

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
BANNER = """
 ██████╗██╗  ██╗ █████╗ ██╗   ██╗███████╗██╗     ███████╗ █████╗ ██╗  ██╗
██╔════╝██║  ██║██╔══██╗███╗ ███║██╔════╝██║     ██╔════╝██╔══██╗███╗ ██║
██║     ███████║███████║████████║█████╗  ██║     █████╗  ██║  ██║████╗██║
██║     ██╔══██║██╔══██║██╔██╔██║██╔══╝  ██║     ██╔══╝  ██║  ██║██╔████║
╚██████╗██║  ██║██║  ██║██║╚═╝██║███████╗███████╗███████╗╚█████╔╝██║╚███║
 ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝   ╚═╝╚══════╝╚══════╝╚══════╝ ╚════╝ ╚═╝ ╚══╝
"""


class ChameleonCLI:
    """
        CLI for chameleon
    """

    def __init__(self):
        self.completer = chameleon_utils.CustomNestedCompleter.from_clitree(chameleon_cli_unit.root)
        self.session = prompt_toolkit.PromptSession(completer=self.completer,
                                                    history=FileHistory(pathlib.Path.home() / ".chameleon_history"))

        # new a device communication instance(only communication)
        self.device_com = chameleon_com.ChameleonCom()

    def get_cmd_node(self, node: chameleon_utils.CLITree,
                     cmdline: list[str]) -> tuple[chameleon_utils.CLITree, list[str]]:
        """
        Recursively traverse the command line tree to get to the matching node

        :return: last matching CLITree node, remaining tokens
        """
        # No more subcommands to parse, return node
        if cmdline == []:
            return node, []

        for child in node.children:
            if cmdline[0] == child.name:
                return self.get_cmd_node(child, cmdline[1:])

        # No matching child node
        return node, cmdline[:]

    def get_prompt(self):
        """
        Retrieve the cli prompt

        :return: current cmd prompt
        """
        device_string = f"{CG}USB" if self.device_com.isOpen(
        ) else f"{CR}Offline"
        status = f"[{device_string}{C0}] chameleon --> "
        return status

    @staticmethod
    def print_banner():
        """
            print chameleon ascii banner
        :return:
        """
        print(f"{CY}{BANNER}{C0}")

    def startCLI(self):
        """
            start listen input.
        :return:
        """
        if sys.version_info < (3, 9):
            raise Exception("This script requires at least Python 3.9")

        self.print_banner()
        closing = False
        cmd_strs = []
        while True:
            if cmd_strs:
                cmd_str = cmd_strs.pop(0)
            else:
                # wait user input
                try:
                    cmd_str = self.session.prompt(
                        ANSI(self.get_prompt())).strip()
                    cmd_strs = cmd_str.replace(
                        "\r\n", "\n").replace("\r", "\n").split("\n")
                    cmd_str = cmd_strs.pop(0)
                except EOFError:
                    closing = True
                except KeyboardInterrupt:
                    closing = True

            # look for alternate exit
            if closing or cmd_str in ["quit", "q", "e"]:
                cmd_str = 'exit'

            # empty line
            if cmd_str == "":
                continue

            # look for alternate comments
            if cmd_str[0] in ";#%":
                cmd_str = 'rem ' + cmd_str[1:].lstrip()

            # parse cmd
            argv = cmd_str.split()

            tree_node, arg_list = self.get_cmd_node(chameleon_cli_unit.root, argv)
            if not tree_node.cls:
                # Found tree node is a group without an implementation, print children
                print("".ljust(18, "-") + "".ljust(10) + "".ljust(30, "-"))
                for child in tree_node.children:
                    cmd_title = f"{CG}{child.name}{C0}"
                    if not child.cls:
                        help_line = (f" - {cmd_title}".ljust(37)) + f"{{ {child.help_text}... }}"
                    else:
                        help_line = (f" - {cmd_title}".ljust(37)) + f"{child.help_text}"
                    print(help_line)
                continue

            unit: chameleon_cli_unit.BaseCLIUnit = tree_node.cls()
            unit.device_com = self.device_com
            args_parse_result = unit.args_parser()

            assert args_parse_result is not None
            args: argparse.ArgumentParser = args_parse_result
            args.prog = tree_node.fullname
            try:
                args_parse_result = args.parse_args(arg_list)
            except chameleon_utils.ArgsParserError as e:
                args.print_usage()
                print(str(e).strip(), end="\n\n")
                continue
            except chameleon_utils.ParserExitIntercept:
                # don't exit process.
                continue
            try:
                # before process cmd, we need to do something...
                if not unit.before_exec(args_parse_result):
                    continue

                # start process cmd, delay error to call after_exec firstly
                error = None
                try:
                    unit.on_exec(args_parse_result)
                except Exception as e:
                    error = e
                unit.after_exec(args_parse_result)
                if error is not None:
                    raise error

            except (chameleon_utils.UnexpectedResponseError, chameleon_utils.ArgsParserError) as e:
                print(f"{CR}{str(e)}{C0}")
            except Exception:
                print(
                    f"CLI exception: {CR}{traceback.format_exc()}{C0}")


if __name__ == '__main__':
    colorama.init(autoreset=True)
    ChameleonCLI().startCLI()
