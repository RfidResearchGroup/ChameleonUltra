#ifndef EM4X05_FORWARD_H
#define EM4X05_FORWARD_H

#include <stddef.h>
#include <stdint.h>

#define EM4X05_READ_BITS 13U
#define EM4X05_LOGIN_BITS 51U

/* EM4205/4305 DS v6, command structure: start gap/zero, code with even parity. */
static inline void em4x05_forward_prefix(uint8_t *bits, uint8_t code) {
    bits[0] = 0U; /* represented by the initial field stop */
    bits[1] = 0U;
    for (unsigned int i = 0U; i < 4U; ++i) bits[2U + i] = (code >> i) & 1U;
}

static inline size_t em4x05_read_bits(uint8_t address, uint8_t *bits, size_t capacity) {
    /* Word 2 is write-only. There is no WRITE/PROTECT/DISABLE entrypoint. */
    if (address >= 16U || address == 2U || bits == NULL || capacity < EM4X05_READ_BITS) return 0U;
    em4x05_forward_prefix(bits, 0x09U); /* transmitted 1001 */
    uint8_t parity = 0U;
    for (unsigned int i = 0U; i < 6U; ++i) {
        bits[6U + i] = (address >> i) & 1U;
        parity ^= bits[6U + i];
    }
    bits[12] = parity;
    return EM4X05_READ_BITS;
}

static inline size_t em4x05_login_bits(uint32_t password, uint8_t *bits, size_t capacity) {
    if (bits == NULL || capacity < EM4X05_LOGIN_BITS) return 0U;
    em4x05_forward_prefix(bits, 0x0CU); /* transmitted 0011 */
    uint8_t columns = 0U;
    size_t offset = 6U;
    for (unsigned int row = 0U; row < 4U; ++row) {
        uint8_t parity = 0U;
        for (unsigned int col = 0U; col < 8U; ++col) {
            uint8_t bit = (password >> (row * 8U + col)) & 1U;
            bits[offset++] = bit;
            parity ^= bit;
            columns ^= bit << col;
        }
        bits[offset++] = parity;
    }
    for (unsigned int col = 0U; col < 8U; ++col) bits[offset++] = (columns >> col) & 1U;
    bits[offset++] = 0U;
    return offset;
}

#endif
