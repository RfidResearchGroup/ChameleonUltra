#include <assert.h>
#include <stdio.h>
#include "mfu_profile_wire.h"

int main(void) {
    uint8_t expected[8] = {1, 7, 4, 76, 0, 45, 0, 56}; /* NTAG213, slot 8 */
    uint8_t data[11 + 242 * 4] = {0};
    memcpy(data, expected, 8);
    data[8] = 0xF3; /* opaque reserved bits + detection + write denied + magic */
    size_t length = 11 + 56 * 4;
    assert(mfu_profile_write_valid(data, length, expected, 56, 0));
    assert(mfu_profile_write_valid(data, length, expected, 56, 1));
    assert(mfu_profile_write_valid(data, length, expected, 56, 2));
    assert(!mfu_profile_write_valid(data, length, expected, 56, 3));
    assert(!mfu_profile_write_valid(data, length, expected, 56, 4));
    for (size_t i = 0; i < 8; ++i) {
        data[i] ^= 1;
        assert(!mfu_profile_write_valid(data, length, expected, 56, 0));
        data[i] ^= 1;
    }
    for (size_t short_length = 0; short_length < length; ++short_length) {
        assert(!mfu_profile_write_valid(data, short_length, expected, 56, 0));
    }
    assert(!mfu_profile_write_valid(data, length + 1, expected, 56, 0));
    assert(!mfu_profile_write_valid(NULL, length, expected, 56, 0));
    assert(!mfu_profile_write_valid(data, length, NULL, 56, 0));
    assert(!mfu_profile_write_valid(data, length, expected, 0, 0));
    assert(!mfu_profile_write_valid(data, length, expected, 256, 0));
    for (unsigned int mode = 3; mode < 8; ++mode) {
        data[8] = (uint8_t)(mode << 1);
        assert(!mfu_profile_write_valid(data, length, expected, 56, 0));
    }
    const uint16_t totals[] = {56,146,242,16,48,33,54,31,52};
    const uint16_t physical[] = {45,135,231,16,48,20,41,20,41};
    for (size_t i = 0; i < sizeof(totals) / sizeof(totals[0]); ++i) {
        expected[2] = (1100U + i) >> 8U;
        expected[3] = (1100U + i) & 0xFFU;
        expected[4] = physical[i] >> 8U;
        expected[5] = physical[i] & 0xFFU;
        expected[6] = totals[i] >> 8U;
        expected[7] = totals[i] & 0xFFU;
        memcpy(data, expected, 8);
        data[8] = 0;
        assert(mfu_profile_write_valid(data, 11 + totals[i] * 4, expected, totals[i], 0));
    }
    puts("MFU profile descriptors, all supported lengths and write-policy guards passed");
    return 0;
}
