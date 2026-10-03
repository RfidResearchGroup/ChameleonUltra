#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSK_BITRATE_32 (32)

// Higher-rate PSK: ~166.67kHz SAADC (TIMER3-triggered), RF/32
// Bit period = 32 carrier cycles × 8µs = 256µs → 256/6 = 42.67 samples/bit
// Use 43 as the nominal value; actual spacing uses fixed-point (128/3).
#define PSK166_BITRATE_43 (43)

// PSK sample buffer for batch correlation decoding.
// Protocol codecs buffer samples via psk_feed_sample(), then run their
// own correlation + preamble check when enough samples are collected.
typedef struct {
    uint8_t bitrate;
    uint16_t buf_size;          // total buffer capacity (samples)
    uint16_t *samples;          // sample buffer (heap-allocated)
    uint16_t sample_count;      // samples collected so far
    uint16_t phase_offset;      // tracks total shifted samples for DFT phase coherence
} psk_t;

// Feed one ADC sample into the buffer.
extern void psk_feed_sample(psk_t *m, uint16_t sample);

// Shift the sample buffer left by `shift` samples (discard oldest).
extern void psk_shift(psk_t *m, uint16_t shift);

// Legacy: even/odd correlation at 125kHz (1 sample/carrier-cycle).
extern int32_t psk_correlate(psk_t *m, uint16_t start);

// DFT-based fc/2 correlation at 166.67kHz.
// Uses cos(2π·3n/8) lookup — exact match for 62.5kHz at 166.67kHz sampling.
// `len` = number of samples to correlate (42 or 43 for RF/32).
extern int32_t psk166_correlate(psk_t *m, uint16_t start, uint8_t len);

// Phase-independent fc/2 correlation (IQ — picks max-magnitude axis).
// Robust against arbitrary carrier phase relative to DFT reference.
extern int32_t psk166_correlate_iq(psk_t *m, uint16_t start, uint8_t len);

extern psk_t *psk_alloc(uint8_t bitrate, uint16_t buf_samples);
extern void psk_free(psk_t *m);

// Shared static sample buffer for PSK reader codecs (Indala, Keri, etc.).
// Only one PSK reader runs at a time, so sharing is safe.
#define PSK_SHARED_BUF_SIZE (6144)
extern uint16_t psk_shared_samples[PSK_SHARED_BUF_SIZE];

#ifdef __cplusplus
}
#endif
