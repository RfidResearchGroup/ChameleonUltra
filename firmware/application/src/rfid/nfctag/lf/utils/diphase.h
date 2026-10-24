#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t (*period)(uint8_t interval);

typedef struct {
    bool boundary;  // true: at bit boundary, false: at mid-bit
    period rp;
} diphase;

extern void diphase_reset(diphase *d);
extern void diphase_feed(diphase *d, uint8_t interval, bool *bits, int8_t *bitlen);
