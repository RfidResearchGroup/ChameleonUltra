import argparse
import subprocess
import sys
import tempfile
import os.path
from pathlib import Path

import colorama
from functools import wraps

# once Python3.10 is mainstream, we can replace Union[str, None] by str | None
from typing import Union, Callable, Any
from prompt_toolkit.completion import Completer, NestedCompleter, WordCompleter
from prompt_toolkit.completion.base import Completion
from prompt_toolkit.document import Document

from chameleon.enum import Status

# Colorama shorthands
CR = colorama.Fore.RED
CG = colorama.Fore.GREEN
CB = colorama.Fore.BLUE
CC = colorama.Fore.CYAN
CY = colorama.Fore.YELLOW
CM = colorama.Fore.MAGENTA
C0 = colorama.Style.RESET_ALL

default_cwd = Path.cwd() / Path(__file__).with_name("bin")


class ArgsParserError(Exception):
    pass


class ParserExitIntercept(Exception):
    pass


class UnexpectedResponseError(Exception):
    """
    Unexpected response exception
    """


class ArgumentParserNoExit(argparse.ArgumentParser):
    """
    If arg ArgumentParser parse error, we can't exit process,
    we must raise exception to stop parse
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.add_help = False
        self.description = "Please enter correct parameters"
        self.help_requested = False

    def exit(self, status: int = 0, message: Union[str, None] = None):
        if message:
            raise ParserExitIntercept(message)

    def error(self, message: str):
        args = {"prog": self.prog, "message": message}
        raise ArgsParserError("%(prog)s: error: %(message)s\n" % args)

    def print_help(self):
        """
        Colorize argparse help
        """
        print("-" * 80)
        print(f"{CR}{self.prog}{C0}\n")
        lines = self.format_help().splitlines()
        usage = lines[: lines.index("")]
        assert usage[0].startswith("usage:")
        usage[0] = usage[0].replace("usage:", f"{CG}usage:{C0}\n ")
        usage[0] = usage[0].replace(self.prog, f"{CR}{self.prog}{C0}")
        usage = [usage[0]] + [x[4:] for x in usage[1:]] + [""]
        lines = lines[lines.index("") + 1 :]
        desc = lines[: lines.index("")]
        print(f"{CC}" + "\n".join(desc) + f"{C0}\n")
        print("\n".join(usage))
        lines = lines[lines.index("") + 1 :]
        if "" in lines:
            options = lines[: lines.index("")]
            lines = lines[lines.index("") + 1 :]
        else:
            options = lines
            lines = []
        if len(options) > 0 and options[0].strip() == "positional arguments:":
            positional_args = options
            positional_args[0] = positional_args[0].replace(
                "positional arguments:", f"{CG}positional arguments:{C0}"
            )
            if len(positional_args) > 1:
                positional_args.append("")
            print("\n".join(positional_args))
            if "" in lines:
                options = lines[: lines.index("")]
                lines = lines[lines.index("") + 1 :]
            else:
                options = lines
                lines = []
        if len(options) > 0:
            # 2 variants depending on Python version(?)
            assert options[0].strip() in ["options:", "optional arguments:"]
            options[0] = options[0].replace("options:", f"{CG}options:{C0}")
            options[0] = options[0].replace(
                "optional arguments:", f"{CG}optional arguments:{C0}"
            )
            if len(options) > 1:
                options.append("")
            print("\n".join(options))
        if len(lines) > 0:
            lines[0] = f"{CG}{lines[0]}{C0}"
            print("\n".join(lines))
        print("")
        self.help_requested = True


def print_mem_dump(bindata, blocksize):
    hexadecimal_len = blocksize * 3 + 1
    ascii_len = blocksize + 1
    print(f"[=] ----+{hexadecimal_len * '-'}+{ascii_len * '-'}")
    print(f"[=] blk | data{(hexadecimal_len - 5) * ' '}| ascii")
    print(f"[=] ----+{hexadecimal_len * '-'}+{ascii_len * '-'}")

    blocks = [bindata[i : i + blocksize] for i in range(0, len(bindata), blocksize)]
    blk_index = 1
    for b in blocks:
        hexstr = " ".join(b.hex()[i : i + 2] for i in range(0, len(b.hex()), 2))
        asciistr = "".join(
            [
                chr(b[i]) if (b[i] > 31 and b[i] < 127) else "."
                for i in range(0, len(b), 1)
            ]
        )
        print(f"[=] {blk_index:3} | {hexstr.upper()} | {asciistr} ")
        blk_index += 1


def print_key_table(key_map):
    key_width = max(
        max(len(k) for k in key_map["A"].values()),
        max(len(k) for k in key_map["B"].values()),
        len("key A"),
        len("key B"),
    )
    header_line = f"[=] {'-' * 5}+{'-' * (key_width + 2)}+{'-' * (key_width + 2)}"
    print(header_line)
    print(f"[=]  sec | key A{' ' * (key_width - 5)} | key B{' ' * (key_width - 5)}")
    print(header_line)
    for sec, (a, b) in enumerate(zip(key_map["A"].values(), key_map["B"].values())):
        print(f"[=]  {sec:02d}  | {a:{key_width}} | {b:{key_width}}")
    print(header_line)


def _swap_endian(x):
    x = ((x >> 8) & 0x00FF00FF) | ((x & 0x00FF00FF) << 8)
    x = (x >> 16) | (x << 16)
    return x & 0xFFFFFFFF


def prng_successor(x, n):
    x = _swap_endian(x)

    while n > 0:
        x = (x >> 1) | ((((x >> 16) ^ (x >> 18) ^ (x >> 19) ^ (x >> 21)) & 0x1) << 31)
        x = x & 0xFFFFFFFF
        n -= 1

    return _swap_endian(x)


def reconstruct_full_nt(response_data, offset):
    nt = int.from_bytes(response_data[offset : offset + 2], byteorder="big")

    return (nt << 16) | prng_successor(nt, 16)


def parity_to_str(nt_par_err):
    return "".join(
        [
            str((nt_par_err >> 3) & 1),
            str((nt_par_err >> 2) & 1),
            str((nt_par_err >> 1) & 1),
            str(nt_par_err & 1),
        ]
    )


def execute_tool(tool_name, args):
    if sys.platform == "win32":
        tool_executable = f"{tool_name}.exe"
    else:
        tool_executable = f"./{tool_name}"

    tool_path = os.path.join(default_cwd, tool_executable)
    cmd_recover_list = [tool_path]
    cmd_recover_list.extend(args)

    # print(f"Executing: {' '.join(cmd_recover_list)}")

    temp_output_file = tempfile.NamedTemporaryFile(
        suffix=".log",
        prefix="output_",
        delete=True,
        mode="w+",
        encoding="utf-8",
        errors="replace",
    )

    process = subprocess.Popen(
        cmd_recover_list,
        cwd=tempfile.gettempdir(),
        stdout=temp_output_file,
        stderr=subprocess.STDOUT,
    )

    ret_code = process.wait()
    temp_output_file.seek(0)

    if ret_code:
        raise Exception("Failed to execute tool: " + temp_output_file.read())

    return temp_output_file.read()


def tqdm_if_exists(iterator):
    try:
        import tqdm

        return tqdm.tqdm(iterator)
    except ImportError:
        return iterator


def expect_response(accepted_responses: Union[int, list[int]]) -> Callable[..., Any]:
    """
    Decorator for wrapping a Chameleon CMD function to check its response
    for expected return codes and throwing an exception otherwise
    """
    if isinstance(accepted_responses, int):
        accepted_responses = [accepted_responses]

    def decorator(func):
        @wraps(func)
        def error_throwing_func(*args, **kwargs):
            ret = func(*args, **kwargs)
            if ret.status not in accepted_responses:
                try:
                    status_string = str(Status(ret.status))
                except ValueError:
                    status_string = (
                        f"Unexpected response and unknown status {ret.status}"
                    )
                raise UnexpectedResponseError(status_string)

            return ret.parsed

        return error_throwing_func

    return decorator


class CLITree:
    """
    Class holding a

    :param name: Name of the command (e.g. "set")
    :param help_text: Hint displayed for the command
    :param fullname: Full name of the command that includes previous commands (e.g. "hw settings animation")
    :param cls: A BaseCLIUnit instance handling the command
    """

    def __init__(
        self,
        name: str = "",
        help_text: Union[str, None] = None,
        fullname: Union[str, None] = None,
        children: Union[list["CLITree"], None] = None,
        cls=None,
        root=False,
    ) -> None:
        self.name = name
        self.help_text = help_text
        self.fullname = fullname if fullname else name
        self.children = children if children else list()
        self.cls = cls
        self.root = root
        if self.help_text is None and not root:
            assert self.cls is not None
            parser = self.cls().args_parser()
            assert parser is not None
            self.help_text = parser.description

    def subgroup(self, name, help_text=None):
        """
        Create a child command group

        :param name: Name of the command group
        :param help_text: Hint displayed for the group
        """
        child = CLITree(
            name=name,
            fullname=f"{self.fullname} {name}" if not self.root else f"{name}",
            help_text=help_text,
        )
        self.children.append(child)
        return child

    def command(self, name):
        """
        Create a child command

        :param name: Name of the command
        """

        def decorator(cls):
            self.children.append(
                CLITree(
                    name=name,
                    fullname=f"{self.fullname} {name}" if not self.root else f"{name}",
                    cls=cls,
                )
            )
            return cls

        return decorator


class CustomNestedCompleter(NestedCompleter):
    """
    Copy of the NestedCompleter class that accepts a CLITree object and
    supports meta_dict for descriptions
    """

    def __init__(self, options, ignore_case: bool = True, meta_dict: dict = {}) -> None:
        self.options = options
        self.ignore_case = ignore_case
        self.meta_dict = meta_dict

    def __repr__(self) -> str:
        return (
            f"CustomNestedCompleter({self.options!r}, ignore_case={self.ignore_case!r})"
        )

    @classmethod
    def from_clitree(cls, node):
        options = {}
        meta_dict = {}

        for child_node in node.children:
            if child_node.cls:
                # CLITree is a standalone command with arguments
                options[child_node.name] = ArgparseCompleter(
                    child_node.cls().args_parser()
                )
            else:
                # CLITree is a command group
                options[child_node.name] = cls.from_clitree(child_node)
                meta_dict[child_node.name] = child_node.help_text

        return cls(options, meta_dict=meta_dict)

    def get_completions(self, document, complete_event):
        # Split document.
        text = document.text_before_cursor.lstrip()
        stripped_len = len(document.text_before_cursor) - len(text)

        # If there is a space, check for the first term, and use a sub_completer.
        if " " in text:
            first_term = text.split()[0]
            completer = self.options.get(first_term)

            # If we have a sub completer, use this for the completions.
            if completer is not None:
                remaining_text = text[len(first_term) :].lstrip()
                move_cursor = len(text) - len(remaining_text) + stripped_len

                new_document = Document(
                    remaining_text,
                    cursor_position=document.cursor_position - move_cursor,
                )

                yield from completer.get_completions(new_document, complete_event)

        # No space in the input: behave exactly like `WordCompleter`.
        else:
            completer = WordCompleter(
                list(self.options.keys()),
                ignore_case=self.ignore_case,
                meta_dict=self.meta_dict,
            )
            yield from completer.get_completions(document, complete_event)


class ArgparseCompleter(Completer):
    """
    Completer instance for autocompletion of ArgumentParser arguments

    :param parser: ArgumentParser instance
    """

    def __init__(self, parser) -> None:
        self.parser: ArgumentParserNoExit = parser

    def check_tokens(self, parsed, unparsed):
        suggestions = {}

        def check_arg(tokens):
            return tokens and tokens[0].startswith("-")

        if not parsed and not unparsed:
            # No tokens detected, just show all flags
            for action in self.parser._actions:
                for opt in action.option_strings:
                    suggestions[opt] = action.help
            return [], [], suggestions

        token = unparsed.pop(0)

        for action in self.parser._actions:
            if any(opt == token for opt in action.option_strings):
                # Argument fully matches the token
                parsed.append(token)

                if action.choices:
                    # Autocomplete with choices
                    if unparsed:
                        # Autocomplete values
                        value = unparsed.pop(0)
                        for choice in action.choices:
                            if str(choice).startswith(value):
                                suggestions[str(choice)] = None

                        parsed.append(value)

                        if check_arg(unparsed):
                            parsed, unparsed, suggestions = self.check_tokens(
                                parsed, unparsed
                            )

                    else:
                        # Show all possible values
                        for choice in action.choices:
                            suggestions[str(choice)] = None

                    break
                else:
                    # No choices, process further arguments
                    if check_arg(unparsed):
                        parsed, unparsed, suggestions = self.check_tokens(
                            parsed, unparsed
                        )
                    break
            elif any(opt.startswith(token) for opt in action.option_strings):
                for opt in action.option_strings:
                    if opt.startswith(token):
                        suggestions[opt] = action.help

        if suggestions:
            unparsed.insert(0, token)

        return parsed, unparsed, suggestions

    def get_completions(self, document, complete_event):
        text = document.text_before_cursor
        word_before_cursor = document.text_before_cursor.split(" ")[-1]

        _, _, suggestions = self.check_tokens(list(), text.split())

        for key, suggestion in suggestions.items():
            yield Completion(
                key, -len(word_before_cursor), display=key, display_meta=suggestion
            )
