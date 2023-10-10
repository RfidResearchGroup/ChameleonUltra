import argparse
import colorama
from functools import wraps
from typing import Union
from prompt_toolkit.completion import Completer, NestedCompleter, WordCompleter
from prompt_toolkit.completion.base import Completion
from prompt_toolkit.document import Document

import chameleon_status

# Colorama shorthands
CR = colorama.Fore.RED
CG = colorama.Fore.GREEN
CB = colorama.Fore.BLUE
CC = colorama.Fore.CYAN
CY = colorama.Fore.YELLOW
CM = colorama.Fore.MAGENTA
C0 = colorama.Style.RESET_ALL


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

    def __init__(self, **args):
        super().__init__(*args)
        self.add_help = False
        self.description = "Please enter correct parameters"

    def exit(self, status: int = ..., message: str or None = ...):
        if message:
            raise ParserExitIntercept(message)

    def error(self, message: str):
        args = {'prog': self.prog, 'message': message}
        raise ArgsParserError('%(prog)s: error: %(message)s\n' % args)

    def print_help(self):
        """
        Colorize argparse help
        """
        print("-" * 80)
        print(f"{CR}{self.prog}{C0}\n")
        lines = self.format_help().splitlines()
        usage = lines[:lines.index('')]
        assert usage[0].startswith('usage:')
        usage[0] = usage[0].replace('usage:', f'{CG}usage:{C0}\n ')
        usage[0] = usage[0].replace(self.prog, f'{CR}{self.prog}{C0}')
        usage = [usage[0]] + [x[4:] for x in usage[1:]] + ['']
        lines = lines[lines.index('')+1:]
        desc = lines[:lines.index('')]
        print(f'{CC}'+'\n'.join(desc)+f'{C0}\n')
        print('\n'.join(usage))
        lines = lines[lines.index('')+1:]
        if '' in lines:
            options = lines[:lines.index('')]
            lines = lines[lines.index('')+1:]
        else:
            options = lines
            lines = []
        if len(options) > 0 and options[0].strip() == 'positional arguments:':
            positional_args = options
            positional_args[0] = positional_args[0].replace('positional arguments:', f'{CG}positional arguments:{C0}')
            if len(positional_args) > 1:
                positional_args.append('')
            print('\n'.join(positional_args))
            if '' in lines:
                options = lines[:lines.index('')]
                lines = lines[lines.index('')+1:]
            else:
                options = lines
                lines = []
        if len(options) > 0:
            assert options[0].strip() == 'options:'
            options[0] = options[0].replace('options:', f'{CG}options:{C0}')
            if len(options) > 1:
                options.append('')
            print('\n'.join(options))
        if len(lines) > 0:
            lines[0] = f'{CG}{lines[0]}{C0}'
            print('\n'.join(lines))


def expect_response(accepted_responses: Union[int, list[int]]):
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
                if ret.status in chameleon_status.Device and ret.status in chameleon_status.message:
                    raise UnexpectedResponseError(
                        chameleon_status.message[ret.status])
                else:
                    raise UnexpectedResponseError(
                        f"Unexpected response and unknown status {ret.status}")

            return ret.data

        return error_throwing_func

    return decorator


class CLITree:
    """
    Class holding a

    :param name: Name of the command (e.g. "set")
    :param help_text: Hint displayed for the command
    :param fullname: Full name of the command that includes previous commands (e.g. "hw mode set")
    :param cls: A BaseCLIUnit instance handling the command
    """

    def __init__(self, name=None, help_text=None, fullname=None, children=None, cls=None, root=False) -> None:
        self.name: str = name
        self.help_text: str = help_text
        self.fullname: str = fullname if fullname else name
        self.children: list[CLITree] = children if children else list()
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
            fullname=f'{self.fullname} {name}' if not self.root else f'{name}',
            help_text=help_text)
        self.children.append(child)
        return child

    def command(self, name):
        """
        Create a child command

        :param name: Name of the command
        """
        def decorator(cls):
            self.children.append(CLITree(
                name=name,
                fullname=f'{self.fullname} {name}' if not self.root else f'{name}',
                cls=cls))
            return cls
        return decorator


class CustomNestedCompleter(NestedCompleter):
    """
    Copy of the NestedCompleter class that accepts a CLITree object and
    supports meta_dict for descriptions
    """

    def __init__(
        self, options, ignore_case: bool = True, meta_dict: dict = {}
    ) -> None:
        self.options = options
        self.ignore_case = ignore_case
        self.meta_dict = meta_dict

    def __repr__(self) -> str:
        return f"CustomNestedCompleter({self.options!r}, ignore_case={self.ignore_case!r})"

    @classmethod
    def from_clitree(cls, node):
        options = {}
        meta_dict = {}

        for child_node in node.children:
            if child_node.cls:
                # CLITree is a standalone command with arguments
                options[child_node.name] = ArgparseCompleter(
                    child_node.cls().args_parser())
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
                remaining_text = text[len(first_term):].lstrip()
                move_cursor = len(text) - len(remaining_text) + stripped_len

                new_document = Document(
                    remaining_text,
                    cursor_position=document.cursor_position - move_cursor,
                )

                yield from completer.get_completions(new_document, complete_event)

        # No space in the input: behave exactly like `WordCompleter`.
        else:
            completer = WordCompleter(
                list(self.options.keys()), ignore_case=self.ignore_case, meta_dict=self.meta_dict
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
            return tokens and tokens[0].startswith('-')

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
                                parsed, unparsed)

                    else:
                        # Show all possible values
                        for choice in action.choices:
                            suggestions[str(choice)] = None

                    break
                else:
                    # No choices, process further arguments
                    if check_arg(unparsed):
                        parsed, unparsed, suggestions = self.check_tokens(
                            parsed, unparsed)
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
        word_before_cursor = document.text_before_cursor.split(' ')[-1]

        _, _, suggestions = self.check_tokens(list(), text.split())

        for key, suggestion in suggestions.items():
            yield Completion(key, -len(word_before_cursor), display=key, display_meta=suggestion)
