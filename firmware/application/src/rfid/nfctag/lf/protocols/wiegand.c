#include "wiegand.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "parity.h"

#define PREAMBLE_26BIT (0x801)
#define PREAMBLE_27BIT (0x401)
#define PREAMBLE_28BIT (0x201)
#define PREAMBLE_29BIT (0x101)
#define PREAMBLE_30BIT (0x081)
#define PREAMBLE_31BIT (0x041)
#define PREAMBLE_32BIT (0x021)
#define PREAMBLE_33BIT (0x011)
#define PREAMBLE_34BIT (0x009)
#define PREAMBLE_35BIT (0x005)
#define PREAMBLE_36BIT (0x003)
#define PREAMBLE_ACTP  (0x095)

/**@brief Set a bit in the uint64 word.
 *
 * @param[in] W  Word whose bit is being set.
 * @param[in] B  Bit number in the word to be set.
 */
#define SET_BIT64(W, B) ((W) |= (uint64_t)(1ULL << (B)))

// if (!validate_card_limit(format_idx, card)) return false;

const uint8_t indasc27_fc_map[13] = {4, 14, 2, 10, 16, 18, 7, 19, 26, 21, 20, 22, 17};
const uint8_t indasc27_cn_map[14] = {3, 15, 5, 8, 24, 1, 13, 6, 9, 12, 11, 23, 25, 0};

const uint8_t tecom27_fc_map[11] = {24, 23, 12, 16, 20, 8, 4, 3, 2, 7, 11};
const uint8_t tecom27_cn_map[16] = {21, 22, 15, 18, 19, 1, 5, 9, 10, 6, 0, 17, 14, 13, 25, 26};

static wiegand_match_info_t g_wiegand_match_info = {0};

static void match_reset(uint64_t raw) {
    g_wiegand_match_info.valid = 1;
    g_wiegand_match_info.count = 0;
    g_wiegand_match_info.raw = raw;
    memset(g_wiegand_match_info.entries, 0, sizeof(g_wiegand_match_info.entries));
}

static void match_add(uint8_t format, bool has_parity, uint8_t fixed_mismatches, uint64_t repacked) {
    if (!g_wiegand_match_info.valid) {
        return;
    }
    if (g_wiegand_match_info.count >= WIEGAND_MATCH_MAX_FORMATS) {
        return;
    }
    wiegand_match_entry_t *entry = &g_wiegand_match_info.entries[g_wiegand_match_info.count++];
    entry->format = format;
    entry->has_parity = has_parity ? 1 : 0;
    entry->fixed_mismatches = fixed_mismatches;
    entry->repacked = repacked;
}

static uint64_t validation_mask(uint8_t length, card_format_t format) {
    if (length != 32) {
        return (1ULL << 38) - 1; // HID Prox payload size (preamble + Wiegand)
    }

    switch (format) {
        case HCP32:
            return ((1ULL << 24) - 1) << 7; // CN bits (30..7)
        case HPP32:
            return (1ULL << 31) - 1; // FC+CN bits (30..0)
        case B32:
            return ((1ULL << 30) - 1) << 1; // FC+CN bits (30..1)
        case KANTECH:
            return ((1ULL << 24) - 1) << 1; // FC+CN bits (24..1)
        case WIE32:
            return (1ULL << 28) - 1; // FC+CN bits (27..0)
        case KASTLE:
            return (1ULL << 32) - 1; // full payload (parity + fixed bit)
        default:
            return (1ULL << 38) - 1;
    }
}

wiegand_card_t *wiegand_card_alloc() {
    wiegand_card_t *card = (wiegand_card_t *)malloc(sizeof(wiegand_card_t));
    memset(card, 0, sizeof(wiegand_card_t));
    return card;
}

bool wiegand_get_match_info(wiegand_match_info_t *out) {
    if (!g_wiegand_match_info.valid || out == NULL) {
        return false;
    }
    *out = g_wiegand_match_info;
    return true;
}

static uint64_t get_nonlinear_fields(uint64_t n, const uint8_t *map, size_t size) {
    uint64_t bits = 0x0;
    for (int i = 0; (i < size) && (n > 0); i++) {
        if (n & 0x01) {
            bits |= 1ULL << map[i];
        }
        n >>= 1;
    }
    return bits;
}

