"""Conservative host decoding for ordinary T5577 Manchester/RF32/RF64 reads.

The firmware starts its ADC capture at the end of the addressed READ command.
A configuration capture calibrates a *sample* anchor and signal polarity; later
blocks reuse that anchor. Looking for an arbitrary repeated 32-bit word would
silently accept any cyclic rotation when the sequence terminator is disabled.

The acquisition window below is a deliberately restricted timing hypothesis,
not an RF acceptance claim. It covers the nominal 64-carrier-clock downlink
timeout and limited acquisition/settling variation. Real T5577 captures must
validate it before this profile is described as hardware-tested.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from dataclasses import dataclass

CAPTURE_HEADER_SIZE = 8
MAX_CAPTURE_SAMPLES = (4096 - CAPTURE_HEADER_SIZE) * 2
BITRATE = 64
SUPPORTED_BITRATES = (32, 64)
WORD_BITS = 32
REQUIRED_REPETITIONS = 3
RESPONSE_START_WINDOW = (48, 160)
ANCHOR_JITTER_SAMPLES = 4
MIN_SIGNAL_SPAN = 16


class T55xxReadError(ValueError):
    """The capture does not establish a unique supported block value."""


class T55xxUnsupportedError(T55xxReadError):
    """A plausible configuration requests a profile this reader cannot decode."""


@dataclass(frozen=True)
class T55xxConfig:
    word: int
    start_sample: int
    inverted: bool
    modulation: str = "manchester"
    bitrate: int = BITRATE

    @property
    def max_block(self) -> int:
        return (self.word >> 5) & 7

    @property
    def sample_phase(self) -> int:
        return self.start_sample % self.bitrate


def unpack_capture(payload: bytes) -> tuple[int, ...]:
    """Validate and expand the firmware's signed-range, high-nibble-first U4."""
    if len(payload) < CAPTURE_HEADER_SIZE:
        raise T55xxReadError("T55xx capture header is truncated")
    version, bits, count, low, high = struct.unpack("!BBHhh", payload[:8])
    if version != 1 or bits != 4:
        raise T55xxReadError("unsupported T55xx capture format")
    if not 1 <= count <= MAX_CAPTURE_SAMPLES:
        raise T55xxReadError(
            "T55xx capture sample count is outside the supported range"
        )
    if high < low:
        raise T55xxReadError("T55xx capture range is inverted")
    if len(payload) != CAPTURE_HEADER_SIZE + (count + 1) // 2:
        raise T55xxReadError("T55xx capture size does not match its sample count")
    if count & 1 and payload[-1] & 0x0F:
        raise T55xxReadError("T55xx capture has nonzero padding")

    span = high - low
    samples: list[int] = []
    for index in range(count):
        byte = payload[CAPTURE_HEADER_SIZE + index // 2]
        nibble = byte >> 4 if index % 2 == 0 else byte & 0x0F
        samples.append(low + (nibble * span + 7) // 15)
    return tuple(samples)


def _validate_profile(word: int) -> int:
    if not 0 <= word <= 0xFFFFFFFF:
        raise T55xxReadError("T55xx configuration must be a 32-bit unsigned word")
    if word in (0, 0xFFFFFFFF):
        raise T55xxReadError("a constant word is not a supported T55xx configuration")
    if (word >> 28) not in (0, 6, 9) or word & 0x0FE00000:
        raise T55xxReadError(
            "word does not match the supported T5577 configuration layout"
        )
    if word & 0x00020000:
        raise T55xxUnsupportedError(
            "extended-mode T55xx configurations are unsupported"
        )
    rate_code = (word >> 18) & 7
    if rate_code not in (2, 5):
        raise T55xxUnsupportedError(
            "only RF/32 and RF/64 T55xx configurations are supported"
        )
    if (word >> 12) & 0x1F != 8:
        raise T55xxUnsupportedError(
            "only ASK Manchester T55xx configurations are supported"
        )
    if word & 0x00000C00:
        raise T55xxUnsupportedError("nonzero PSK carrier fields are unsupported")
    if word & 0x00000002:
        # Inverse-data is an extended-mode option. Analog receiver polarity is
        # calibrated from the waveform separately and remains supported.
        raise T55xxUnsupportedError(
            "inverse-data bit is reserved in the basic T55xx profile"
        )
    for mask, name in (
        (0x00000200, "answer-on-request"),
        (0x00000100, "one-time-programming"),
        (0x00000010, "password"),
        (0x00000008, "sequence terminator"),
        (0x00000004, "fast write"),
        (0x00000001, "power-on delay"),
    ):
        if word & mask:
            raise T55xxUnsupportedError(f"T55xx {name} mode is unsupported")
    return 32 if rate_code == 2 else 64


def config_from_word(word: int, *, start_sample: int, inverted: bool) -> T55xxConfig:
    """Validate the narrow profile and attach a capture-derived timing anchor."""
    bitrate = _validate_profile(word)
    if not isinstance(start_sample, int) or start_sample < 0:
        raise T55xxReadError("T55xx sample anchor must be a nonnegative integer")
    return T55xxConfig(word, start_sample, bool(inverted), bitrate=bitrate)


@dataclass(frozen=True)
class _Signal:
    prefix: tuple[float, ...]
    center: float
    span: float
    count: int

    def mean(self, start: int, end: int) -> float:
        return (self.prefix[end] - self.prefix[start]) / (end - start)


def _signal(samples: Sequence[int]) -> _Signal:
    if len(samples) > MAX_CAPTURE_SAMPLES:
        raise T55xxReadError("T55xx capture exceeds the sample limit")
    if len(samples) < REQUIRED_REPETITIONS * WORD_BITS * min(SUPPORTED_BITRATES):
        raise T55xxReadError("T55xx capture is too short for three complete words")
    if any(
        not isinstance(value, int) or not -32768 <= value <= 32767 for value in samples
    ):
        raise T55xxReadError("T55xx samples must be signed 16-bit integers")

    # Manchester spends half of every bit in each state. Interior percentiles
    # reject isolated startup spikes without normalizing a flat trace into data.
    ordered = sorted(samples)
    low = ordered[len(ordered) * 15 // 100]
    high = ordered[len(ordered) * 85 // 100]
    span = high - low
    if span < MIN_SIGNAL_SPAN:
        raise T55xxReadError("T55xx capture has no usable modulation amplitude")
    prefix = [0.0]
    for value in samples:
        prefix.append(prefix[-1] + value)
    return _Signal(tuple(prefix), (high + low) / 2, span, len(samples))


def _read_repeated_word(
    signal: _Signal, start: int, inverted: bool, bitrate: int
) -> int | None:
    end = start + REQUIRED_REPETITIONS * WORD_BITS * bitrate
    if start < 0 or end > signal.count:
        return None
    words: list[int] = []
    for repeat in range(REQUIRED_REPETITIONS):
        word = 0
        for bit in range(WORD_BITS):
            offset = start + (repeat * WORD_BITS + bit) * bitrate
            # Read the interior half of each half-cell. Transition ringing and
            # small edge jitter must not move the recovered bit boundary.
            guard = bitrate // 8
            half = bitrate // 2
            first = signal.mean(offset + guard, offset + half - guard) - signal.center
            second = (
                signal.mean(offset + half + guard, offset + bitrate - guard)
                - signal.center
            )
            if (first >= 0) == (second >= 0):
                return None
            if abs(first - second) < signal.span * 0.60:
                return None
            value = int(first < second) ^ int(inverted)
            word = (word << 1) | value
        words.append(word)
    if any(word != words[0] for word in words[1:]):
        return None
    return words[0]


def detect_config(
    samples: Sequence[int],
    *,
    start_window: tuple[int, int] = RESPONSE_START_WINDOW,
) -> T55xxConfig:
    """Find one supported configuration in a bounded first-response window.

    Equivalent nearby sample phases are collapsed, but different words or
    polarities are an error. There is no best-confidence or arbitrary bit
    rotation fallback. A window spanning a full 32-bit period is disallowed.
    """
    lower, upper = start_window
    if not 0 <= lower <= upper or upper - lower >= WORD_BITS * min(SUPPORTED_BITRATES):
        raise T55xxReadError("invalid T55xx first-response acquisition window")
    signal = _signal(samples)
    candidates: dict[tuple[int, bool, int], list[int]] = {}
    unsupported: set[str] = set()
    for bitrate in SUPPORTED_BITRATES:
        for start in range(lower, upper + 1):
            for inverted in (False, True):
                word = _read_repeated_word(signal, start, inverted, bitrate)
                if word is None:
                    continue
                try:
                    configured_bitrate = _validate_profile(word)
                except T55xxUnsupportedError as error:
                    unsupported.add(str(error))
                    continue
                except T55xxReadError:
                    continue
                if configured_bitrate != bitrate:
                    continue
                candidates.setdefault((word, inverted, bitrate), []).append(start)

    if not candidates:
        if unsupported:
            raise T55xxUnsupportedError("; ".join(sorted(unsupported)))
        raise T55xxReadError("no supported configuration at the first-response anchor")
    if len(candidates) != 1:
        raise T55xxReadError("T55xx configuration or polarity is ambiguous")
    (word, inverted, bitrate), starts = next(iter(candidates.items()))
    if starts[-1] - starts[0] >= bitrate // 2:
        raise T55xxReadError("T55xx configuration timing is ambiguous")

    # Use a central valid phase, not the first sample that barely passes a
    # threshold. The anchor is local to this command-synchronous acquisition.
    return config_from_word(
        word, start_sample=starts[len(starts) // 2], inverted=inverted
    )


def decode_block(samples: Sequence[int], config: T55xxConfig) -> int:
    """Decode with a configuration's sample anchor; never search word rotations.

    An absolute timing shift of one whole bit cannot be detected from arbitrary
    block data alone. The caller must use command-synchronous captures, compare
    independent reads, and recheck block 0 around a dump. RF validation must
    establish that this acquisition anchor stays stable across block addresses.
    """
    bitrate = _validate_profile(config.word)
    if config.modulation != "manchester" or config.bitrate != bitrate:
        raise T55xxUnsupportedError("T55xx calibration uses an unsupported profile")
    if not isinstance(config.start_sample, int) or config.start_sample < 0:
        raise T55xxReadError("invalid T55xx calibration anchor")
    signal = _signal(samples)
    words = {
        word
        for start in range(
            max(0, config.start_sample - ANCHOR_JITTER_SAMPLES),
            config.start_sample + ANCHOR_JITTER_SAMPLES + 1,
        )
        if (word := _read_repeated_word(signal, start, config.inverted, bitrate))
        is not None
    }
    if not words:
        raise T55xxReadError("block does not repeat at the calibrated T55xx anchor")
    if len(words) != 1:
        raise T55xxReadError("block data is ambiguous at the calibrated T55xx anchor")
    return words.pop()
