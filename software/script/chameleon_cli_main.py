#!/usr/bin/env python3
import argparse
import platform
import sys
import traceback
import types
import chameleon_com
import chameleon_cmd
import colorama
import chameleon_cli_unit
import chameleon_utils
import os
import pathlib
import prompt_toolkit
from prompt_toolkit.formatted_text import ANSI
from prompt_toolkit.history import FileHistory


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
        self.completer = chameleon_utils.CustomNestedCompleter.from_nested_dict(chameleon_cli_unit.root_commands)
        self.session = prompt_toolkit.PromptSession(completer=self.completer, history=FileHistory(pathlib.Path.home() / ".chameleon_history"))

        # new a device communication instance(only communication)
        self.device_com = chameleon_com.ChameleonCom()

    def get_cmd_node(self, node: chameleon_utils.CLITree, cmdline: list[str]) -> tuple[chameleon_utils.CLITree, list[str]]:
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
        device_string = f"{colorama.Fore.GREEN}USB" if self.device_com.isOpen(
        ) else f"{colorama.Fore.RED}Offline"
        status = f"[{device_string}{colorama.Style.RESET_ALL}] chameleon --> "
        return status

    @staticmethod
    def print_banner():
        """
            print chameleon ascii banner
        :return:
        """
        print(colorama.Fore.YELLOW + BANNER)

    def startCLI(self):
        """
            start listen input.
        :return:
        """
        self.print_banner()
        closing = False
        while True:
            # wait user input
            try:
                cmd_str = self.session.prompt(ANSI(self.get_prompt())).strip()
            except EOFError:
                closing = True
            except KeyboardInterrupt:
                closing = True

            if closing or cmd_str in ["exit", "quit", "q", "e"]:
                print("Bye, thank you.  ^.^ ")
                self.device_com.close()
                sys.exit(996)
            elif cmd_str == "clear":
                os.system('clear' if os.name == 'posix' else 'cls')
                continue
            elif cmd_str == "":
                continue

            # parse cmd
            argv = cmd_str.split()
            root_cmd = argv[0]
            if root_cmd not in chameleon_cli_unit.root_commands:
                # No matching command group
                print("".ljust(18, "-") + "".ljust(10) + "".ljust(30, "-"))
                for cmd_name, cmd_node in chameleon_cli_unit.root_commands.items():
                    cmd_title = f"{colorama.Fore.GREEN}{cmd_name}{colorama.Style.RESET_ALL}"
                    help_line = (f" - {cmd_title}".ljust(37)) + f"[ {cmd_node.helptext} ]"
                    print(help_line)
                continue

            tree_node, arg_list = self.get_cmd_node(chameleon_cli_unit.root_commands[root_cmd], argv[1:])

            if not tree_node.cls:
                # Found tree node is a group without an implementation, print children
                print("".ljust(18, "-") + "".ljust(10) + "".ljust(30, "-"))
                for child in tree_node.children:
                    cmd_title = f"{colorama.Fore.GREEN}{child.name}{colorama.Style.RESET_ALL}"
                    help_line = (f" - {cmd_title}".ljust(37)) + f"[ {child.helptext} ]"
                    print(help_line)
                continue

            unit: chameleon_cli_unit.BaseCLIUnit = tree_node.cls()
            unit.device_com = self.device_com
            args_parse_result = unit.args_parser()

            if args_parse_result is not None:
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

                # start process cmd
                unit.on_exec(args_parse_result)
            except (chameleon_utils.UnexpectedResponseError, chameleon_utils.ArgsParserError) as e:
                print(f"{colorama.Fore.RED}{str(e)}{colorama.Style.RESET_ALL}")
            except Exception:
                print(f"CLI exception: {colorama.Fore.RED}{traceback.format_exc()}{colorama.Style.RESET_ALL}")


if __name__ == '__main__':
    colorama.init(autoreset=True)
    ChameleonCLI().startCLI()
