#!/usr/bin/env python3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from chameleon_cli_unit import (  # noqa: E402
    indala_check_bits_match,
    indala_decode_raw,
    indala_encode_raw,
    indala_format_output,
    indala_has_26bit_preamble,
)

# Made-up test card: FC 123, card 1337, as written by indala_encode_raw()
FRAME = bytes.fromhex("a00000008f49d2a1")
# Same frame with the checksum bits (62, 63) flipped from 01 to 10
FRAME_CHECKSUM_FLIPPED = bytes.fromhex("a00000008f49d2a2")


def test_encode_round_trip():
    assert indala_encode_raw(123, 1337) == FRAME
    assert indala_decode_raw(FRAME) == (123, 1337)


def test_encoder_frame_prints_fc_card_and_check_bits():
    assert indala_has_26bit_preamble(FRAME)
    assert indala_check_bits_match(FRAME)
    assert indala_format_output(FRAME).splitlines() == [
        "Indala (len 64)  Raw: A00000008F49D2A1",
        "   Fmt 26  FC: 123  Card: 1337  Parity: 01  Checksum: 01",
    ]


def test_flipped_checksum_still_prints_fc_card():
    assert indala_has_26bit_preamble(FRAME_CHECKSUM_FLIPPED)
    assert not indala_check_bits_match(FRAME_CHECKSUM_FLIPPED)
    lines = indala_format_output(FRAME_CHECKSUM_FLIPPED).splitlines()
    assert lines[1] == "   Fmt 26  FC: 123  Card: 1337  Parity: 01  Checksum: 10"
    assert "differ" in lines[2]


def test_missing_preamble_prints_raw_only():
    frame = bytes([FRAME[0] ^ 0x80]) + FRAME[1:]
    assert not indala_has_26bit_preamble(frame)
    assert indala_format_output(frame) == "Indala (len 64)  Raw: 200000008F49D2A1"
