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
from chameleon_utils import CR, CG, CY, color_string

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
        # new a device communication instance(only communication)
        self.device_com = chameleon_com.ChameleonCom()

    def get_cmd_node(
        self, node: chameleon_utils.CLITree, cmdline: list[str]
    ) -> tuple[chameleon_utils.CLITree, list[str]]:
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
        if self.device_com.isOpen():
            status = color_string((CG, "USB"))
        else:
            status = color_string((CR, "Offline"))

        return ANSI(f"[{status}] chameleon --> ")

    @staticmethod
    def print_banner():
        """
            print chameleon ascii banner.

        :return:
        """
        print(color_string((CG, BANNER)))

    def exec_cmd(self, cmd_str):
        if cmd_str == "":
            return

        # look for alternate exit
        if cmd_str in ["quit", "q", "e"]:
            cmd_str = "exit"

        # look for alternate comments
        if cmd_str[0] in ";#%":
            cmd_str = "rem " + cmd_str[1:].lstrip()

        # parse cmd
        argv = cmd_str.split()

        tree_node, arg_list = self.get_cmd_node(chameleon_cli_unit.root, argv)
        if not tree_node.cls:
            # Found tree node is a group without an implementation, print children
            print("".ljust(18, "-") + "".ljust(10) + "".ljust(30, "-"))
            for child in tree_node.children:
                cmd_title = color_string((CG, child.name))
                if not child.cls:
                    help_line = (
                        f" - {cmd_title}".ljust(37)
                    ) + f"{{ {child.help_text}... }}"
                else:
                    help_line = (f" - {cmd_title}".ljust(37)) + f"{child.help_text}"
                print(help_line)
            return

        unit: chameleon_cli_unit.BaseCLIUnit = tree_node.cls()
        unit.device_com = self.device_com
        args_parse_result = unit.args_parser()

        assert args_parse_result is not None
        args: argparse.ArgumentParser = args_parse_result
        args.prog = tree_node.fullname
        try:
            args_parse_result = args.parse_args(arg_list)
            if args.help_requested:
                return
        except chameleon_utils.ArgsParserError as e:
            args.print_help()
            print(color_string((CY, str(e).strip())))
            return
        except chameleon_utils.ParserExitIntercept:
            # don't exit process.
            return
        try:
            # before process cmd, we need to do something...
            if not unit.before_exec(args_parse_result):
                return

            # start process cmd, delay error to call after_exec firstly
            error = None
            try:
                unit.on_exec(args_parse_result)
            except Exception as e:
                error = e
            unit.after_exec(args_parse_result)
            if error is not None:
                raise error

        except (
            chameleon_utils.UnexpectedResponseError,
            chameleon_utils.ArgsParserError,
        ) as e:
            print(color_string((CR, str(e))))
        except Exception:
            print(f"CLI exception: {color_string((CR, traceback.format_exc()))}")

    def startCLI(self):
        """
            start listen input.

        :return:
        """
        self.completer = chameleon_utils.CustomNestedCompleter.from_clitree(
            chameleon_cli_unit.root
        )
        self.session = prompt_toolkit.PromptSession(
            completer=self.completer,
            history=FileHistory(str(pathlib.Path.home() / ".chameleon_history")),
        )

        self.print_banner()
        cmd_strs = []
        while True:
            if cmd_strs:
                cmd_str = cmd_strs.pop(0)
            else:
                # wait user input
                try:
                    cmd_str = self.session.prompt(self.get_prompt()).strip()
                    cmd_strs = (
                        cmd_str.replace("\r\n", "\n").replace("\r", "\n").split("\n")
                    )
                    cmd_str = cmd_strs.pop(0)
                except EOFError:
                    cmd_str = "exit"
                except KeyboardInterrupt:
                    cmd_str = "exit"
            self.exec_cmd(cmd_str)


if __name__ == "__main__":
    if sys.version_info < (3, 9):
        raise Exception("This script requires at least Python 3.9")
    colorama.init(autoreset=True)
    chameleon_cli_unit.check_tools()
    ChameleonCLI().startCLI()