static uint64_t pack_nonlinear(
    wiegand_card_t *card,
    const uint8_t *fc_map, size_t fc_map_size,
    const uint8_t *cn_map, size_t cn_map_size) {
    uint64_t bits = PREAMBLE_27BIT;
    bits <<= 27;
    bits |= get_nonlinear_fields(card->facility_code, fc_map, fc_map_size);
    bits |= get_nonlinear_fields(card->card_number, cn_map, cn_map_size);
    return bits;
}

static wiegand_card_t *unpack_nonlinear(
    uint64_t hi, uint64_t lo,
    const uint8_t *fc_map, size_t fc_map_size,
    const uint8_t *cn_map, size_t cn_map_size) {
    wiegand_card_t *d = wiegand_card_alloc();
    for (int i = fc_map_size - 1; i >= 0; i--) {
        d->facility_code <<= 1;
        if (IS_SET(lo, fc_map[i])) {
            d->facility_code |= 0x1;
        }
    }
    for (int i = cn_map_size - 1; i >= 0; i--) {
        d->card_number <<= 1;
        if (IS_SET(lo, cn_map[i])) {
            d->card_number |= 0x1;
        }
    }
    return d;
}

static uint64_t pack_h10301(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_26BIT;
    bits <<= 1;  // even parity bit
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;  // odd parity bit
    if (oddparity32((bits >> 1) & 0xfff)) {
        SET_BIT64(bits, 0);
    }
    if (evenparity32((bits >> 13) & 0xfff)) {
        SET_BIT64(bits, 25);
    }
    return bits;
}

