#!/usr/bin/env python3
"""Compile and exercise Paradox firmware codec through its public protocol API."""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import main


HOST_PWM_HEADER = r"""
#pragma once
#include <stdint.h>

typedef struct {
    uint16_t channel_0;
    uint16_t channel_1;
    uint16_t channel_2;
    uint16_t counter_top;
} nrf_pwm_values_wave_form_t;

typedef union {
    nrf_pwm_values_wave_form_t *p_wave_form;
} nrf_pwm_values_t;

typedef struct {
    nrf_pwm_values_t values;
    uint16_t length;
    uint32_t repeats;
    uint32_t end_delay;
} nrf_pwm_sequence_t;
"""


HOST_TEST = r"""
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "protocols/paradox.h"
#include "protocols/t55xx.h"

#define SAMPLE_RATE (125000.0)
#define PI (3.14159265358979323846)
#define FSK_SAMPLES_PER_BIT (50)
#define FRAME_BITS (96)
#define BUFFER_BITS (104)
#define FRAME_BYTES (12)
#define BUFFER_BYTES (13)

static const uint8_t expected_data[PARADOX_DATA_SIZE] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xB0,
};

/* 0x0F preamble followed by 44 Manchester pairs for expected_data. */
static const uint8_t expected_frame[FRAME_BYTES] = {
    0x0F, 0x56, 0x59, 0x5A, 0x65, 0x66,
    0x69, 0x6A, 0x95, 0x96, 0x99, 0x9A,
};

/* Decoder confirms frame preamble with next frame's 0x0F preamble. */
static const uint8_t expected_stream[BUFFER_BYTES] = {
    0x0F, 0x56, 0x59, 0x5A, 0x65, 0x66,
    0x69, 0x6A, 0x95, 0x96, 0x99, 0x9A, 0x0F,
};

static const uint32_t expected_t55xx_blocks[PARADOX_T55XX_BLOCK_COUNT] = {
    0x00107070,
    0x0F56595A,
    0x6566696A,
    0x9596999A,
};

static uint8_t frame_bit(const uint8_t *frame, uint16_t position) {
    return (uint8_t)((frame[position / 8] >> (7 - (position % 8))) & 1);
}

static int feed_bits(void *codec, const uint8_t *bits, uint16_t bit_count, uint32_t *sample_index) {
    int found = 0;

    for (uint16_t bit_index = 0; bit_index < bit_count; bit_index++) {
        double frequency = frame_bit(bits, bit_index) ? 12500.0 : 15625.0;
        for (uint16_t sample = 0; sample < FSK_SAMPLES_PER_BIT; sample++) {
            double phase = 2.0 * PI * frequency * *sample_index / SAMPLE_RATE;
            uint16_t value = (uint16_t)(20000.0 + 10000.0 * sin(phase));
            *sample_index += 1;
            if (paradox.decoder.feed(codec, value)) {
                found++;
            }
        }
    }

    return found;
}

static void test_valid_payload(void) {
    void *codec = paradox.alloc();
    uint32_t sample_index = 0;
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(memcmp(paradox.get_data(codec), expected_data, PARADOX_DATA_SIZE) == 0);
    paradox.free(codec);
}

static void test_bad_preamble(void) {
    uint8_t bad_frame[BUFFER_BYTES];
    void *codec = paradox.alloc();
    uint32_t sample_index = 0;
    memcpy(bad_frame, expected_stream, sizeof(bad_frame));
    bad_frame[0] ^= 0x01;
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, bad_frame, BUFFER_BITS, &sample_index) == 0);
    paradox.free(codec);
}

static void test_manchester_resync(void) {
    uint8_t bad_frame[BUFFER_BYTES];
    const uint8_t noise[1] = {0xA8};
    void *codec = paradox.alloc();
    uint32_t sample_index = 0;
    memcpy(bad_frame, expected_stream, sizeof(bad_frame));
    bad_frame[1] &= 0x3F; /* first pair becomes 00, not a Manchester pair */
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, bad_frame, BUFFER_BITS, &sample_index) == 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(memcmp(paradox.get_data(codec), expected_data, PARADOX_DATA_SIZE) == 0);

    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, noise, 7, &sample_index) == 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(memcmp(paradox.get_data(codec), expected_data, PARADOX_DATA_SIZE) == 0);
    paradox.free(codec);
}

static void test_repeated_frames_and_reset(void) {
    void *codec = paradox.alloc();
    uint32_t sample_index = 0;
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);

    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(memcmp(paradox.get_data(codec), expected_data, PARADOX_DATA_SIZE) == 0);
    paradox.free(codec);
}

static void test_payload_preserved_after_rejected_frame(void) {
    uint8_t bad_frame[BUFFER_BYTES];
    void *codec = paradox.alloc();
    uint32_t sample_index = 0;
    memcpy(bad_frame, expected_stream, sizeof(bad_frame));
    bad_frame[0] ^= 0x01;
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    assert(feed_bits(codec, expected_stream, BUFFER_BITS, &sample_index) == 1);
    assert(feed_bits(codec, bad_frame, BUFFER_BITS, &sample_index) == 0);
    assert(memcmp(paradox.get_data(codec), expected_data, PARADOX_DATA_SIZE) == 0);
    paradox.free(codec);
}

static void test_t55xx_writer_preserves_wire_bits(void) {
    uint8_t data_with_padding[PARADOX_DATA_SIZE] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBF,
    };
    uint32_t blocks[PARADOX_T55XX_BLOCK_COUNT] = {0};

    assert(paradox_t55xx_writer((uint8_t *)expected_data, blocks) == PARADOX_T55XX_BLOCK_COUNT);
    assert(memcmp(blocks, expected_t55xx_blocks, sizeof(blocks)) == 0);

    /* The final four data bits are decoder padding and must not reach T5577. */
    assert(paradox_t55xx_writer(data_with_padding, blocks) == PARADOX_T55XX_BLOCK_COUNT);
    assert(memcmp(blocks, expected_t55xx_blocks, sizeof(blocks)) == 0);
    assert(blocks[0] == T5577_PARADOX_CONFIG);
}

static void test_pwm_frame_consistency(void) {
    uint16_t expected_entry_count = 0;
    nrf_pwm_sequence_t *sequence = paradox.modulator(NULL, (uint8_t *)expected_data);
    assert(sequence != NULL);
    assert(sequence->length % 4 == 0);

    uint16_t entry_index = 0;
    for (uint16_t bit_index = 0; bit_index < FRAME_BITS; bit_index++) {
        bool bit = frame_bit(expected_frame, bit_index) != 0;
        uint16_t entries = bit ? 5 : 6;
        uint16_t top = bit ? 10 : 8;
        uint16_t duty = bit ? 5 : 4;
        expected_entry_count += entries;
        for (uint16_t entry = 0; entry < entries; entry++) {
            assert(sequence->values.p_wave_form[entry_index].counter_top == top);
            assert(sequence->values.p_wave_form[entry_index].channel_0 == duty);
            entry_index++;
        }
    }
    assert(entry_index == expected_entry_count);
    assert(sequence->length == entry_index * 4);
}

int main(void) {
    test_valid_payload();
    test_bad_preamble();
    test_manchester_resync();
    test_repeated_frames_and_reset();
    test_payload_preserved_after_rejected_frame();
    test_t55xx_writer_preserves_wire_bits();
    test_pwm_frame_consistency();
    return 0;
}
"""


class TestParadoxCodec(unittest.TestCase):
    def test_firmware_codec(self):
        root = Path(__file__).resolve().parents[3]
        source_root = root / "firmware" / "application" / "src"
        with tempfile.TemporaryDirectory() as temp_name:
            temp = Path(temp_name)
            (temp / "nrf_pwm.h").write_text(HOST_PWM_HEADER)
            harness = temp / "paradox_host_test.c"
            harness.write_text(HOST_TEST)
            binary = temp / "paradox_host_test"
            command = [
                "gcc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{temp}",
                f"-I{source_root / 'rfid' / 'nfctag'}",
                f"-I{source_root / 'rfid' / 'nfctag' / 'lf'}",
                f"-I{source_root}",
                str(harness),
                str(source_root / "rfid" / "nfctag" / "lf" / "protocols" / "paradox.c"),
                str(source_root / "rfid" / "nfctag" / "lf" / "utils" / "fskdemod.c"),
                "-lm",
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=root, check=True)
            subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == "__main__":
    main()
