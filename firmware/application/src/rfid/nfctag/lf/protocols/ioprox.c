/*
 * ioprox.c - STUB for local building only. DO NOT COMMIT.
 * Provides the protocol object only - ioprox_read is in lf_ioprox_data.c
 */
#include "ioprox.h"
#include <stdlib.h>
#include <string.h>

#define IOPROX_DATA_SIZE (8)

typedef struct {
    uint8_t data[IOPROX_DATA_SIZE];
} ioprox_codec;

static void *ioprox_alloc(void) {
    ioprox_codec *c = malloc(sizeof(ioprox_codec));
    memset(c, 0, sizeof(*c));
    return c;
}
static void ioprox_free(void *c) { free(c); }
static uint8_t *ioprox_get_data(void *c) { return ((ioprox_codec *)c)->data; }
static void ioprox_decoder_start(void *c, uint8_t fmt) { (void)c; (void)fmt; }
static bool ioprox_decoder_feed(void *c, uint16_t val) { (void)c; (void)val; return false; }
static nrf_pwm_sequence_t *ioprox_modulator(void *c, uint8_t *buf) { (void)c; (void)buf; return NULL; }

const protocol ioprox = {
    .tag_type  = TAG_TYPE_HID_PROX + 1,
    .data_size = IOPROX_DATA_SIZE,
    .alloc     = ioprox_alloc,
    .free      = ioprox_free,
    .get_data  = ioprox_get_data,
    .decoder   = { .start = ioprox_decoder_start, .feed = ioprox_decoder_feed },
    .modulator = ioprox_modulator,
};