static wiegand_card_t *unpack_h10301(uint64_t hi, uint64_t lo) {
    if (!((IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xfff)) &&
          (IS_SET(lo, 25) == evenparity32((lo >> 13) & 0xfff)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0xff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_ind26(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_26BIT;
    bits <<= 1;  // even parity bit

    bits = (bits << 12) | (card->facility_code & 0xfff);
    bits = (bits << 12) | (card->card_number & 0xfff);

    uint8_t odd_parity = oddparity32(bits & 0xfff);
    bits <<= 1;  // odd parity bit
    if (odd_parity) {
        bits |= 0x01;
    }
    uint8_t even_parity = evenparity32((bits >> 13) & 0xfff);
    if (even_parity) {
        bits |= 0x2000000;
    }
    return bits;
}

static wiegand_card_t *unpack_ind26(uint64_t hi, uint64_t lo) {
    uint32_t odd = (lo >> 1) & 0xfff;         // 32..43
    uint8_t odd_parity = lo & 0x01;           // 44
    uint32_t even = (lo >> 13) & 0xfff;       // 19..31
    uint8_t even_parity = (lo >> 25) & 0x01;  // 18
    if (!(oddparity32(odd) == odd_parity) && (evenparity32(even) == even_parity)) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 1) & 0xfff;
    d->facility_code = (lo >> 13) & 0xfff;
    return d;
}

static uint64_t pack_ind27(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_27BIT;
    bits = (bits << 13) | (card->facility_code & 0x1fff);
    bits = (bits << 14) | (card->card_number & 0x3fff);
    return bits;
}

static wiegand_card_t *unpack_ind27(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 14) & 0x1fff;
    d->card_number = (lo >> 0) & 0x3fff;
    return d;
}

static uint64_t pack_indasc27(wiegand_card_t *card) {
    return pack_nonlinear(card, indasc27_fc_map, sizeof(indasc27_fc_map), indasc27_cn_map, sizeof(indasc27_cn_map));
}

static wiegand_card_t *unpack_indasc27(uint64_t hi, uint64_t lo) {
    return unpack_nonlinear(hi, lo, indasc27_fc_map, sizeof(indasc27_fc_map), indasc27_cn_map, sizeof(indasc27_cn_map));
}

static uint64_t pack_tecom27(wiegand_card_t *card) {
    return pack_nonlinear(card, tecom27_fc_map, sizeof(tecom27_fc_map), tecom27_cn_map, sizeof(tecom27_cn_map));
}

static wiegand_card_t *unpack_tecom27(uint64_t hi, uint64_t lo) {
    return unpack_nonlinear(hi, lo, tecom27_fc_map, sizeof(tecom27_fc_map), tecom27_cn_map, sizeof(tecom27_cn_map));
}

static uint64_t pack_2804w(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_28BIT;
    bits <<= 4;
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 15) | (card->card_number & 0x7fff);
    bits <<= 1;  // parity bit
    if (oddparity32(bits & 0xDB6DB6)) {
        SET_BIT64(bits, 25);
    }
    if (evenparity32((bits >> 14) & 0x1fff)) {
        SET_BIT64(bits, 27);
    }
    if (oddparity32((bits >> 1) & 0x7ffffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_2804w(uint64_t hi, uint64_t lo) {
    if (!(((lo >> 27) & 0x1) == (evenparity32((lo >> 14) & 0x1fff)) &&
          (((lo >> 25) & 0x1) == (oddparity32(lo & 0xDB6DB6))) &&
          (((lo >> 0) & 0x1) == (oddparity32((lo >> 1) & 0x7ffffff))))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 16) & 0xff;
    d->card_number = (lo >> 1) & 0x7fff;
    return d;
}

static uint64_t pack_ind29(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_29BIT;
    bits = (bits << 13) | (card->facility_code & 0x1fff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    return bits;
}

static wiegand_card_t *unpack_ind29(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 0) & 0xffff;
    d->facility_code = (lo >> 16) & 0x1fff;
    return d;
}

static uint64_t pack_atsw30(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_30BIT;
    bits <<= 1;
    bits = (bits << 12) | (card->facility_code & 0xfff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 17) & 0xfff)) {
        SET_BIT64(bits, 29);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_atsw30(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 29) == evenparity32((lo >> 17) & 0xfff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0xfff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_adt31(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_31BIT;
    bits <<= 1;  // parity bit, unknown
    bits = (bits << 4) | (card->facility_code & 0xf);
    bits = (bits << 23) | (card->card_number & 0x7fffff);
    bits <<= 3;
    return bits;
}

static wiegand_card_t *unpack_adt31(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 26) & 0xf;
    d->card_number = (lo >> 3) & 0x7fffff;
    return d;
}

static uint64_t pack_hcp32(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits <<= 1;
    bits = (bits << 24) | (card->card_number & 0xffffff);
    bits <<= 7;
    return bits;
}

static wiegand_card_t *unpack_hcp32(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 7) & 0xffffff;
    return d;
}

static uint64_t pack_hpp32(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits <<= 1;
    bits = (bits << 12) | (card->facility_code & 0xfff);
    bits = (bits << 19) | (card->card_number & 0x7ffff);
    return bits;
}

static wiegand_card_t *unpack_hpp32(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 19) & 0xfff;
    d->card_number = (lo >> 0) & 0x7ffff;
    return d;
}

static uint64_t pack_b32(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits <<= 1;
    bits = (bits << 14) | (card->facility_code & 0x3fff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 15) & 0xffff)) {
        SET_BIT64(bits, 31);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_b32(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 31) == evenparity32((lo >> 15) & 0xffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0x3fff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_kastle(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits = (bits << 2) | 0x1;  // Always 1
    bits = (bits << 5) | (card->issue_level & 0x1f);
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 15) & 0xffff)) {
        SET_BIT64(bits, 31);  // even parity bit
    }
    if (oddparity32((bits >> 1) & 0x1ffff)) {
        SET_BIT64(bits, 0);  // odd parity bit
    }
    return bits;
}

