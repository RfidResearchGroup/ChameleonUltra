from functools import wraps
from typing import Union

import chameleon_status


class NegativeResponseError(Exception):
    """
        Not positive response
    """

def expect_response(accepted_responses):
    if isinstance(accepted_responses, int):
        accepted_responses = [accepted_responses]
    
    def decorator(func):
        @wraps(func)
        def error_throwing_func(*args, **kwargs):
            ret = func(*args, **kwargs)

            if ret.status not in accepted_responses:
                if ret.status in chameleon_status.Device and ret.status in chameleon_status.message:
                    raise NegativeResponseError(chameleon_status.message[ret.status])
                else:
                    raise NegativeResponseError(f"Not positive response and unknown status {ret.status}")

            return ret

        return error_throwing_func

    return decorator
