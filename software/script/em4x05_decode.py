"""Strict EM4205/4305 ASK response decoder (manufacturer DS v6, tables 8/11).

Only Manchester and bi-phase at RF/8,16,32,40,64 are claimed here.
Programming commands and guessed-password paths are deliberately absent.
"""

from __future__ import annotations

import struct
from typing import Optional, Sequence

RATES = (8, 16, 32, 40, 64)
SAMPLES = 6144
SUCCESS = (0, 0, 0, 0, 1, 0, 1, 0)
FAILURE = (0, 0, 0, 0, 0, 0, 0, 1)


class EMReadError(ValueError):
    pass


def config_parameters(config: int) -> tuple[str, int]:
    if type(config) is not int or not 0 <= config <= 0xFFFFFFFF:
        raise EMReadError("invalid configuration word")
    rate = ((config & 0x3F) + 1) * 2
    encoding = (config >> 6) & 0x0F
    if rate not in RATES or encoding not in (1, 2):
        raise EMReadError(
            "unsupported configuration: requires Manchester/bi-phase RF/8..64"
        )
    return ("manchester" if encoding == 1 else "biphase"), rate


def unpack_capture(payload: bytes) -> tuple[int, ...]:
    if type(payload) is not bytes or len(payload) != 8 + SAMPLES // 2:
        raise EMReadError("invalid packed capture length")
    version, bits, count, low, high = struct.unpack("!BBHhh", payload[:8])
    if (version, bits, count) != (1, 4, SAMPLES) or high < low:
        raise EMReadError("unsupported capture header")
    span = high - low
    values: list[int] = []
    for byte in payload[8:]:
        values.extend(
            (low + ((byte >> 4) * span + 7) // 15, low + ((byte & 15) * span + 7) // 15)
        )
    return tuple(values)


def _payload_word(bits: Sequence[Optional[int]], start: int) -> Optional[int]:
    if start + 45 > len(bits) or bits[start + 44] != 0:
        return None
    word = 0
    columns = [0] * 8
    for row in range(4):
        parity = 0
        for col in range(8):
            bit = bits[start + row * 9 + col]
            if bit is None:
                return None
            parity ^= bit
            columns[col] ^= bit
            word |= bit << (row * 8 + col)
        if parity != bits[start + row * 9 + 8]:
            return None
    if any(columns[col] != bits[start + 36 + col] for col in range(8)):
        return None
    return word


def _symbols(
    samples: Sequence[int],
    prefix: Sequence[int],
    encoding: str,
    rate: int,
    phase: int,
    inverted: bool,
    center: float,
    minimum_spread: float,
) -> tuple[Optional[int], ...]:
    half = rate // 2
    bits: list[Optional[int]] = []
    previous: Optional[bool] = None
    for start in range(phase, len(samples) - rate + 1, rate):
        first = (prefix[start + half] - prefix[start]) / half
        second = (prefix[start + rate] - prefix[start + half]) / half
        strong = (
            abs(first - center) >= minimum_spread
            and abs(second - center) >= minimum_spread
        )
        a, b = first > center, second > center
        if encoding == "manchester":
            bits.append((int(a) ^ int(inverted)) if strong and a != b else None)
        else:
            # Bi-phase has a boundary transition; zero has a mid-bit transition.
            bits.append(
                int(a == b) if strong and (previous is None or a != previous) else None
            )
        previous = b if strong else None
    return tuple(bits)


def decode_capture(
    samples: Sequence[int], *, config: Optional[int] = None, expect_config: bool = False
) -> dict[str, object]:
    if len(samples) != SAMPLES or max(samples) - min(samples) < 8:
        raise EMReadError("capture has no usable modulation")
    modes = (
        [config_parameters(config)]
        if config is not None
        else [
            (encoding, rate) for encoding in ("manchester", "biphase") for rate in RATES
        ]
    )
    prefix = [0]
    for sample in samples:
        prefix.append(prefix[-1] + sample)
    found: dict[int, dict[str, object]] = {}
    saw_error = False
    center = (min(samples) + max(samples)) / 2
    minimum_spread = (max(samples) - min(samples)) * 0.15
    for encoding, rate in modes:
        for inverted in (False, True) if encoding == "manchester" else (False,):
            for phase in range(rate):
                bits = _symbols(
                    samples,
                    prefix,
                    encoding,
                    rate,
                    phase,
                    inverted,
                    center,
                    minimum_spread,
                )
                for offset in range(max(0, len(bits) - 7)):
                    preamble = bits[offset : offset + 8]
                    if preamble == FAILURE:
                        saw_error = True
                    if preamble != SUCCESS:
                        continue
                    word = _payload_word(bits, offset + 8)
                    if word is None:
                        continue
                    if expect_config:
                        try:
                            declared = config_parameters(word)
                        except EMReadError:
                            continue
                        if declared != (encoding, rate):
                            continue
                    found[word] = {
                        "word": word,
                        "encoding": encoding,
                        "rate": rate,
                        "sample_phase": phase,
                        "inverted": inverted,
                    }
    if len(found) > 1:
        raise EMReadError("ambiguous response: more than one parity-valid word")
    if found:
        return next(iter(found.values()))
    if saw_error:
        raise EMReadError("tag returned an error preamble or no valid word")
    raise EMReadError("no supported, parity-valid EM response")
