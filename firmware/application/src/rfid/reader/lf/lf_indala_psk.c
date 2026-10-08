#include "lf_indala_psk.h"

#include <limits.h>
#include <string.h>

/* The preamble is the same for every Indala-64 tag (Proxmark3 cmdlfindala.c). */
static const uint8_t INDALA_PREAMBLE[INDALA_PSK_PREAMBLE_BITS] = {
    1, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1
};

/*
 * IDTECK uses the same air layer (PSK1, RF/32, fc/2). A loud IDTECK frame can
 * contain a run that matches Indala's mostly-zero preamble, which produced a
 * stable false Indala credential in testing. A capture that also carries the
 * IDTECK preamble ("IDTK") is therefore rejected.
 */
#define IDTECK_PREAMBLE_BITS 32
static const uint8_t IDTECK_PREAMBLE[IDTECK_PREAMBLE_BITS] = {
    0, 1, 0, 0, 1, 0, 0, 1,  // 'I'
    0, 1, 0, 0, 0, 1, 0, 0,  // 'D'
    0, 1, 0, 1, 0, 1, 0, 0,  // 'T'
    0, 1, 0, 0, 1, 0, 1, 1   // 'K'
};

/*
 * Straddle gate. At some sample phases the best candidate is not aligned to the
 * bit boundaries, and it decodes to a wrong word that repeats from capture to
 * capture. Such a frame has at least one bit whose integrator spans a transition
 * and cancels to near zero, so its weakest bit is tiny relative to its average.
 * Measured min/mean: aligned frames 0.36-0.62, straddles 0.001-0.092. A weak but
 * genuine frame (tag on the back of the device) is also ragged, but an order of
 * magnitude quieter, so the gate only rejects frames that are both loud and
 * ragged. INDALA_PSK_STRADDLE_AMP sits between the two populations.
 */
#define INDALA_PSK_STRADDLE_AMP 2048
#define INDALA_PSK_STRADDLE_DIV 8

/*
 * Subtract the mean and multiply by (-1)^n: the fc/2 subcarrier moves to DC, the
 * original DC and slow envelope drift move to fs/2. A 14-bit conversion minus its
 * mean stays within int16, so this works in place.
 */
static void baseband_in_place(int16_t *y, size_t n) {
    int32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += y[i];
    }
    const int32_t mean = sum / (int32_t)n;
    for (size_t i = 0; i < n; i++) {
        int32_t v = (int32_t)y[i] - mean;
        y[i] = (int16_t)((i & 1u) ? -v : v);
    }
}

/*
 * Sum of one 32-sample bit after a [1,2,1] filter. The filter is needed: without
 * it the carrier ripple the mix moved to fs/2 leaks into every bit, and nothing
 * decodes. Summing the filtered signal over 32 samples equals a weighted sum of
 * the unfiltered one over 34 samples (weights 1,3,4,...,4,3,1), so
 *
 *     integrator = 4*sum(y[a..a+31]) + y[a-1] - y[a] - y[a+31] + y[a+32]
 *
 * with y read as 0 outside the capture. The factor of 4 is never divided out;
 * only signs and comparisons are used.
 */
static int32_t bit_integrator(const int16_t *y, size_t n, size_t a) {
    int32_t box = 0;
    for (size_t j = 0; j < INDALA_PSK_BIT_SAMPLES; j++) {
        box += y[a + j];
    }
    int32_t v = 4 * box - y[a] - y[a + INDALA_PSK_BIT_SAMPLES - 1];
    if (a > 0) {
        v += y[a - 1];
    }
    if (a + INDALA_PSK_BIT_SAMPLES < n) {
        v += y[a + INDALA_PSK_BIT_SAMPLES];
    }
    return v;
}

/* Exact match of `pattern` at bits[i], normal or inverted. A tolerance bought no
 * extra reads in testing, and a correlator can't be used: the preamble is mostly
 * one constant run, so it matches itself almost as well several bits off. */
static bool preamble_match(const uint8_t *bits, size_t i, bool inverted,
                           const uint8_t *pattern, size_t pattern_bits) {
    for (size_t j = 0; j < pattern_bits; j++) {
        uint8_t want = inverted ? (uint8_t)(1u - pattern[j]) : pattern[j];
        if (bits[i + j] != want) {
            return false;
        }
    }
    return true;
}

