//-----------------------------------------------------------------------------
// Copyright (C) 2008-2014 bla <blapost@gmail.com>
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
#include "crapto1.h"

#include <stdlib.h>
#include "parity.h"

/** update_contribution
 * helper, calculates the partial linear feedback contributions and puts in MSB
 */
static inline void update_contribution(uint32_t *item, const uint32_t mask1, const uint32_t mask2) {
    uint32_t p = *item >> 25;

    p = p << 1 | (evenparity32(*item & mask1));
    p = p << 1 | (evenparity32(*item & mask2));
    *item = p << 24 | (*item & 0xffffff);
}

/** extend_table
 * using a bit of the keystream extend the table of possible lfsr states
 */
static inline void extend_table(uint32_t *tbl, uint32_t **end, int bit, int m1, int m2, uint32_t in) {
    in <<= 24;
    for (*tbl <<= 1; tbl <= *end; *++tbl <<= 1)
        if (filter(*tbl) ^ filter(*tbl | 1)) {
            *tbl |= filter(*tbl) ^ bit;
            update_contribution(tbl, m1, m2);
            *tbl ^= in;
        } else if (filter(*tbl) == bit) {
            *++*end = tbl[1];
            tbl[1] = tbl[0] | 1;
            update_contribution(tbl, m1, m2);
            *tbl++ ^= in;
            update_contribution(tbl, m1, m2);
            *tbl ^= in;
        } else
            *tbl-- = *(*end)--;
}
/** extend_table_simple
 * using a bit of the keystream extend the table of possible lfsr states
 */
static inline void extend_table_simple(uint32_t *tbl, uint32_t **end, int bit) {
    for (*tbl <<= 1; tbl <= *end; *++tbl <<= 1) {
        if (filter(*tbl) ^ filter(*tbl | 1)) { // replace
            *tbl |= filter(*tbl) ^ bit;
        } else if (filter(*tbl) == bit) {     // insert
            *++*end = *++tbl;
            *tbl = tbl[-1] | 1;
        } else {                              // drop
            *tbl-- = *(*end)--;
        }
    }
}


/** lfsr_rollback_bit
 * Rollback the shift register in order to get previous states
 */
uint8_t lfsr_rollback_bit(struct Crypto1State *s, uint32_t in, int fb) {
    int out;
    uint8_t ret;
    uint32_t t;

    s->odd &= 0xffffff;
    t = s->odd, s->odd = s->even, s->even = t;

    out = s->even & 1;
    out ^= LF_POLY_EVEN & (s->even >>= 1);
    out ^= LF_POLY_ODD & s->odd;
    out ^= !!in;
    out ^= (ret = filter(s->odd)) & (!!fb);

    s->even |= (evenparity32(out)) << 23;
    return ret;
}
/** lfsr_rollback_byte
 * Rollback the shift register in order to get previous states
 */
uint8_t lfsr_rollback_byte(struct Crypto1State *s, uint32_t in, int fb) {
    uint8_t ret = 0;
    ret |= lfsr_rollback_bit(s, BIT(in, 7), fb) << 7;
    ret |= lfsr_rollback_bit(s, BIT(in, 6), fb) << 6;
    ret |= lfsr_rollback_bit(s, BIT(in, 5), fb) << 5;
    ret |= lfsr_rollback_bit(s, BIT(in, 4), fb) << 4;
    ret |= lfsr_rollback_bit(s, BIT(in, 3), fb) << 3;
    ret |= lfsr_rollback_bit(s, BIT(in, 2), fb) << 2;
    ret |= lfsr_rollback_bit(s, BIT(in, 1), fb) << 1;
    ret |= lfsr_rollback_bit(s, BIT(in, 0), fb) << 0;
    return ret;
}
/** lfsr_rollback_word
 * Rollback the shift register in order to get previous states
 */
