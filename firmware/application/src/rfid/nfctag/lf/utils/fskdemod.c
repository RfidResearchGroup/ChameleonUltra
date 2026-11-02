#include "fskdemod.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979f
#define GOERTZEL(FREQ, SAMPLE_RATE) (2.0 * cos((2.0 * PI * FREQ) / (SAMPLE_RATE)))

// Goertzel squared magnitude (|X|^2) with an optional DC offset subtracted from
// each sample.
//
// No sqrt: the bit decision only compares two of these and power is monotonic
// with magnitude, so the sqrt is dropped (the Cortex-M4F FPU has no double
// hardware; a double sqrt() would pull in a soft-float routine). The result can
// round slightly negative near zero signal, which is harmless for a comparison.
//
// DC offset: a 50-sample (RF/50) window holds an integer number of fc/10 cycles
// but a non-integer count of fc/8, so the raw ADC's DC component leaks into the
// fc/8 bin and biases the decision toward fc/8. Subtracting a running baseline
// (dc) removes that bias; pass dc=0 for the legacy behaviour.
static float goertzel_power(float coef, const uint16_t samples[], int n, float dc) {
    float z1 = 0;
    float z2 = 0;
    for (int i = 0; i < n; i++) {
        float z0 = coef * z1 - z2 + ((float)samples[i] - dc);
        z2 = z1;
        z1 = z0;
    }
    return z1 * z1 + z2 * z2 - coef * z1 * z2;
}

void fsk_free(fsk_t *m) {
    if (m != NULL) {
        free(m);
    }
}

/**
 * Changed from fixed BITRATE constant to dynamic parameter to allow
 * multiple protocols with different speeds.
 */
bool fsk_feed(fsk_t *m, uint16_t sample, bool *bit) {
    if (m->dc_block) {
        // Slow IIR baseline tracker (time const ~64 samples). Removes the large
        // raw-ADC DC offset that otherwise leaks into the fc/8 Goertzel bin and
        // pins every decision to bit 0. Unlike a per-window mean, a running
        // baseline does not distort fc/8's non-integer cycle count in a window.
        if (!m->dc_seen) {
            m->dc_run = (float)sample;
            m->dc_seen = true;
        } else {
            m->dc_run += ((float)sample - m->dc_run) * (1.0f / 64.0f);
        }
    }
    m->samples[m->c++] = sample;
    if (m->c < m->bitrate) {
        return false;
    }
    float dc = m->dc_block ? m->dc_run : 0.0f;
    float bit0 = goertzel_power(m->goertzel_fc_8,  m->samples, m->bitrate, dc);
    float bit1 = goertzel_power(m->goertzel_fc_10, m->samples, m->bitrate, dc);
    *bit = (bit1 > bit0);

    // Reset counter and clear sample buffer for the next bit
    m->c = 0;
    memset(m->samples, 0, sizeof(uint16_t) * m->bitrate);
    return true;
}

/**
 * Changed from fixed BITRATE constant to dynamic parameter to allow
 * multiple protocols with different speeds.
 */
fsk_t *fsk_alloc(uint8_t bitrate) {
    if (bitrate == 0 || bitrate > FSK_MAX_BITRATE) {
        return NULL;
    }

    fsk_t *m = (fsk_t *)malloc(sizeof(fsk_t));
    if (!m) return NULL;

    m->bitrate = bitrate;
    m->c = 0;
    m->dc_block = false;
    m->dc_seen = false;
    m->dc_run = 0.0f;
    m->goertzel_fc_8 = GOERTZEL(15625.0f, 125000.0f);
    m->goertzel_fc_10 = GOERTZEL(12500.0f, 125000.0f);
    return m;
}