bool indala_psk_decode(int16_t *samples, size_t n, indala_psk_result_t *out) {
    if (samples == NULL || out == NULL || n < INDALA_PSK_MIN_SAMPLES) {
        return false;
    }
    if (n > INDALA_PSK_CAPTURE_SAMPLES) {
        n = INDALA_PSK_CAPTURE_SAMPLES;
    }
    baseband_in_place(samples, n);

    int32_t integ[INDALA_PSK_MAX_BITS];
    uint8_t bits[INDALA_PSK_MAX_BITS];
    uint8_t best_word[INDALA_PSK_FRAME_BITS];
    int32_t best_amp = -1;
    int32_t best_min = 0;
    uint8_t best_off = 0;
    bool found = false;
    bool rejected = false;

    /* The bit boundary within the 32-sample period is unknown, so every offset is
     * tried. Several offsets can match the preamble exactly while straddling the
     * true bit boundaries; the aligned one has the largest integrators, because a
     * straddling integrator averages part of each neighbour and partly cancels. */
    for (size_t off = 0; off < INDALA_PSK_BIT_SAMPLES; off++) {
        size_t nb = (n - off) / INDALA_PSK_BIT_SAMPLES;
        if (nb > INDALA_PSK_MAX_BITS) {
            nb = INDALA_PSK_MAX_BITS;
        }
        for (size_t k = 0; k < nb; k++) {
            integ[k] = bit_integrator(samples, n, off + k * INDALA_PSK_BIT_SAMPLES);
            bits[k] = (integ[k] > 0) ? 1u : 0u;
        }
        if (nb < INDALA_PSK_FRAME_BITS) {
            continue;
        }

        for (size_t i = 0; !rejected && i + IDTECK_PREAMBLE_BITS <= nb; i++) {
            if (preamble_match(bits, i, false, IDTECK_PREAMBLE, IDTECK_PREAMBLE_BITS) ||
                    preamble_match(bits, i, true, IDTECK_PREAMBLE, IDTECK_PREAMBLE_BITS)) {
                rejected = true;
            }
        }
        for (size_t i = 0; i + INDALA_PSK_FRAME_BITS <= nb; i++) {
            for (uint8_t inv = 0; inv < 2; inv++) {
                if (!preamble_match(bits, i, inv != 0, INDALA_PREAMBLE, INDALA_PSK_PREAMBLE_BITS)) {
                    continue;
                }
                uint8_t cand[INDALA_PSK_FRAME_BITS];
                for (size_t k = 0; k < INDALA_PSK_FRAME_BITS; k++) {
                    cand[k] = inv ? (uint8_t)(1u - bits[i + k]) : bits[i + k];
                }
                /* Bits 60 and 61 must be zero, as in the Flipper's Indala26 decoder: on
                 * recorded captures this rejected only wrong frames. A 64-bit format that
                 * sets either bit is not read. */
                if (cand[60] != 0 || cand[61] != 0) {
                    continue;
                }
                // Sum, not mean: every candidate spans the same 64 bits.
                int32_t amp = 0;
                int32_t mn = INT32_MAX;
                for (size_t k = 0; k < INDALA_PSK_FRAME_BITS; k++) {
                    int32_t v = integ[i + k];
                    v = (v < 0) ? -v : v;
                    amp += v;
                    if (v < mn) {
                        mn = v;
                    }
                }
                if (amp > best_amp) {
                    best_amp = amp;
                    best_min = mn;
                    best_off = (uint8_t)off;
                    memcpy(best_word, cand, sizeof(best_word));
                    found = true;
                }
            }
        }
    }

    if (rejected || !found) {
        return false;
    }

    const int32_t mean_amp = best_amp / INDALA_PSK_FRAME_BITS;
    if (mean_amp >= INDALA_PSK_STRADDLE_AMP &&
            best_min * INDALA_PSK_STRADDLE_DIV < mean_amp) {
        return false;
    }

    for (size_t k = 0; k < sizeof(out->id); k++) {
        uint8_t byte = 0;
        for (size_t b = 0; b < 8; b++) {
            byte = (uint8_t)((byte << 1) | best_word[k * 8 + b]);
        }
        out->id[k] = byte;
    }
    out->offset = best_off;
    return true;
}
