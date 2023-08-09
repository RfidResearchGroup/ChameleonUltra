from functools import wraps
from typing import Union

import chameleon_status


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
