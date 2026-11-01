"""Presco code parsing and framing; no device access."""

import re


def parse_hex_code(value: str) -> bytes:
    """Parse the complete 32-bit code, including its unknown middle byte."""
    if re.fullmatch(r"[0-9a-fA-F]{8}", value) is None:
        raise ValueError("Presco code must contain exactly 8 hexadecimal digits")
    return bytes.fromhex(value)


def parse_printed_code(value: str) -> bytes:
    """Printed Presco identifiers are base 12, with * = 10 and # = 11."""
    if re.fullmatch(r"[0-9*#]{1,9}", value) is None:
        raise ValueError("Printed Presco code must contain 1-9 digits, * or #")
    code = 0
    for digit in value:
        code = code * 12 + "0123456789*#".index(digit)
    if code > 0xFFFFFFFF:
        raise ValueError("Printed Presco code exceeds 32 bits")
    return code.to_bytes(4, "big")


def encode_frame(code: bytes) -> bytes:
    """Build the 128-bit frame; its on-air terminator is not a data bit."""
    if len(code) != 4:
        raise ValueError("Presco code must be exactly 4 bytes")
    return bytes.fromhex("10D000000000000000000000") + code
