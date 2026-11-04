import random
import struct
import unittest

from em4x05_decode import (
    EMReadError,
    FAILURE,
    RATES,
    SAMPLES,
    config_parameters,
    decode_capture,
    unpack_capture,
)
from tests.em4x05_fixtures import frame_bits, packed_capture, waveform


class EMDecodeTest(unittest.TestCase):
    def test_manchester_all_rates_offsets_and_polarities(self):
        for rate in RATES:
            config = ((rate // 2) - 1) | (1 << 6)
            for inverted in (False, True):
                with self.subTest(rate=rate, inverted=inverted):
                    samples = waveform(
                        frame_bits(0x01234567), rate=rate, inverted=inverted
                    )
                    self.assertEqual(
                        decode_capture(samples, config=config)["word"], 0x01234567
                    )

    def test_biphase_all_rates(self):
        for rate in RATES:
            config = ((rate // 2) - 1) | (2 << 6)
            with self.subTest(rate=rate):
                self.assertEqual(
                    decode_capture(
                        waveform(frame_bits(0x13579BDF), encoding="biphase", rate=rate),
                        config=config,
                    )["word"],
                    0x13579BDF,
                )

    def test_config_must_match_the_observed_encoding_and_rate(self):
        word = 31 | (1 << 6)
        value = decode_capture(waveform(frame_bits(word)), expect_config=True)
        self.assertEqual(value["word"], word)
        self.assertEqual(value["rate"], 64)
        with self.assertRaises(EMReadError):
            decode_capture(waveform(frame_bits(word), rate=32), expect_config=True)

    def test_row_column_and_stop_errors_are_rejected(self):
        for position in (8, 16, 44, 52):
            bits = frame_bits(0x76543210)
            bits[position] ^= 1
            with self.subTest(position=position), self.assertRaises(EMReadError):
                decode_capture(waveform(bits), config=31 | (1 << 6))

    def test_truncated_frame_is_rejected(self):
        with self.assertRaises(EMReadError):
            decode_capture(waveform(frame_bits(123)[:-5]), config=31 | (1 << 6))

    def test_failure_preamble_is_reported(self):
        with self.assertRaisesRegex(EMReadError, "error preamble"):
            decode_capture(waveform(FAILURE), config=31 | (1 << 6))

    def test_ambiguity_is_not_guessed(self):
        bits = frame_bits(0x12345678) + [1, 0, 1, 0] + frame_bits(0x76543210)
        with self.assertRaisesRegex(EMReadError, "ambiguous"):
            decode_capture(waveform(bits, rate=32), config=15 | (1 << 6))

    def test_flat_and_noise_do_not_decode(self):
        rng = random.Random(35)
        for samples in (
            (1100,) * SAMPLES,
            tuple(rng.randrange(300, 1900) for _ in range(SAMPLES)),
        ):
            with self.assertRaises(EMReadError):
                decode_capture(samples, config=31 | (1 << 6))

    def test_header_and_sample_count_are_strict(self):
        payload = packed_capture()
        self.assertEqual(len(unpack_capture(payload)), SAMPLES)
        for bad in (
            payload[:-1],
            payload + b"x",
            bytes([2]) + payload[1:],
            struct.pack("!BBHhh", 1, 4, SAMPLES, 2, 1) + payload[8:],
        ):
            with self.assertRaises(EMReadError):
                unpack_capture(bad)

    def test_unsupported_psk_and_reserved_rate_are_rejected(self):
        for config in (0, 0xFFFFFFFF, 31 | (4 << 6), 30 | (1 << 6)):
            with self.assertRaises(EMReadError):
                config_parameters(config)
