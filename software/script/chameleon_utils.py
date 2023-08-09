from functools import wraps
from typing import Union

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
