#!/usr/bin/env python3
"""
Jablotron modulator/decoder round-trip validator.

Pure-Python reimplementation of firmware/application/src/rfid/nfctag/lf/
protocols/jablotron.c (modulator) and utils/diphase.c (decoder).

For each test ID, this script:
  1. Builds the 64-bit raw frame (preamble + data + checksum).
  2. Generates the PWM entries our firmware modulator would emit.
  3. Expands the PWM entries to a per-tick LF_MOD level stream.
  4. Extracts rising-edge intervals (what the reader's demodulator sees).
  5. Feeds the intervals through the diphase state machine.
  6. Confirms the decoded data matches the original ID.

Tests both single-frame and double-frame modulator output.  The
double-frame variant is what the firmware uses; the single-frame
variant is kept as a regression guard for the frame-boundary-gap bug:
single-frame encoding produces an invalid interval at the PWM loop
boundary whenever the data's zero-count is odd.

Usage:  python3 test_jablotron_modulator.py
"""

# -------- constants (match jablotron.c) --------
JABLOTRON_RAW_SIZE = 64
JABLOTRON_DATA_SIZE = 5
COUNTER_TOP = 31          # 32 ticks per entry at NRF_PWM_CLK_125kHz = 32 carrier cycles
CC_HIGH = 32              # counter_top + 1: pin held HIGH (no compare match)
CC_LOW = 0                # compare = 0: pin held LOW

# decoder interval classification thresholds (jablotron.c)
READ_TIME1_BASE = 0x40    # 64   (1T)
READ_TIME2_BASE = 0x60    # 96   (1.5T)
READ_TIME3_BASE = 0x80    # 128  (2T)
JITTER = 0x10             # ±16


# -------- modulator side --------

def jablotron_raw_data(uid: bytes) -> int:
    """Build 64-bit raw frame: 16-bit preamble + 40-bit data + 8-bit checksum."""
    assert len(uid) == JABLOTRON_DATA_SIZE
    raw = 0xFFFF << 48
    chksum = 0
    for i, b in enumerate(uid):
        raw |= b << (40 - 8 * i)
        chksum = (chksum + b) & 0xFF
    raw |= (chksum ^ 0x3A) & 0xFF
    return raw


def jablotron_modulator(uid: bytes, double_frame: bool = True) -> list:
    """
    Reproduce jablotron_modulator(): return list of (cc, counter_top) PWM entries.

    If double_frame=True (firmware default): encode the frame twice with
    level persisting across passes.  Output: 256 entries, always loops
    cleanly regardless of the data's zero-count parity.

    If double_frame=False: single encoding, 128 entries.  Only loops cleanly
    when the frame's zero-count is even; retained here as a regression guard.
    """
    raw = jablotron_raw_data(uid)
    entries = []
    level = False   # persists across both passes when double_frame=True

    passes = 2 if double_frame else 1
    for _ in range(passes):
        for i in range(JABLOTRON_RAW_SIZE):
            bit = (raw >> (JABLOTRON_RAW_SIZE - 1 - i)) & 1
            level = not level                # boundary flip
            entries.append((CC_HIGH if level else CC_LOW, COUNTER_TOP))
            if not bit:
                level = not level            # mid-bit flip
            entries.append((CC_HIGH if level else CC_LOW, COUNTER_TOP))
    return entries


def expand_entries_to_signal(entries: list) -> list:
    """
    Convert PWM entries to a per-tick level stream (True=HIGH, False=LOW).
    Each entry contributes (counter_top + 1) ticks.

    For non-inverted polarity:
      cc == 0:              constant LOW  (compare fires immediately at counter=0)
      cc > counter_top:     constant HIGH (compare never fires)
      otherwise:            HIGH for cc ticks, then LOW for the rest
    """
    stream = []
    for cc, ctop in entries:
        ticks = ctop + 1
        if cc == 0:
            stream.extend([False] * ticks)
        elif cc > ctop:
            stream.extend([True] * ticks)
        else:
            stream.extend([True] * cc)
            stream.extend([False] * (ticks - cc))
    return stream


def extract_rising_edges(signal: list) -> list:
    """Return tick indices of rising (LOW->HIGH) edges."""
    return [i for i in range(1, len(signal)) if signal[i] and not signal[i - 1]]


def edges_to_intervals(edges: list) -> list:
    """Consecutive edge-to-edge intervals, in ticks = carrier cycles."""
    return [edges[i + 1] - edges[i] for i in range(len(edges) - 1)]


# -------- decoder side (matches diphase.c + jablotron.c) --------

def jablotron_period(interval: int) -> int:
    """Classify interval into 0=1T / 1=1.5T / 2=2T / 3=invalid."""
    if READ_TIME1_BASE - JITTER <= interval <= READ_TIME1_BASE + JITTER:
        return 0
    if READ_TIME2_BASE - JITTER <= interval <= READ_TIME2_BASE + JITTER:
        return 1
    if READ_TIME3_BASE - JITTER <= interval <= READ_TIME3_BASE + JITTER:
        return 2
    return 3


