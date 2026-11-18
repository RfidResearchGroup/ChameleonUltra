#include <assert.h>
#include <string.h>
#include "raw_manchester.h"

int main(void) {
    assert(!lf_raw_manchester_repetitions(0, 5000));
    assert(!lf_raw_manchester_repetitions(32, 0));
    assert(!lf_raw_manchester_repetitions(32, 5001));
    assert(!lf_raw_manchester_repetitions(UINT32_MAX, 1));
    assert(!lf_raw_manchester_repetitions(224 * 64, 1));
    for (uint32_t ticks = 32; ticks <= 228 * 64; ticks += 32) {
        for (uint32_t ms = 1; ms <= 5000; ms++) {
            uint16_t n = lf_raw_manchester_repetitions(ticks, ms);
            assert(n * ticks <= ms * 125u);
            assert((n + 1u) * ticks > ms * 125u);
        }
    }
    uint8_t record[32] = {1, 32, 0, 8, 0xAA};
    lf_raw_manchester_entry_t out[LF_RAW_MANCHESTER_MAX_ENTRIES];
    assert(lf_raw_manchester_validate(record, sizeof(record)));
    assert(!lf_raw_manchester_validate(NULL, 32));
    for (size_t n = 0; n < 33; n++) {
        if (n != 32) assert(!lf_raw_manchester_validate(record, n));
    }
    for (unsigned bits = 1; bits <= 224; bits++) {
        for (unsigned clock = 32; clock <= 64; clock += 32) {
            for (unsigned flags = 0; flags <= 255; flags++) {
                memset(record, 0, sizeof(record));
                record[0] = 1; record[1] = clock; record[2] = flags; record[3] = bits;
                assert(lf_raw_manchester_validate(record, 32) == (flags <= 2));
                if (flags > 2) continue;
                size_t count = lf_raw_manchester_encode(record + 4, (bits + 7) / 8,
                    bits, clock, flags & 2, flags & 1, out, LF_RAW_MANCHESTER_MAX_ENTRIES);
                assert(count == bits + ((flags & 1) ? 4 : 0));
                assert(out[count - 1].top == clock);
                assert(out[count - 1].compare[0] == (clock / 2 | ((flags & 2) ? 0x8000 : 0)));
                for (size_t bit = bits; bit < 224; bit++) {
                    record[4 + bit / 8] |= 1u << (7 - bit % 8);
                    assert(!lf_raw_manchester_validate(record, 32));
                    record[4 + bit / 8] = 0;
                }
            }
        }
    }
    memset(record, 0, sizeof(record));
    record[0] = 1; record[1] = 32; record[3] = 1;
    record[0] = 2; assert(!lf_raw_manchester_validate(record, 32)); record[0] = 1;
    record[1] = 16; assert(!lf_raw_manchester_validate(record, 32)); record[1] = 32;
    record[3] = 0; assert(!lf_raw_manchester_validate(record, 32));
    record[3] = 225; assert(!lf_raw_manchester_validate(record, 32));
    memset(out, 0xA5, sizeof(out));
    lf_raw_manchester_entry_t before[LF_RAW_MANCHESTER_MAX_ENTRIES];
    memcpy(before, out, sizeof(out));
    assert(!lf_raw_manchester_encode(record + 4, 1, 1, 32, true, true, out, LF_RAW_MANCHESTER_MAX_ENTRIES));
    assert(memcmp(before, out, sizeof(out)) == 0);
    return 0;
}
