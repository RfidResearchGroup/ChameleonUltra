#include "diphase.h"

#include <stdlib.h>
#include <string.h>

void diphase_reset(diphase *d) {
    d->boundary = true;
}

/*
 * Inverted diphase (biphase) decoder for edge-interval input.
 *
 * Diphase encoding:
 *   - Transition at every bit boundary (guaranteed).
 *   - Mid-bit transition present  -> decoded as 0 (inverted convention).
 *   - Mid-bit transition absent   -> decoded as 1 (inverted convention).
 *
 * The hardware detects rising edges and provides the interval (in carrier
 * cycles) between consecutive rising edges.  Intervals classify as:
 *   t=0 : 1T   (one full bit period,   ~64 cycles at RF/64)
 *   t=1 : 1.5T (one and a half periods, ~96 cycles)
 *   t=2 : 2T   (two full bit periods,  ~128 cycles)
 *   t=3 : invalid
 *
 * State machine (boundary = true when at a bit boundary):
 *
 *   boundary + 1T   -> bit 0,         stay at boundary
 *   boundary + 1.5T -> bits [1, 0],   move to mid-bit
 *   boundary + 2T   -> bits [1, 1],   stay at boundary
 *
 *   mid-bit  + 1T   -> bit 0,         stay at mid-bit
 *   mid-bit  + 1.5T -> bit 1,         move to boundary
 *   mid-bit  + 2T   -> invalid (reset)
 */
void diphase_feed(diphase *d, uint8_t interval, bool *bits, int8_t *bitlen) {
    uint8_t t = d->rp(interval);
    *bitlen = -1;
    if (t == 3) {
        diphase_reset(d);
        return;
    }

    if (d->boundary) {
        if (t == 0) {
            // 1T at boundary: complete bit with mid-bit transition -> 0
            *bitlen = 1;
            bits[0] = 0;
        } else if (t == 1) {
            // 1.5T at boundary: bit 1 (no mid-bit) + start of bit 0 (mid-bit)
            *bitlen = 2;
            bits[0] = 1;
            bits[1] = 0;
            d->boundary = false;
        } else if (t == 2) {
            // 2T at boundary: two bits without mid-bit transitions -> 1, 1
            *bitlen = 2;
            bits[0] = 1;
            bits[1] = 1;
        }
    } else {
        if (t == 0) {
            // 1T at mid-bit: next bit has mid-bit transition -> 0
            *bitlen = 1;
            bits[0] = 0;
        } else if (t == 1) {
            // 1.5T at mid-bit: next bit has no mid-bit transition -> 1
            *bitlen = 1;
            bits[0] = 1;
            d->boundary = true;
        } else {
            // 2T at mid-bit: impossible in clean diphase -> reset
            diphase_reset(d);
            return;
        }
    }
}