uint32_t lfsr_rollback_word(struct Crypto1State *s, uint32_t in, int fb) {

    uint32_t ret = 0;
    // note: xor args have been swapped because some compilers emit a warning
    // for 10^x and 2^x as possible misuses for exponentiation. No comment.
    ret |= lfsr_rollback_bit(s, BEBIT(in, 31), fb) << (24 ^ 31);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 30), fb) << (24 ^ 30);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 29), fb) << (24 ^ 29);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 28), fb) << (24 ^ 28);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 27), fb) << (24 ^ 27);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 26), fb) << (24 ^ 26);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 25), fb) << (24 ^ 25);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 24), fb) << (24 ^ 24);

    ret |= lfsr_rollback_bit(s, BEBIT(in, 23), fb) << (24 ^ 23);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 22), fb) << (24 ^ 22);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 21), fb) << (24 ^ 21);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 20), fb) << (24 ^ 20);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 19), fb) << (24 ^ 19);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 18), fb) << (24 ^ 18);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 17), fb) << (24 ^ 17);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 16), fb) << (24 ^ 16);

    ret |= lfsr_rollback_bit(s, BEBIT(in, 15), fb) << (24 ^ 15);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 14), fb) << (24 ^ 14);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 13), fb) << (24 ^ 13);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 12), fb) << (24 ^ 12);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 11), fb) << (24 ^ 11);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 10), fb) << (24 ^ 10);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 9), fb) << (24 ^ 9);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 8), fb) << (24 ^ 8);

    ret |= lfsr_rollback_bit(s, BEBIT(in, 7), fb) << (24 ^ 7);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 6), fb) << (24 ^ 6);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 5), fb) << (24 ^ 5);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 4), fb) << (24 ^ 4);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 3), fb) << (24 ^ 3);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 2), fb) << (24 ^ 2);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 1), fb) << (24 ^ 1);
    ret |= lfsr_rollback_bit(s, BEBIT(in, 0), fb) << (24 ^ 0);
    return ret;
}

/** nonce_distance
 * x,y valid tag nonces, then prng_successor(x, nonce_distance(x, y)) = y
 */
static uint16_t *dist = 0;
int nonce_distance(uint32_t from, uint32_t to) {
    if (!dist) {
        // allocation 2bytes * 0xFFFF times.
        dist = calloc(2 << 16,  sizeof(uint8_t));
        if (!dist)
            return -1;
        uint16_t x = 1;
        for (uint16_t i = 1; i; ++i) {
            dist[(x & 0xff) << 8 | x >> 8] = i;
            x = x >> 1 | (x ^ x >> 2 ^ x >> 3 ^ x >> 5) << 15;
        }
    }
    return (65535 + dist[to >> 16] - dist[from >> 16]) % 65535;
}

/** validate_prng_nonce
 * Determine if nonce is deterministic. ie: Suspectable to Darkside attack.
 * returns
 *   true = weak prng
 *   false = hardend prng
 */
bool validate_prng_nonce(uint32_t nonce) {
    // init prng table:
    if (nonce_distance(nonce, nonce) == -1)
        return false;
    return ((65535 - dist[nonce >> 16] + dist[nonce & 0xffff]) % 65535) == 16;
}

static uint32_t fastfwd[2][8] = {
        { 0, 0x4BC53, 0xECB1, 0x450E2, 0x25E29, 0x6E27A, 0x2B298, 0x60ECB},
        { 0, 0x1D962, 0x4BC53, 0x56531, 0xECB1, 0x135D3, 0x450E2, 0x58980}
};

/** lfsr_prefix_ks
 *
 * Is an exported helper function from the common prefix attack
 * Described in the "dark side" paper. It returns an -1 terminated array
 * of possible partial(21 bit) secret state.
 * The required keystream(ks) needs to contain the keystream that was used to
 * encrypt the NACK which is observed when varying only the 3 last bits of Nr
 * only correct iff [NR_3] ^ NR_3 does not depend on Nr_3
 */
