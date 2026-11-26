#!/usr/bin/env python3
"""Check EM410x decoder recovery at each frame phase and supported clock."""

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RFID = ROOT / "firmware/application/src/rfid"

SOURCE = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "em410x.h"

static bool level(uint64_t frame, unsigned time, unsigned clock) {
    return ((frame >> (63 - (time / clock) % 64)) & 1) ^ ((time / (clock / 2)) & 1);
}

static bool read_id(const protocol *reader, const uint8_t *id, unsigned clock,
                    unsigned phase, bool saturated) {
    uint32_t blocks[3];
    em410x_t55xx_writer((uint8_t *)id, blocks);
    uint64_t frame = ((uint64_t)blocks[1] << 32) | blocks[2];
    void *codec = reader->alloc();
    reader->decoder.start(codec, 0);
    bool previous = level(frame, phase, clock);
    bool found = false;
    unsigned last = 0;
    for (unsigned time = 1; time < 62500 && !found; time++) {
        bool current = level(frame, phase + time, clock);
        if (current && !previous) {
            unsigned interval = last ? time - last : (saturated ? 255 : time);
            last = time;
            found = reader->decoder.feed(codec, interval);
        }
        previous = current;
    }
    if (found) assert(memcmp(reader->get_data(codec), id, 5) == 0);
    reader->free(codec);
    return found;
}

static bool read_capture(const protocol *reader, const char *path, unsigned offset, const uint8_t *id) {
    FILE *input = fopen(path, "rb");
    assert(input && fseek(input, 2 * offset, SEEK_SET) == 0);
    void *codec = reader->alloc();
    reader->decoder.start(codec, 0);
    bool found = false;
    int low;
    while ((low = fgetc(input)) != EOF) {
        int high = fgetc(input);
        assert(high != EOF);
        if (reader->decoder.feed(codec, low | (high << 8))) {
            found = memcmp(reader->get_data(codec), id, 5) == 0;
            break;
        }
    }
    reader->free(codec);
    fclose(input);
    return found;
}

int main(int argc, char **argv) {
    const protocol *readers[] = {&em410x_16, &em410x_32, &em410x_64};
    const unsigned clocks[] = {16, 32, 64};
    const uint8_t ids[][5] = {
        {0x10, 0x20, 0x30, 0x40, 0x50},
        {0x02, 0xb7, 0xd3, 0xe4, 0x05},
        {0xa1, 0xb2, 0xc3, 0xd4, 0xe5},
    };
    unsigned checks = 0, missing = 0;
    for (unsigned mode = 0; mode < 3; mode++) {
        for (unsigned tag = 0; tag < 3; tag++) {
            for (unsigned initial = 0; initial < 2; initial++) {
                for (unsigned phase = 0; phase < 64 * clocks[mode]; phase += clocks[mode] / 2) {
                    if (!read_id(readers[mode], ids[tag], clocks[mode], phase, initial)) missing++;
                    checks++;
                }
            }
        }
    }
    printf("EM410x phase recovery: %u/%u passed\n", checks - missing, checks);
    assert((argc - 1) % 4 == 0);
    unsigned captured = 0;
    for (int arg = 1; arg < argc; arg += 4) {
        unsigned clock = strtoul(argv[arg], NULL, 10);
        unsigned mode = 0;
        while (mode < 3 && clocks[mode] != clock) mode++;
        assert(mode < 3 && strlen(argv[arg + 1]) == 10);
        uint8_t id[5];
        for (unsigned byte = 0; byte < 5; byte++) {
            assert(sscanf(argv[arg + 1] + 2 * byte, "%2hhx", &id[byte]) == 1);
        }
        unsigned offset = strtoul(argv[arg + 3], NULL, 10);
        bool found = read_capture(readers[mode], argv[arg + 2], offset, id);
        printf("EM410x capture %s offset %u: %s\n", argv[arg + 2], offset, found ? "passed" : "failed");
        if (!found) missing++;
        captured++;
    }
    printf("EM410x captured reads: %u checked\n", captured);
    return missing != 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / "nrf_pwm.h").write_text(
        "#pragma once\n#include <stdint.h>\n"
        "typedef struct { uint16_t channel_0, channel_1, channel_2, counter_top; } nrf_pwm_values_wave_form_t;\n"
        "typedef struct { union { const nrf_pwm_values_wave_form_t *p_wave_form; } values; "
        "uint16_t length; uint32_t repeats, end_delay; } nrf_pwm_sequence_t;\n"
        "#define NRF_PWM_VALUES_LENGTH(a) (sizeof(a) / (sizeof(uint16_t)))\n"
    )
    (tmp / "nordic_common.h").write_text(
        "#pragma once\n#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))\n"
        "#define IS_SET(value, bit) (((value) >> (bit)) & 1)\n"
    )
    for name in ("nrf_log.h", "nrf_log_ctrl.h", "nrf_log_default_backends.h"):
        (tmp / name).write_text("#pragma once\n#define NRF_LOG_MODULE_REGISTER()\n")
    (tmp / "test.c").write_text(SOURCE)
    executable = tmp / "test"
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-Wall",
            "-Werror",
            "-O2",
            "-I",
            str(tmp),
            "-I",
            str(RFID),
            "-I",
            str(RFID / "nfctag"),
            "-I",
            str(RFID / "nfctag/lf"),
            "-I",
            str(RFID / "nfctag/lf/protocols"),
            str(tmp / "test.c"),
            str(RFID / "nfctag/lf/protocols/em410x.c"),
            str(RFID / "nfctag/lf/utils/manchester.c"),
            str(RFID / "parity.c"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    captures = ROOT / "software/script/tests/data/em410x"
    arguments = [str(executable)]
    for capture in json.loads((captures / "captures.json").read_text()):
        path = captures / capture["file"]
        assert hashlib.sha256(path.read_bytes()).hexdigest() == capture["sha256"]
        for offset in capture["offsets"]:
            arguments.extend(
                [str(capture["clock"]), capture["id"], str(path), str(offset)]
            )
    subprocess.run(arguments, check=True)
