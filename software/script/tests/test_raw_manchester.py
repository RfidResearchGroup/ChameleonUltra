"""Compile and exercise the production encoder without the Nordic SDK."""

import pathlib
import subprocess
import tempfile
import unittest


class RawManchesterTest(unittest.TestCase):
    def test_production_encoder(self):
        root = pathlib.Path(__file__).resolve().parents[3]
        source = root / "firmware/application/src/rfid/nfctag/lf/protocols"
        harness = r'''
#include <assert.h>
#include <string.h>
#include "raw_manchester.h"
int main(void) {
    const uint8_t data[] = {0xa5};
    lf_raw_manchester_entry_t out[12], before[12];
    memset(out, 0x55, sizeof(out));
    memcpy(before, out, sizeof(out));
    assert(!lf_raw_manchester_encode(data, 1, 8, 16, false, false, out, 12));
    assert(!memcmp(before, out, sizeof(out)));
    assert(!lf_raw_manchester_encode(data, 1, 8, 32, false, true, out, 11));
    assert(!memcmp(before, out, sizeof(out)));
    assert(!lf_raw_manchester_encode(data, 1, 7, 32, false, false, out, 12));
    for (int clock = 32; clock <= 64; clock += 32) {
        for (int inv = 0; inv < 2; inv++) {
            if (inv) {
                assert(!lf_raw_manchester_encode(data, 1, 8, clock, inv, true, out, 12));
                assert(lf_raw_manchester_encode(data, 1, 8, clock, inv, false, out, 12) == 8);
                continue;
            }
            assert(lf_raw_manchester_encode(data, 1, 8, clock, inv, true, out, 12) == 12);
            for (int i = 0; i < 12; i++) assert(out[i].top == clock);
            assert(out[0].compare[0] == ((clock / 2) | (inv ? 0 : 0x8000)));
            assert(out[1].compare[0] == (clock | (inv ? 0 : 0x8000)));
            for (int i = 0; i < 8; i++) {
                int bit = (data[0] >> (7-i)) & 1;
                assert(out[i+4].compare[0] == ((clock/2) | ((bit != inv) ? 0x8000 : 0)));
            }
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            test = pathlib.Path(tmp) / "test.c"
            binary = pathlib.Path(tmp) / "test"
            test.write_text(harness)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(source),
                 str(test), str(source / "raw_manchester.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
