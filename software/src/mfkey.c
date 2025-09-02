//-----------------------------------------------------------------------------
// Merlok - June 2011
// Roel - Dec 2009
// Unknown author
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// MIFARE Darkside hack
//-----------------------------------------------------------------------------
#include "mfkey.h"

#include "crapto1.h"

// MIFARE
extern int compare_uint64(const void *a, const void *b);
int inline compare_uint64(const void *a, const void *b)
{
    if (*(uint64_t *)b == *(uint64_t *)a) return 0;
    if (*(uint64_t *)b < *(uint64_t *)a) return 1;
    return -1;
}

// create the intersection (common members) of two sorted lists. Lists are terminated by -1. Result will be in list1.
// Number of elements is returned.
uint32_t intersection(uint64_t *listA, uint64_t *listB)
{
    if (listA == NULL || listB == NULL) return 0;

    uint64_t *p1, *p2, *p3;
    p1 = p3 = listA;
    p2 = listB;

    while (*p1 != UINT64_C(-1) && *p2 != UINT64_C(-1)) {
        if (compare_uint64(p1, p2) == 0) {
            *p3++ = *p1++;
            p2++;
        }
        else {
            while (compare_uint64(p1, p2) < 0) ++p1;
            while (compare_uint64(p1, p2) > 0) ++p2;
        }
    }
    *p3 = UINT64_C(-1);
    return p3 - listA;
}

// Darkside attack (hf mf mifare)
// if successful it will return a list of keys, not just one.
uint32_t nonce2key(uint32_t uid, uint32_t nt, uint32_t nr, uint32_t ar, uint64_t par_info, uint64_t ks_info,
                   uint64_t **keys)
{
    union {
        struct Crypto1State *states;
        uint64_t *keylist;
    } unionstate;

    uint32_t i, pos;
    uint8_t ks3x[8], par[8][8];
    uint64_t key_recovered;

    // Reset the last three significant bits of the reader nonce
    nr &= 0xFFFFFF1F;

    for (pos = 0; pos < 8; pos++) {
        ks3x[7 - pos] = (ks_info >> (pos * 8)) & 0x0F;
        uint8_t bt = (par_info >> (pos * 8)) & 0xFF;

        par[7 - pos][0] = (bt >> 0) & 1;
        par[7 - pos][1] = (bt >> 1) & 1;
        par[7 - pos][2] = (bt >> 2) & 1;
        par[7 - pos][3] = (bt >> 3) & 1;
        par[7 - pos][4] = (bt >> 4) & 1;
        par[7 - pos][5] = (bt >> 5) & 1;
        par[7 - pos][6] = (bt >> 6) & 1;
        par[7 - pos][7] = (bt >> 7) & 1;
    }

    unionstate.states = lfsr_common_prefix(nr, ar, ks3x, par, (par_info == 0));

    if (!unionstate.states) {
        *keys = NULL;
        return 0;
    }

    for (i = 0; unionstate.keylist[i]; i++) {
        lfsr_rollback_word(unionstate.states + i, uid ^ nt, 0);
        crypto1_get_lfsr(unionstate.states + i, &key_recovered);
        unionstate.keylist[i] = key_recovered;
    }
    unionstate.keylist[i] = -1;

    *keys = unionstate.keylist;
    return i;
}
