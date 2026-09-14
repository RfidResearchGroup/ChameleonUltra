#!/usr/bin/env python3
"""Regression tests for Paradox decoder phase and DC-margin failures."""

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
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocols/paradox.h"

#define SAMPLE_RATE (125000.0)
#define PI (3.14159265358979323846)
#define BIT_SAMPLES (50.0)
#define FRAME_BITS (96)
#define MAX_SAMPLES (60000)
#define PPM_SCALE (1000000.0)

static uint8_t frame_bit(const uint8_t *data, uint32_t bit_index, bool malformed) {
    uint32_t bit = bit_index % FRAME_BITS;
    if (bit < 8) {
        uint8_t preamble = 0x0F;
        if (malformed && bit == 0) {
            preamble ^= 0x80;
        }
        return (uint8_t)((preamble >> (7 - bit)) & 1);
    }

    uint32_t payload_bit = (bit - 8) / 2;
    if (payload_bit >= 44) {
        return 0;
    }
    uint8_t value = (uint8_t)((data[payload_bit / 8] >> (7 - (payload_bit % 8))) & 1);
    if (malformed && payload_bit == 9) {
        value ^= 1;
    }
    return ((bit - 8) & 1) ? (uint8_t)!value : value;
}

static bool feed_signal(
    const uint8_t *data,
    uint32_t sample_offset,
    double dc,
    double amplitude,
    double initial_phase,
    double tx_ppm,
    bool malformed) {
    void *codec = paradox.alloc();
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);

    double phase = initial_phase;
    double tx_scale = 1.0 + tx_ppm / PPM_SCALE;
    for (uint32_t sample_index = 0; sample_index < MAX_SAMPLES; sample_index++) {
        double tx_position = ((double)sample_index + sample_offset) * tx_scale;
        uint32_t bit_index = (uint32_t)floor(tx_position / BIT_SAMPLES);
        double frequency = frame_bit(data, bit_index, malformed) ? 12500.0 : 15625.0;
        phase += 2.0 * PI * frequency * tx_scale / SAMPLE_RATE;
        uint16_t sample = (uint16_t)llround(dc + amplitude * sin(phase));
        if (paradox.decoder.feed(codec, sample)) {
            bool match = memcmp(paradox.get_data(codec), data, PARADOX_DATA_SIZE) == 0;
            paradox.free(codec);
            return match;
        }
    }

    paradox.free(codec);
    return false;
}

static bool feed_constant(uint16_t sample) {
    void *codec = paradox.alloc();
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    for (uint32_t i = 0; i < MAX_SAMPLES; i++) {
        if (paradox.decoder.feed(codec, sample)) {
            paradox.free(codec);
            return true;
        }
    }
    paradox.free(codec);
    return false;
}

static bool feed_noise(void) {
    void *codec = paradox.alloc();
    uint32_t lfsr = 0x13579BDF;
    assert(codec != NULL);
    paradox.decoder.start(codec, 0);
    for (uint32_t i = 0; i < MAX_SAMPLES; i++) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
        uint16_t sample = (uint16_t)(7000u + (lfsr & 0x7FFu));
        if (paradox.decoder.feed(codec, sample)) {
            paradox.free(codec);
            return true;
        }
    }
    paradox.free(codec);
    return false;
}

static void test_all_sample_offsets_and_payloads(void) {
    const uint8_t payloads[][PARADOX_DATA_SIZE] = {
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xB0},
        {0xA5, 0xC3, 0x3C, 0x5A, 0x01, 0x90},
    };

    for (size_t payload = 0; payload < sizeof(payloads) / sizeof(payloads[0]); payload++) {
        for (uint32_t offset = 0; offset < 50; offset++) {
            assert(feed_signal(payloads[payload], offset, 8192.0, 2000.0, 0.0, 0.0, false));
        }
    }
}

static void test_dc_margin_and_carrier_phase(void) {
    const uint8_t payload[PARADOX_DATA_SIZE] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xB0};
    for (uint32_t offset = 0; offset < 50; offset++) {
        assert(feed_signal(payload, offset, 8192.0, 500.0, PI / 2.0, 0.0, false));
    }
    assert(feed_signal(payload, 17, 12000.0, 1500.0, PI / 4.0, 200.0, false));
    assert(feed_signal(payload, 31, 8192.0, 1500.0, PI, -200.0, false));
}

static void test_rejects_invalid_input(void) {
    const uint8_t payload[PARADOX_DATA_SIZE] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xB0};
    assert(!feed_constant(8192));
    assert(!feed_noise());
    assert(!feed_signal(payload, 0, 8192.0, 2000.0, 0.0, 0.0, true));
}

static void test_reset_reuses_all_phase_state(void) {
    const uint8_t first[PARADOX_DATA_SIZE] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xB0};
    const uint8_t second[PARADOX_DATA_SIZE] = {0xA5, 0xC3, 0x3C, 0x5A, 0x01, 0x90};
    void *codec = paradox.alloc();
    double phase = 0.0;
    assert(codec != NULL);

    paradox.decoder.start(codec, 0);
    for (uint32_t i = 0; i < 12000; i++) {
        uint32_t bit = (uint32_t)floor((double)i / BIT_SAMPLES);
        double frequency = frame_bit(first, bit, false) ? 12500.0 : 15625.0;
        phase += 2.0 * PI * frequency / SAMPLE_RATE;
        if (paradox.decoder.feed(codec, (uint16_t)llround(8192.0 + 2000.0 * sin(phase)))) {
            assert(memcmp(paradox.get_data(codec), first, PARADOX_DATA_SIZE) == 0);
            break;
        }
        assert(i + 1 < 12000);
    }

    paradox.decoder.start(codec, 0);
    phase = PI / 2.0;
    bool found = false;
    for (uint32_t i = 0; i < 12000; i++) {
        uint32_t bit = (uint32_t)floor(((double)i + 25.0) / BIT_SAMPLES);
        double frequency = frame_bit(second, bit, false) ? 12500.0 : 15625.0;
        phase += 2.0 * PI * frequency / SAMPLE_RATE;
        if (paradox.decoder.feed(codec, (uint16_t)llround(8192.0 + 500.0 * sin(phase)))) {
            assert(memcmp(paradox.get_data(codec), second, PARADOX_DATA_SIZE) == 0);
            found = true;
            break;
        }
    }
    assert(found);
    paradox.free(codec);
}

int main(void) {
    test_all_sample_offsets_and_payloads();
    test_dc_margin_and_carrier_phase();
    test_rejects_invalid_input();
    test_reset_reuses_all_phase_state();
    puts("paradox reliability matrix passed");
    return 0;
}
"""


class TestParadoxReliability(unittest.TestCase):
    def test_firmware_decoder_reliability_matrix(self):
        root = Path(__file__).resolve().parents[3]
        source_root = root / "firmware" / "application" / "src"
        with tempfile.TemporaryDirectory() as temp_name:
            temp = Path(temp_name)
            (temp / "nrf_pwm.h").write_text(HOST_PWM_HEADER)
            harness = temp / "paradox_reliability_host_test.c"
            harness.write_text(HOST_TEST)
            binary = temp / "paradox_reliability_host_test"
            command = [
                "gcc",
                "-std=c11",
                "-O2",
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