static wiegand_card_t *unpack_kastle(uint64_t hi, uint64_t lo) {
    if (!IS_SET(lo, 30)) {  // Always 1 in this format
        return NULL;
    }
    if (!(IS_SET(lo, 31) == evenparity32((lo >> 15) & 0xffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x1ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->issue_level = (lo >> 25) & 0x1f;
    d->facility_code = (lo >> 17) & 0xff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_kantech(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits <<= 7;
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    return bits;
}

static wiegand_card_t *unpack_kantech(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0xff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_wie32(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_32BIT;
    bits <<= 4;
    bits = (bits << 12) | (card->facility_code & 0xfff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    return bits;
}

static wiegand_card_t *unpack_wie32(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 16) & 0xfff;
    d->card_number = (lo >> 0) & 0xffff;
    return d;
}

static uint64_t pack_d10202(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_33BIT;
    bits <<= 1;
    bits = (bits << 7) | (card->facility_code & 0x7f);
    bits = (bits << 24) | (card->card_number & 0xffffff);
    bits <<= 1;
    if (evenparity32((bits >> 16) & 0xffff)) {
        SET_BIT64(bits, 32);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_d10202(uint64_t hi, uint64_t lo) {
    if (!((IS_SET(lo, 32) == evenparity32((lo >> 16) & 0xffff)) &&
          (IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 25) & 0x7f;
    d->card_number = (lo >> 1) & 0xffffff;
    return d;
}

static uint64_t pack_h10306(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_34BIT;
    bits <<= 1;
    bits = (bits << 16) | (card->facility_code & 0xffff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 17) & 0xffff)) {
        SET_BIT64(bits, 33);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_h10306(uint64_t hi, uint64_t lo) {
    if (!((IS_SET(lo, 33) == evenparity32((lo >> 17) & 0xffff)) &&
          (IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0xffff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_n10002(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_34BIT;
    bits <<= 1;
    bits = (bits << 16) | (card->facility_code & 0xffff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 17) & 0xffff)) {
        SET_BIT64(bits, 33);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_n10002(uint64_t hi, uint64_t lo) {
    if (!((IS_SET(lo, 33) == evenparity32((lo >> 17) & 0xffff)) &&
          (IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0xffff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_optus(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_34BIT;
    bits <<= 1;
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 5;
    bits = (bits << 11) | (card->facility_code & 0x7ff);
    bits <<= 1;
    return bits;
}

static wiegand_card_t *unpack_optus(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 17) & 0xffff;
    d->facility_code = (lo >> 1) & 0x7ff;
    return d;
}

static uint64_t pack_smartpass(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_34BIT;
    bits <<= 1;
    bits = (bits << 13) | (card->facility_code & 0x1fff);
    bits = (bits << 3) | (card->issue_level & 0x7);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    return bits;
}

static wiegand_card_t *unpack_smartpass(uint64_t hi, uint64_t lo) {
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 20) & 0x1fff;
    d->issue_level = (lo >> 17) & 0x7;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_bqt34(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_34BIT;
    bits <<= 1;
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 24) | (card->card_number & 0xffffff);
    bits <<= 1;
    if (evenparity32((bits >> 17) & 0xffff)) {
        SET_BIT64(bits, 33);
    }
    if (oddparity32((bits >> 1) & 0xffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_bqt34(uint64_t hi, uint64_t lo) {
    if (!((IS_SET(lo, 33) == evenparity32((lo >> 17) & 0xffff)) &&
          (IS_SET(lo, 0) == oddparity32((lo >> 1) & 0xffff)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 25) & 0xff;
    d->card_number = (lo >> 1) & 0xffffff;
    return d;
}

static uint64_t pack_c1k35s(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_35BIT;
    bits <<= 2;
    bits = (bits << 12) | (card->facility_code & 0xfff);
    bits = (bits << 20) | (card->card_number & 0xfffff);
    bits <<= 1;  // parity bit
    if (evenparity32((bits >> 1) & 0xDB6DB6DB)) {
        SET_BIT64(bits, 33);
    }
    if (oddparity32(((bits >> 2) & 0xDB6DB6DB))) {
        SET_BIT64(bits, 0);
    }
    if (oddparity32(((bits >> 32) & 0x3) ^ (bits & 0xFFFFFFFF))) {
        SET_BIT64(bits, 34);
    }
    return bits;
}

static wiegand_card_t *unpack_c1k35s(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 33) == (evenparity32((lo >> 1) & 0xDB6DB6DB)) &&
          IS_SET(lo, 0) == (oddparity32((lo >> 2) & 0xDB6DB6DB)) &&
          IS_SET(lo, 34) == (oddparity32(((lo >> 32) & 0x3) ^ (lo & 0xFFFFFFFF))))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 1) & 0xfffff;
    d->facility_code = (lo >> 21) & 0xfff;
    return d;
}

static uint64_t pack_c15001(wiegand_card_t *card) {
    if (card->oem == 0) {
        card->oem = 900;
    }
    uint64_t bits = PREAMBLE_36BIT;
    bits <<= 1;
    bits = (bits << 10) | (card->oem & 0x3ff);
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 18) & 0x1ffff)) {
        SET_BIT64(bits, 35);
    }
    if (oddparity32((bits >> 1) & 0x1ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_c15001(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 35) == evenparity32((lo >> 18) & 0x1ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x1ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->oem = (lo >> 25) & 0x3ff;
    d->facility_code = (lo >> 17) & 0xff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_actprox(wiegand_card_t *card) {
    if (card->oem == 0) {
        card->oem = 900;
    }
    uint64_t bits = PREAMBLE_ACTP;
    bits <<= 1;
    bits = (bits << 10) | (card->oem & 0x3ff);
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (evenparity32((bits >> 18) & 0x1ffff)) {
        SET_BIT64(bits, 35);
    }
    if (oddparity32((bits >> 1) & 0x1ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_actprox(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 35) == evenparity32((lo >> 18) & 0x1ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x1ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->oem = (lo >> 25) & 0x3ff;
    d->facility_code = (lo >> 17) & 0xff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_s12906(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_36BIT;
    bits <<= 1;
    bits = (bits << 8) | (card->facility_code & 0xff);
    bits = (bits << 2) | (card->issue_level & 0x3);
    bits = (bits << 24) | (card->card_number & 0xffffff);
    bits <<= 1;
    if (oddparity32((bits >> 18) & 0x1ffff)) {
        SET_BIT64(bits, 35);
    }
    if (oddparity32((bits >> 1) & 0x3ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_s12906(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 35) == oddparity32((lo >> 18) & 0x1ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x3ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 27) & 0xff;
    d->issue_level = (lo >> 25) & 0x3;
    d->card_number = (lo >> 1) & 0xffffff;
    return d;
}

static uint64_t pack_sie36(wiegand_card_t *card) {
    uint64_t bits = PREAMBLE_36BIT;
    bits <<= 1;
    bits = (bits << 18) | (card->facility_code & 0x3ffff);
    bits = (bits << 16) | (card->card_number & 0xffff);
    bits <<= 1;
    if (oddparity32((bits & 0xB6DB6DB6) ^ ((bits >> 32) & 0x05))) {
        SET_BIT64(bits, 35);
    }
    if (evenparity32((bits & 0xDB6DB6DA) ^ ((bits >> 32) & 0x06))) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_sie36(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 35) == oddparity32((lo & 0xB6DB6DB6) ^ ((lo >> 32) & 0x05)) &&
          IS_SET(lo, 0) == evenparity32((lo & 0xDB6DB6DA) ^ ((lo >> 32) & 0x06)))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 17) & 0x3ffff;
    d->card_number = (lo >> 1) & 0xffff;
    return d;
}

static uint64_t pack_h10320(wiegand_card_t *card) {
    uint64_t bits = 0x01;  // first bit is ONE.
    // This card is BCD-encoded rather than binary. Set the 4-bit groups independently.
    uint64_t n = 10000000;
    for (uint32_t i = 0; i < 8; i++) {
        bits = (bits << 4) | (((uint64_t)(card->card_number / n) % 10) & 0xf);
        n /= 10;
    }
    bits <<= 4;
    if (evenparity32((bits >> 4) & 0x88888888)) {
        SET_BIT64(bits, 3);
    }
    if (oddparity32((bits >> 4) & 0x44444444)) {
        SET_BIT64(bits, 2);
    }
    if (evenparity32((bits >> 4) & 0x22222222)) {
        SET_BIT64(bits, 1);
    }
    if (evenparity32((bits >> 4) & 0x11111111)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_h10320(uint64_t hi, uint64_t lo) {
    if (IS_SET(lo, 36) != 1) {
        return NULL;
    }

    if (!((IS_SET(lo, 3) == evenparity32((lo >> 4) & 0x88888888)) &&
          (IS_SET(lo, 2) == oddparity32((lo >> 4) & 0x44444444)) &&
          (IS_SET(lo, 1) == evenparity32((lo >> 4) & 0x22222222)) &&
          (IS_SET(lo, 0) == evenparity32((lo >> 4) & 0x11111111)))) {
        return NULL;
    }

    // This card is BCD-encoded rather than binary. Get the 4-bit groups independently.
    uint64_t n = 1;
    uint64_t cn = 0;
    for (uint32_t i = 0; i < 8; i++) {
        lo >>= 4;
        uint64_t val = lo & 0xf;
        if (val > 9) {  // violation of BCD; Zero and exit.
            return NULL;
        }
        cn += val * n;
        n *= 10;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = cn;
    return d;
}

static uint64_t pack_h10302(wiegand_card_t *card) {
    uint64_t bits = 0x00;
    bits <<= 1;
    bits = (bits << 35) | (card->card_number & 0x7ffffffff);
    bits <<= 1;
    if (evenparity32((bits >> 18) & 0x3ffff)) {
        SET_BIT64(bits, 36);
    }
    if (oddparity32((bits >> 1) & 0x3ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_h10302(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 36) == evenparity32((lo >> 18) & 0x3ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x3ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 1) & 0x7ffffffff;
    return d;
}

static uint64_t pack_h10304(wiegand_card_t *card) {
    uint64_t bits = 0x00;
    bits <<= 1;
    bits = (bits << 16) | (card->facility_code & 0xffff);
    bits = (bits << 19) | (card->card_number & 0x7ffff);
    bits <<= 1;
    if (evenparity32((bits >> 18) & 0x3ffff)) {
        SET_BIT64(bits, 36);
    }
    if (oddparity32((bits >> 1) & 0x3ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_h10304(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 36) == evenparity32((lo >> 18) & 0x3ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x3ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 20) & 0xffff;
    d->card_number = (lo >> 1) & 0x7ffff;
    return d;
}

static uint64_t pack_p10004(wiegand_card_t *card) {
    // unknown parity scheme
    uint64_t bits = 0x00;
    bits <<= 1;
    bits = (bits << 13) | (card->facility_code & 0x1fff);
    bits = (bits << 18) | (card->card_number & 0x3ffff);
    bits <<= 5;
    return bits;
}

static wiegand_card_t *unpack_p10004(uint64_t hi, uint64_t lo) {
    // unknown parity scheme
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 23) & 0x1fff;
    d->card_number = (lo >> 5) & 0x3ffff;
    return d;
}

static uint64_t pack_hgeneric37(wiegand_card_t *card) {
    uint64_t bits = 0x00;
    bits <<= 4;
    bits = (bits << 32) | (card->card_number & 0xffffffff);
    bits = (bits << 1) | 0x1;  // Always 1
    // even1
    if (evenparity32((bits >> 4) & 0x11111111)) {
        SET_BIT64(bits, 36);
    }
    // odd1
    if (oddparity32(bits & 0x44444444)) {
        SET_BIT64(bits, 34);
    }
    // even2
    if (evenparity32(bits & 0x22222222)) {
        SET_BIT64(bits, 33);
    }
    return bits;
}

static wiegand_card_t *unpack_hgeneric37(uint64_t hi, uint64_t lo) {
    if (!IS_SET(lo, 0)) {  // Always 1 in this format
        return NULL;
    }
    if (!(IS_SET(lo, 36) == evenparity32((lo >> 4) & 0x11111111) &&
          IS_SET(lo, 34) == oddparity32(lo & 0x44444444) &&
          IS_SET(lo, 33) == evenparity32(lo & 0x22222222))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->card_number = (lo >> 1) & 0xffffffff;
    return d;
}

static uint64_t pack_mdi37(wiegand_card_t *card) {
    uint64_t bits = 0x00;
    bits <<= 3;
    bits = (bits << 4) | (card->facility_code & 0xf);
    bits = (bits << 29) | (card->card_number & 0x1fffffff);
    bits <<= 1;
    if (evenparity32((bits >> 18) & 0x3ffff)) {
        SET_BIT64(bits, 36);
    }
    if (oddparity32((bits >> 1) & 0x3ffff)) {
        SET_BIT64(bits, 0);
    }
    return bits;
}

static wiegand_card_t *unpack_mdi37(uint64_t hi, uint64_t lo) {
    if (!(IS_SET(lo, 36) == evenparity32((lo >> 18) & 0x3ffff) &&
          IS_SET(lo, 0) == oddparity32((lo >> 1) & 0x3ffff))) {
        return NULL;
    }
    wiegand_card_t *d = wiegand_card_alloc();
    d->facility_code = (lo >> 30) & 0xf;
    d->card_number = (lo >> 1) & 0x1fffffff;
    return d;
}

// ref:
// https://github.com/RfidResearchGroup/proxmark3/blob/master/client/src/wiegand_formats.c
// https://github.com/Proxmark/proxmark3/blob/master/client/hidcardformats.c
// https://acre.my.site.com/knowledgearticles/s/article/x107
// https://www.everythingid.com.au/hid-card-formats-i-15
static const card_format_table_t formats[] = {
    {H10301, pack_h10301, unpack_h10301, 26, {1, 0xFF, 0xFFFF, 0, 0}},           // HID H10301 26-bit
    {IND26, pack_ind26, unpack_ind26, 26, {1, 0xFFF, 0xFFF, 0, 0}},              // Indala 26-bit
    {IND27, pack_ind27, unpack_ind27, 27, {0, 0x1FFF, 0x3FFF, 0, 0}},            // Indala 27-bit
    {INDASC27, pack_indasc27, unpack_indasc27, 27, {0, 0x1FFF, 0x3FFF, 0, 0}},   // Indala ASC 27-bit
    {TECOM27, pack_tecom27, unpack_tecom27, 27, {0, 0x7FF, 0xFFFF, 0, 0}},       // Tecom 27-bit
    {W2804, pack_2804w, unpack_2804w, 28, {1, 0xFF, 0x7FFF, 0, 0}},              // 2804 Wiegand 28-bit
    {IND29, pack_ind29, unpack_ind29, 29, {0, 0x1FFF, 0xFFFF, 0, 0}},            // Indala 29-bit
    {ATSW30, pack_atsw30, unpack_atsw30, 30, {1, 0xFFF, 0xFFFF, 0, 0}},          // ATS Wiegand 30-bit
    {ADT31, pack_adt31, unpack_adt31, 31, {0, 0xF, 0x7FFFFF, 0, 0}},             // HID ADT 31-bit
    {HCP32, pack_hcp32, unpack_hcp32, 32, {0, 0, 0x3FFF, 0, 0}},                 // HID Check Point 32-bit
    {HPP32, pack_hpp32, unpack_hpp32, 32, {0, 0xFFF, 0x7FFFF, 0, 0}},            // HID Hewlett-Packard 32-bit
    {B32, pack_b32, unpack_b32, 32, {1, 0x3FFF, 0xFFFF, 0, 0}},                  // 32-B 32-bit
    {KASTLE, pack_kastle, unpack_kastle, 32, {1, 0xFF, 0xFFFF, 0x1F, 0}},        // Kastle 32-bit
    {KANTECH, pack_kantech, unpack_kantech, 32, {0, 0xFF, 0xFFFF, 0, 0}},        // Indala/Kantech KFS 32-bit
    {WIE32, pack_wie32, unpack_wie32, 32, {0, 0xFFF, 0xFFFF, 0, 0}},             // Wiegand 32-bit
    {D10202, pack_d10202, unpack_d10202, 33, {1, 0x7F, 0xFFFFFF, 0, 0}},         // HID D10202 33-bit
    {H10306, pack_h10306, unpack_h10306, 34, {1, 0xFFFF, 0xFFFF, 0, 0}},         // HID H10306 34-bit
    {N10002, pack_n10002, unpack_n10002, 34, {1, 0xFFFF, 0xFFFF, 0, 0}},         // Honeywell/Northern N10002 34-bit
    {OPTUS34, pack_optus, unpack_optus, 34, {0, 0x3FF, 0xFFFF, 0, 0}},           // Indala Optus 34-bit
    {SMP34, pack_smartpass, unpack_smartpass, 34, {0, 0x3FF, 0xFFFF, 0x7, 0}},   // Cardkey Smartpass 34-bit
    {BQT34, pack_bqt34, unpack_bqt34, 34, {1, 0xFF, 0xFFFFFF, 0, 0}},            // BQT 34-bit
    {C1K35S, pack_c1k35s, unpack_c1k35s, 35, {1, 0xFFF, 0xFFFFF, 0, 0}},         // HID Corporate 1000 35-bit Std
    {C15001, pack_c15001, unpack_c15001, 36, {1, 0xFF, 0xFFFF, 0, 0x3FF}},       // HID KeyScan 36-bit
    {S12906, pack_s12906, unpack_s12906, 36, {1, 0xFF, 0xFFFFFF, 0x3, 0}},       // HID Simplex 36-bit
    {SIE36, pack_sie36, unpack_sie36, 36, {1, 0x3FFFF, 0xFFFF, 0, 0}},           // HID 36-bit Siemens
    {H10320, pack_h10320, unpack_h10320, 37, {1, 0, 99999999, 0, 0}},            // HID H10320 37-bit BCD
    {H10302, pack_h10302, unpack_h10302, 37, {1, 0, 0x7FFFFFFFF, 0, 0}},         // HID H10302 37-bit huge ID
    {H10304, pack_h10304, unpack_h10304, 37, {1, 0xFFFF, 0x7FFFF, 0, 0}},        // HID H10304 37-bit
    {P10004, pack_p10004, unpack_p10004, 37, {0, 0x1FFF, 0x3FFFF, 0, 0}},        // HID P10004 37-bit PCSC
    {HGEN37, pack_hgeneric37, unpack_hgeneric37, 37, {1, 0, 0xFFFFFFFF, 0, 0}},  // HID Generic 37-bit
    {MDI37, pack_mdi37, unpack_mdi37, 37, {1, 0xF, 0x1FFFFFFF, 0, 0}},           // PointGuard MDI 37-bit
    {ACTPHID, pack_actprox, unpack_actprox, 36, {1, 0xFF, 0xFFFF, 0x3FF, 0}},   // HID ACTProx 36-bit
};

uint64_t pack(wiegand_card_t *card) {
    for (int i = 0; i < ARRAY_SIZE(formats); i++) {
        if (card->format != formats[i].format) {
            continue;
        }
        if (formats[i].pack == NULL) {
            continue;
        }
        return formats[i].pack(card);
    }
    return 0;
}

wiegand_card_t *unpack(uint8_t format_hint, uint8_t length, uint64_t hi, uint64_t lo) {
    const bool use_scoring = (length == 32 && format_hint == 0);
    if (use_scoring) {
        match_reset(lo);
    } else {
        g_wiegand_match_info.valid = 0;
    }
    wiegand_card_t *best_card = NULL;
    uint8_t best_mismatches = 0xFF;
    for (int i = 0; i < ARRAY_SIZE(formats); i++) {
        if (format_hint != 0 && format_hint != formats[i].format) {
            continue;
        }
        if (length != formats[i].bits) {
            continue;
        }
        if (formats[i].unpack == NULL) {
            continue;
        }
        wiegand_card_t *card = formats[i].unpack(hi, lo);
        if (card == NULL) {
            continue;
        }
        card->format = formats[i].format;
        if (!use_scoring) {
            return card;
        }
        if (formats[i].pack == NULL) {
            free(card);
            continue;
        }
        uint64_t repacked = formats[i].pack(card);
        if (repacked == 0) {
            free(card);
            continue;
        }
        uint64_t mask = validation_mask(length, formats[i].format);
        bool passed = ((repacked & mask) == (lo & mask));
        if (!passed) {
            free(card);
            continue;
        }
        uint64_t payload_mask = (1ULL << 38) - 1;
        uint64_t fixed_mask = payload_mask & ~mask;
        uint64_t fixed_diff = (repacked ^ lo) & fixed_mask;
        uint8_t mismatches = (uint8_t)__builtin_popcountll(fixed_diff);
        match_add(formats[i].format, formats[i].fields.has_parity, mismatches, repacked);
        if (mismatches < best_mismatches) {
            if (best_card != NULL) {
                free(best_card);
            }
            best_card = card;
            best_mismatches = mismatches;
            continue;
        }
        free(card);
        continue;
    }
    if (best_card != NULL) {
        return best_card;
    }
    return NULL;
}
