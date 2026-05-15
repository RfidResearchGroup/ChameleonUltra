#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSK_BITRATE_HID   (50)
#define FSK_BITRATE_IOPROX (64)
#define FSK_MAX_BITRATE   (128)

typedef struct {
    uint8_t bitrate; // 50 or 64
    uint8_t c;
    uint16_t samples[FSK_MAX_BITRATE];
    float goertzel_fc_8;
    float goertzel_fc_10;
} fsk_t;

extern bool fsk_feed(fsk_t *m, uint16_t sample, bool *bit);
extern fsk_t *fsk_alloc(uint8_t bitrate);
extern void fsk_free(fsk_t *m);

#ifdef __cplusplus
}
#endif
