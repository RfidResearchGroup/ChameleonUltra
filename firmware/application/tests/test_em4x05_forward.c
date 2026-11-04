#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "em4x05_forward.h"

static void expect_bits(const uint8_t *bits, const char *expected) {
    for (size_t i = 0U; expected[i] != '\0'; ++i) assert(bits[i] == (uint8_t)(expected[i] - '0'));
}

int main(void) {
    uint8_t bits[64];
    memset(bits, 0xA5, sizeof(bits));
    assert(em4x05_read_bits(4, bits, sizeof(bits)) == 13);
    expect_bits(bits, "0010010010001");
    assert(bits[13] == 0xA5);
    assert(em4x05_read_bits(2, bits, sizeof(bits)) == 0);
    assert(em4x05_read_bits(16, bits, sizeof(bits)) == 0);
    assert(em4x05_read_bits(0, NULL, 64) == 0);
    assert(em4x05_read_bits(0, bits, 12) == 0);
    for (uint8_t address = 0; address < 16; ++address) {
        if (address == 2) continue;
        assert(em4x05_read_bits(address, bits, sizeof(bits)) == 13);
        expect_bits(bits, "001001");
        unsigned int parity = 0;
        for (unsigned int i = 0; i < 6; ++i) {
            assert(bits[6 + i] == ((address >> i) & 1));
            parity ^= bits[6 + i];
        }
        assert(bits[10] == 0 && bits[11] == 0 && bits[12] == parity);
    }
    assert(em4x05_login_bits(0, bits, sizeof(bits)) == 51);
    expect_bits(bits, "000011");
    for (unsigned int i = 6; i < 51; ++i) assert(bits[i] == 0);
    assert(em4x05_login_bits(0x01234567U, bits, sizeof(bits)) == 51);
    /* Independent byte-order/parity vector: 67,45,23,01; column XOR is zero. */
    expect_bits(bits, "000011" "111001101" "101000101" "110001001" "100000001" "00000000" "0");
    assert(em4x05_login_bits(0, bits, 50) == 0);
    assert(em4x05_login_bits(0, NULL, 64) == 0);
    puts("EM4x05 READ/LOGIN golden vectors and all readable addresses passed");
    return 0;
}