class DiphaseDecoder:
    """Python port of diphase_feed() state machine."""

    def __init__(self):
        self.boundary = True

    def feed(self, interval: int):
        """Return decoded bits list for one interval, or None on invalid (resets state)."""
        t = jablotron_period(interval)
        if t == 3:
            self.boundary = True
            return None
        if self.boundary:
            if t == 0:
                return [0]
            if t == 1:
                self.boundary = False
                return [1, 0]
            return [1, 1]  # t == 2
        else:
            if t == 0:
                return [0]
            if t == 1:
                self.boundary = True
                return [1]
            self.boundary = True  # 2T at mid-bit: reset
            return None


def jablotron_decode(intervals: list) -> tuple:
    """
    Feed intervals through the diphase decoder, accumulate bits, validate
    preamble+checksum.  Returns (ok, data_bytes).
    Mirrors jablotron_decode_feed() in jablotron.c.
    """
    dec = DiphaseDecoder()
    raw = 0
    raw_length = 0
    mask64 = (1 << 64) - 1

    for interval in intervals:
        bits = dec.feed(interval)
        if bits is None:
            raw = 0
            raw_length = 0
            continue
        for b in bits:
            raw = ((raw << 1) | (b & 1)) & mask64
            raw_length += 1
            if raw_length < JABLOTRON_RAW_SIZE:
                continue
            if ((raw >> 48) & 0xFFFF) != 0xFFFF:
                continue
            if (raw >> 47) & 1:
                continue
            chksum = 0
            for i in range(JABLOTRON_DATA_SIZE):
                chksum = (chksum + ((raw >> (40 - 8 * i)) & 0xFF)) & 0xFF
            chksum ^= 0x3A
            if chksum != (raw & 0xFF):
                continue
            data = bytes(((raw >> (40 - 8 * i)) & 0xFF) for i in range(JABLOTRON_DATA_SIZE))
            return (True, data)

    return (False, b"")


# -------- round-trip check --------

def roundtrip(uid: bytes, double_frame: bool, repeats: int = 3):
    """
    Encode uid into PWM, play back `repeats` times (simulating the firmware
    looping the PWM buffer), extract rising-edge intervals, decode.
    Returns (decoded_ok, invalid_interval_count).
    """
    entries = jablotron_modulator(uid, double_frame=double_frame) * repeats
    signal = expand_entries_to_signal(entries)
    intervals = edges_to_intervals(extract_rising_edges(signal))
    invalid = sum(1 for x in intervals if jablotron_period(x) == 3)
    ok, data = jablotron_decode(intervals)
    return (ok and data == uid, invalid)


# valid Jablotron IDs have bit 47 (MSB of data[0]) = 0, i.e., data[0] < 0x80.
# IDs chosen to cover both odd and even zero-count cases (printed below).
TEST_IDS = [
    "0000000001",   # minimal
    "01B6690000",   # firmware default
    "00DEADBEEF",   # arbitrary
    "7FFFFFFFFF",   # max valid data[0]
    "0012345678",   # odd zero_count — regression case for the frame-boundary bug
    "4242424242",
    "1234567890",
    "000103070F",   # synthetic odd zero_count, bit-pattern varies across bytes
]


def main():
    print(f"{'ID':<12} {'zero_count':<12} {'single-frame':<20} {'double-frame':<20}")
    print("-" * 72)

    single_pass = double_pass = 0
    expected_single_fails = 0  # odd zero-count IDs expected to fail single-frame

    for hex_id in TEST_IDS:
        uid = bytes.fromhex(hex_id)
        frame = jablotron_raw_data(uid)
        zero_count = 64 - bin(frame).count('1')
        parity = "odd" if zero_count & 1 else "even"
        if parity == "odd":
            expected_single_fails += 1

        ok_single, inv_single = roundtrip(uid, double_frame=False)
        ok_double, inv_double = roundtrip(uid, double_frame=True)
        single_pass += int(ok_single)
        double_pass += int(ok_double)

        s = f"{'PASS' if ok_single else 'FAIL'} ({inv_single} invalid)"
        d = f"{'PASS' if ok_double else 'FAIL'} ({inv_double} invalid)"
        print(f"{hex_id:<12} {zero_count:>2} ({parity:<4})   {s:<20} {d:<20}")

    print("-" * 72)
    expected_single_passes = len(TEST_IDS) - expected_single_fails
    print(f"single-frame: {single_pass}/{len(TEST_IDS)} "
          f"(expected {expected_single_passes} — fails when zero_count is odd)")
    print(f"double-frame: {double_pass}/{len(TEST_IDS)} (expected {len(TEST_IDS)})")

    # regression assertions
    assert double_pass == len(TEST_IDS), \
        f"double-frame encoding must decode all valid IDs (got {double_pass}/{len(TEST_IDS)})"
    assert single_pass == expected_single_passes, \
        f"single-frame encoding should still fail on odd-zero-count IDs " \
        f"(got {single_pass} passes, expected {expected_single_passes})"
    print("\nAll assertions passed.")


if __name__ == "__main__":
    main()
