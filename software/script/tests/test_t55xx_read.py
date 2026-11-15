"""Reference Manchester fixtures for the conservative T5577 read profile."""

import random
import struct
import unittest
from unittest.main import TestProgram
from unittest.mock import patch

from t55xx_read import (
    MAX_CAPTURE_SAMPLES,
    T55xxConfig,
    T55xxReadError,
    T55xxUnsupportedError,
    config_from_word,
    decode_block,
    detect_config,
    unpack_capture,
)

CONFIG = 0x00148040
CONFIGS = ((32, 0x00088040), (64, CONFIG))


def reference_capture(
    word,
    *,
    start=80,
    inverted=False,
    dc=1200,
    amplitude=200,
    jitter=0,
    noise=0,
    repeats=3,
    bitrate=64,
):
    """Encode independently as half-cell levels inside fixed carrier clocks."""
    rng = random.Random(0x5577)
    samples = [dc + amplitude] * start
    for _ in range(repeats):
        for shift in range(31, -1, -1):
            data = (word >> shift) & 1
            first = amplitude if data == 0 else -amplitude
            if inverted:
                first = -first
            edge = bitrate // 2 + rng.randint(-jitter, jitter)
            for index in range(bitrate):
                value = dc + (first if index < edge else -first)
                value += rng.randint(-noise, noise)
                samples.append(value)
    samples.extend([dc + amplitude] * 256)
    return samples


