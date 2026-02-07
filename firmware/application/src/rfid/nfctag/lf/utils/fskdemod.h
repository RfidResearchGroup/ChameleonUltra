#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BITRATE (50)

typedef struct {
    uint8_t c;
    uint16_t samples[BITRATE * 2];
    float goertzel_fc_8;
    float goertzel_fc_10;
} fsk_t;

extern bool fsk_feed(fsk_t *m, uint16_t sample, bool *bit);
extern fsk_t *fsk_alloc(void);
extern void fsk_free(fsk_t *m);

#ifdef __cplusplus
}
#endif
