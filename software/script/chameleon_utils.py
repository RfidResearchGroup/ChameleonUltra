from functools import wraps
from typing import Union
from prompt_toolkit.completion import Completer, NestedCompleter, WordCompleter
from prompt_toolkit.document import Document

import chameleon_status


class ArgsParserError(Exception):
    pass


class ParserExitIntercept(Exception):
    pass


class UnexpectedResponseError(Exception):
    """
    Unexpected response exception
    """

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
                    raise UnexpectedResponseError(chameleon_status.message[ret.status])
                else:
                    raise UnexpectedResponseError(f"Unexpected response and unknown status {ret.status}")

            return ret

        return error_throwing_func

    return decorator


class CLITree:
    """
    Class holding a

    :param name: Name of the command (e.g. "set")
    :param helptext: Hint displayed for the command
    :param fullname: Full name of the command that includes previous commands (e.g. "hw mode set")
    :param cls: A BaseCLIUnit instance handling the command
    """

    def __init__(self, name=None, helptext=None, fullname=None, children=None, cls=None) -> None:
        self.name: str = name
        self.helptext: str = helptext
        self.fullname: str = fullname if fullname else name
        self.children: list[CLITree] = children if children else list()
        self.cls = cls

    def subgroup(self, name, helptext=None):
        """
        Create a child command group

        :param name: Name of the command group
        :param helptext: Hint displayed for the group
        """
        child = CLITree(
            name=name, fullname=f'{self.fullname} {name}', helptext=helptext)
        self.children.append(child)
        return child

    def command(self, name, helptext=None):
        """
        Create a child command

        :param name: Name of the command
        :param helptext: Hint displayed for the command
        """
        def decorator(cls):
            self.children.append(
                CLITree(name=name, fullname=f'{self.fullname} {name}', helptext=helptext, cls=cls))
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
    def from_nested_dict(cls, data):
        options = {}
        meta_dict = {}
        for key, value in data.items():
            if isinstance(value, Completer):
                options[key] = value
            elif isinstance(value, dict):
                options[key] = cls.from_nested_dict(value)
            elif isinstance(value, set):
                options[key] = cls.from_nested_dict({item: None for item in value})
            elif isinstance(value, CLITree):
                options[key] = cls.from_clitree(value)
                meta_dict[key] = value.helptext
            else:
                assert value is None
                options[key] = None

        return cls(options, meta_dict=meta_dict)
    
    @classmethod
    def from_clitree(cls, node):
        options = {}
        meta_dict = {}

        for child_node in node.children:
            options[child_node.name] = cls.from_clitree(child_node)
            meta_dict[child_node.name] = child_node.helptext

        return cls(options, meta_dict=meta_dict)

    def get_completions(self, document, complete_event):
        # Split document.
        text = document.text_before_cursor.lstrip()
        stripped_len = len(document.text_before_cursor) - len(text)

        # If there is a space, check for the first term, and use a
        # subcompleter.
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
                list(self.options.keys()), ignore_case=self.ignore_case, meta_dict=self.meta_dict
            )
            yield from completer.get_completions(document, complete_event)