def pack_reference(samples):
    """Serialize with the specified U4 rounding, without using the decoder."""
    low, high = min(samples), max(samples)
    span = high - low
    nibbles = [
        ((value - low) * 15 + span // 2) // span if span else 8 for value in samples
    ]
    packed = bytearray()
    for index in range(0, len(nibbles), 2):
        low_nibble = nibbles[index + 1] if index + 1 < len(nibbles) else 0
        packed.append((nibbles[index] << 4) | low_nibble)
    return struct.pack("!BBHhh", 1, 4, len(samples), low, high) + packed


class T55xxCaptureFormatTest(unittest.TestCase):
    def test_quantized_capture_preserves_supported_signal(self):
        for bitrate, word in CONFIGS:
            with self.subTest(bitrate=bitrate):
                source = reference_capture(word, noise=6, bitrate=bitrate)
                samples = unpack_capture(pack_reference(source))
                self.assertEqual(len(samples), len(source))
                self.assertEqual(detect_config(samples).word, word)

    def test_signed_range_and_odd_sample_count(self):
        self.assertEqual(
            unpack_capture(struct.pack("!BBHhh", 1, 4, 3, -100, 200) + b"\x0f\x80"),
            (-100, 200, 60),
        )

    def test_flat_capture_is_valid_wire_data_but_not_a_tag(self):
        source = [123] * 7000
        self.assertEqual(set(unpack_capture(pack_reference(source))), {123})
        with self.assertRaisesRegex(T55xxReadError, "amplitude"):
            detect_config(unpack_capture(pack_reference(source)))

    def test_rejects_invalid_capture_headers_and_lengths(self):
        cases = [
            b"",
            b"\x01" * 7,
            struct.pack("!BBHhh", 2, 4, 2, 0, 1) + b"\x0f",
            struct.pack("!BBHhh", 1, 8, 2, 0, 1) + b"\x0f",
            struct.pack("!BBHhh", 1, 4, 0, 0, 1),
            struct.pack("!BBHhh", 1, 4, MAX_CAPTURE_SAMPLES + 1, 0, 1),
            struct.pack("!BBHhh", 1, 4, 2, 5, 4) + b"\x0f",
            struct.pack("!BBHhh", 1, 4, 3, 0, 15) + b"\x0f",
            struct.pack("!BBHhh", 1, 4, 2, 0, 15) + b"\x0f\x00",
            struct.pack("!BBHhh", 1, 4, 3, 0, 15) + b"\x0f\x81",
        ]
        for payload in cases:
            with self.subTest(payload=payload.hex()), self.assertRaises(T55xxReadError):
                unpack_capture(payload)


class T55xxConfigTest(unittest.TestCase):
    def test_reference_configs_offsets_dc_polarity_and_jitter(self):
        for bitrate, base in CONFIGS:
            for safer in (0, 0x60000000, 0x90000000):
                for start in (64, 80, 113):
                    for inverted in (False, True):
                        word = base | safer
                        with self.subTest(
                            word=hex(word), start=start, inverted=inverted
                        ):
                            source = reference_capture(
                                word,
                                start=start,
                                dc=-700,
                                inverted=inverted,
                                jitter=3,
                                noise=8,
                                bitrate=bitrate,
                            )
                            config = detect_config(source)
                            self.assertEqual(config.word, word)
                            self.assertEqual(config.modulation, "manchester")
                            self.assertEqual(config.bitrate, bitrate)
                            self.assertEqual(config.max_block, 2)
                            self.assertEqual(config.inverted, inverted)
                            self.assertLessEqual(abs(config.start_sample - start), 4)

    def test_configuration_detected_once_must_repeat_three_times(self):
        for bitrate, word in CONFIGS:
            with self.subTest(bitrate=bitrate):
                source = reference_capture(word, bitrate=bitrate)
                # Corrupt the second copy while keeping valid Manchester cells.
                replacement = reference_capture(word ^ 0x20, bitrate=bitrate)
                period = 32 * bitrate
                source[80 + period : 80 + 2 * period] = replacement[
                    80 + period : 80 + 2 * period
                ]
                with self.assertRaises(T55xxReadError):
                    detect_config(source)

    def test_unsupported_configuration_flags_are_not_silently_ignored(self):
        for mask in (0x10, 0x200, 0x100, 0x8, 0x4, 0x2, 0x1, 0x20000, 0x400):
            with self.subTest(mask=hex(mask)), self.assertRaises(T55xxUnsupportedError):
                config_from_word(CONFIG | mask, start_sample=80, inverted=False)
        for word in (0x00107060, 0x00048040, 0x00141040):
            with self.subTest(word=hex(word)), self.assertRaises(T55xxUnsupportedError):
                config_from_word(word, start_sample=80, inverted=False)
        for word in (0, 0xFFFFFFFF, -1, 0x100000000, CONFIG | 0x01000000):
            with self.subTest(word=word), self.assertRaises(T55xxReadError):
                config_from_word(word, start_sample=80, inverted=False)

    def test_detect_reports_password_profile_as_unsupported(self):
        for bitrate, word in CONFIGS:
            with (
                self.subTest(bitrate=bitrate),
                self.assertRaisesRegex(T55xxUnsupportedError, "password"),
            ):
                detect_config(reference_capture(word | 0x10, bitrate=bitrate))

    def test_basic_inverse_bit_rejected_but_analog_polarity_is_calibrated(self):
        # Inverse-data is reserved in basic mode, while the analog receiver can
        # invert an otherwise supported waveform. These are different facts.
        for bitrate, base in CONFIGS:
            for wire_inverted in (False, True):
                with self.subTest(bitrate=bitrate, wire_inverted=wire_inverted):
                    config = detect_config(
                        reference_capture(base, bitrate=bitrate, inverted=wire_inverted)
                    )
                    self.assertEqual(config.word & 2, 0)
                    self.assertEqual(config.inverted, wire_inverted)
                    self.assertEqual(
                        decode_block(
                            reference_capture(
                                0x0F1234A5,
                                bitrate=bitrate,
                                inverted=wire_inverted,
                            ),
                            config,
                        ),
                        0x0F1234A5,
                    )
                    with self.assertRaisesRegex(T55xxUnsupportedError, "reserved"):
                        detect_config(
                            reference_capture(
                                base | 2, bitrate=bitrate, inverted=wire_inverted
                            )
                        )

    def test_no_word_rotation_fallback_for_unanchored_or_late_capture(self):
        for bitrate, word in CONFIGS:
            for start in (0, 1024, 2048):
                with (
                    self.subTest(bitrate=bitrate, start=start),
                    self.assertRaises(T55xxReadError),
                ):
                    detect_config(reference_capture(word, start=start, bitrate=bitrate))

    def test_noise_cannot_supply_a_configuration(self):
        rng = random.Random(12)
        for source in (
            [1000] * 7000,
            [1000 + rng.randint(-2, 2) for _ in range(7000)],
            [1000 + rng.randint(-200, 200) for _ in range(7000)],
        ):
            with self.assertRaises(T55xxReadError):
                detect_config(source)

    def test_multiple_plausible_configuration_words_are_rejected(self):
        # Exercise the policy independently of a confidence metric: a decoder
        # with two valid candidates must fail even if one signal looks stronger.
        def candidates(_signal, start, inverted, bitrate):
            if inverted or bitrate != 64:
                return None
            return CONFIG if start < 90 else CONFIG ^ 0x20

        with (
            patch("t55xx_read._read_repeated_word", candidates),
            self.assertRaisesRegex(T55xxReadError, "ambiguous"),
        ):
            detect_config(reference_capture(CONFIG))

    def test_rf32_rf64_ambiguity_is_not_resolved_by_search_order(self):
        def candidates(_signal, _start, inverted, bitrate):
            if inverted:
                return None
            return 0x00088040 if bitrate == 32 else CONFIG

        with (
            patch("t55xx_read._read_repeated_word", candidates),
            self.assertRaisesRegex(T55xxReadError, "ambiguous"),
        ):
            detect_config(reference_capture(CONFIG))

    def test_rejects_invalid_acquisition_window(self):
        for window in ((90, 80), (-1, 80), (0, 2048)):
            with self.subTest(window=window), self.assertRaises(T55xxReadError):
                detect_config(reference_capture(CONFIG), start_window=window)


class T55xxBlockTest(unittest.TestCase):
    def test_block_decode_uses_config_calibration_without_expected_word(self):
        for bitrate, base in CONFIGS:
            for inverted in (False, True):
                config = detect_config(
                    reference_capture(base, inverted=inverted, bitrate=bitrate)
                )
                for word in (0, 0xFFFFFFFF, 0x12345678, 0xAAAAAAAA, 0x80000001):
                    for offset in (-3, 0, 3):
                        with self.subTest(
                            bitrate=bitrate,
                            word=hex(word),
                            inverted=inverted,
                            offset=offset,
                        ):
                            samples = reference_capture(
                                word,
                                start=80 + offset,
                                inverted=inverted,
                                noise=7,
                                jitter=3,
                                bitrate=bitrate,
                            )
                            self.assertEqual(decode_block(samples, config), word)

    def test_disagreement_between_word_copies_fails(self):
        for bitrate, base in CONFIGS:
            with self.subTest(bitrate=bitrate):
                config = detect_config(reference_capture(base, bitrate=bitrate))
                samples = reference_capture(0x12345678, bitrate=bitrate)
                other = reference_capture(0x12345679, bitrate=bitrate)
                period = 32 * bitrate
                samples[80 + 2 * period : 80 + 3 * period] = other[
                    80 + 2 * period : 80 + 3 * period
                ]
                with self.assertRaisesRegex(T55xxReadError, "repeat"):
                    decode_block(samples, config)

    def test_does_not_search_a_later_full_block_or_unknown_polarity(self):
        for bitrate, base in CONFIGS:
            with self.subTest(bitrate=bitrate):
                config = detect_config(reference_capture(base, bitrate=bitrate))
                with self.assertRaises(T55xxReadError):
                    decode_block(
                        reference_capture(0x12345678, start=500, bitrate=bitrate),
                        config,
                    )
                # Wrong polarity stays the complement: arbitrary data alone
                # cannot establish a receiver's analog sign.
                self.assertEqual(
                    decode_block(
                        reference_capture(0x12345678, inverted=True, bitrate=bitrate),
                        config,
                    ),
                    0xEDCBA987,
                )

    def test_inconsistent_profile_and_short_or_invalid_samples_fail(self):
        for config in (
            T55xxConfig(CONFIG, 80, False, bitrate=32),
            T55xxConfig(CONFIG | 0x10, 80, False),
            T55xxConfig(CONFIG, -1, False),
        ):
            with self.subTest(config=config), self.assertRaises(T55xxReadError):
                decode_block(reference_capture(123), config)
        config = detect_config(reference_capture(CONFIG))
        for samples in ([1] * 100, [40000] * 7000, [1] * (MAX_CAPTURE_SAMPLES + 1)):
            with self.assertRaises(T55xxReadError):
                decode_block(samples, config)


if __name__ == "__main__":
    TestProgram()
