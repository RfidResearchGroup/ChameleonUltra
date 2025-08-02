#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t (*period)(uint8_t interval);

typedef struct {
    bool sync;
    period rp;
} manchester;

extern void manchester_reset(manchester *m);
extern void manchester_feed(manchester *m, uint8_t interval, bool *bits, int8_t *bitlen);