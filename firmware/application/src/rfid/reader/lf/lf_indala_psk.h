#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Indala-64 PSK1 demodulator for the reader's own captures. Integer only, no
 * nRF dependencies, so it can also be compiled and tested on a host.
 *
 * Indala is PSK1 at RF/32 with an fc/2 subcarrier. In reader mode the ADC is
 * triggered once per carrier period, and a tag derives its subcarrier by
 * dividing that same carrier, so the subcarrier lands exactly on fs/2 at a
 * fixed phase. That makes the demodulator simple:
 *
 *   samples (one per carrier period, at a sample phase outside the dead band,
 *            see lf_indala_data.c)
 *     -> subtract the mean, multiply by (-1)^n   fc/2 moves to DC; DC and slow
 *                                                envelope drift move to fs/2
 *     -> [1,2,1] filter                          a double zero at fs/2 removes them
 *     -> sum each 32-sample bit                  matched filter for RF/32
 *     -> bit = sign of the sum                   PSK1: the phase is the data
 *     -> exact search for the 33-bit preamble, normal and inverted, at each of
 *        the 32 possible bit offsets
 *
 * Only one quadrature is observable, which is why the sample phase matters.
 */

/** Samples per bit: RF/32, one sample per carrier period. */
#define INDALA_PSK_BIT_SAMPLES   32

/** Bits in an Indala-64 frame. */
#define INDALA_PSK_FRAME_BITS    64

/** Bits in the fixed preamble: 1010, 28 zeros, then 1. */
#define INDALA_PSK_PREAMBLE_BITS 33

/** Samples per capture: 128 bits, two frames, about 33ms. */
#define INDALA_PSK_CAPTURE_SAMPLES 4096

#define INDALA_PSK_MAX_BITS (INDALA_PSK_CAPTURE_SAMPLES / INDALA_PSK_BIT_SAMPLES)

/** Shortest capture that can hold a whole frame plus slack. */
#define INDALA_PSK_MIN_SAMPLES (INDALA_PSK_BIT_SAMPLES * (INDALA_PSK_FRAME_BITS + 4))

typedef struct {
    uint8_t id[INDALA_PSK_FRAME_BITS / 8];  // the frame, first bit on air is the MSB of id[0]
    uint8_t offset;                         // winning sample offset within the bit, 0..31
    bool inverted;                          // the frame was found with inverted polarity
    int32_t amp;                            // mean |bit integrator| over the frame
    int32_t min_amp;                        // smallest |bit integrator| in the frame
    int32_t energy;                         // set even when no frame decodes: the largest
                                            // mean |bit integrator| over the whole capture
} indala_psk_result_t;

/**
 * Demodulate one Indala-64 frame.
 *
 * `samples` is modified in place (converted to baseband), so the capture buffer
 * doesn't have to be copied.
 *
 * @param samples  14-bit ADC conversions, one per carrier period
 * @param n        sample count, at least INDALA_PSK_MIN_SAMPLES
 * @param out      filled in on success; `out->energy` is filled in whenever the
 *                 capture was long enough to decode
 * @return         true if a frame was recovered
 */
bool indala_psk_decode(int16_t *samples, size_t n, indala_psk_result_t *out);