uint32_t *lfsr_prefix_ks(const uint8_t ks[8], int isodd) {
    uint32_t *candidates = calloc(4 << 10, sizeof(uint8_t));
    if (!candidates) return 0;

    int size = 0;

    for (int i = 0; i < 1 << 21; ++i) {
        int good = 1;
        for (uint32_t c = 0; good && c < 8; ++c) {
            uint32_t entry = i ^ fastfwd[isodd][c];
            good &= (BIT(ks[c], isodd) == filter(entry >> 1));
            good &= (BIT(ks[c], isodd + 2) == filter(entry));
        }
        if (good)
            candidates[size++] = i;
    }

    candidates[size] = -1;

    return candidates;
}

/** check_pfx_parity
 * helper function which eliminates possible secret states using parity bits
 */
static struct Crypto1State *check_pfx_parity(uint32_t prefix, uint32_t rresp, uint8_t parities[8][8], uint32_t odd, uint32_t even, struct Crypto1State *sl, uint32_t no_par) {
    uint32_t good = 1;

    for (uint32_t c = 0; good && c < 8; ++c) {
        sl->odd = odd ^ fastfwd[1][c];
        sl->even = even ^ fastfwd[0][c];

        lfsr_rollback_bit(sl, 0, 0);
        lfsr_rollback_bit(sl, 0, 0);

        uint32_t ks3 = lfsr_rollback_bit(sl, 0, 0);
        uint32_t ks2 = lfsr_rollback_word(sl, 0, 0);
        uint32_t ks1 = lfsr_rollback_word(sl, prefix | c << 5, 1);

        if (no_par)
            break;

        uint32_t nr = ks1 ^ (prefix | c << 5);
        uint32_t rr = ks2 ^ rresp;

        good &= evenparity32(nr & 0x000000ff) ^ parities[c][3] ^ BIT(ks2, 24);
        good &= evenparity32(rr & 0xff000000) ^ parities[c][4] ^ BIT(ks2, 16);
        good &= evenparity32(rr & 0x00ff0000) ^ parities[c][5] ^ BIT(ks2,  8);
        good &= evenparity32(rr & 0x0000ff00) ^ parities[c][6] ^ BIT(ks2,  0);
        good &= evenparity32(rr & 0x000000ff) ^ parities[c][7] ^ ks3;
    }

    return sl + good;
}

#if !defined(__arm__) || defined(__linux__) || defined(_WIN32) || defined(__APPLE__) // bare metal ARM Proxmark lacks malloc()/free()
/** lfsr_common_prefix
 * Implementation of the common prefix attack.
 * Requires the 28 bit constant prefix used as reader nonce (pfx)
 * The reader response used (rr)
 * The keystream used to encrypt the observed NACK's (ks)
 * The parity bits (par)
 * It returns a zero terminated list of possible cipher states after the
 * tag nonce was fed in
 */

struct Crypto1State *lfsr_common_prefix(uint32_t pfx, uint32_t rr, uint8_t ks[8], uint8_t par[8][8], uint32_t no_par) {
    struct Crypto1State *statelist, *s;
    uint32_t *odd, *even, *o, *e, top;

    odd = lfsr_prefix_ks(ks, 1);
    even = lfsr_prefix_ks(ks, 0);

    s = statelist = calloc(1, (sizeof * statelist) << 24); // was << 20. Need more for no_par special attack. Enough???
    if (!s || !odd || !even) {
        free(statelist);
        statelist = 0;
        goto out;
    }

    for (o = odd; *o + 1; ++o)
        for (e = even; *e + 1; ++e)
            for (top = 0; top < 64; ++top) {
                *o += 1 << 21;
                *e += (!(top & 7) + 1) << 21;
                s = check_pfx_parity(pfx, rr, par, *o, *e, s, no_par);
            }

    s->odd = s->even = 0;
    out:
    free(odd);
    free(even);
    return statelist;
}
#endif
