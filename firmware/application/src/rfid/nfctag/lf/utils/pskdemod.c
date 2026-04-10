#include "pskdemod.h"

#include <stdlib.h>
#include <string.h>

// Shared static sample buffer for PSK reader codecs.
uint16_t psk_shared_samples[PSK_SHARED_BUF_SIZE];

// Shared static PWM buffer for PSK emulation modulators.
nrf_pwm_values_wave_form_t psk_shared_pwm_vals[PSK_PWM_MAX_ENTRIES] = {};
nrf_pwm_sequence_t psk_shared_pwm_seq = {
    .values.p_wave_form = psk_shared_pwm_vals,
    .length = 0,
    .repeats = 0,
    .end_delay = 0,
};

// cos(2π·2·n/8) × 1024, period 4 samples.
// At fs=166.67kHz, DFT bin k=2 of N=8 → 2×166.67/8 = 41.67kHz.
// 125kHz carrier aliases to 166.67-125 = 41.67kHz — matches bin k=2.
// The tuned antenna passes the carrier; fc/2 subcarrier is filtered out.
static const int16_t psk166_cos_lut[8] = {
    1024, 0, -1024, 0, 1024, 0, -1024, 0
};

// sin(2π·2·n/8) × 1024, period 4 samples.
// Quadrature component for phase-independent carrier phase detection.
static const int16_t psk166_sin_lut[8] = {
    0, 1024, 0, -1024, 0, 1024, 0, -1024
};

void psk_free(psk_t *m) {
    if (m != NULL) {
        if (m->samples) {
            free(m->samples);
        }
        free(m);
    }
}

void psk_feed_sample(psk_t *m, uint16_t sample) {
    if (m->sample_count < m->buf_size) {
        m->samples[m->sample_count++] = sample;
    }
}

void psk_shift(psk_t *m, uint16_t shift) {
    if (shift >= m->sample_count) {
        m->sample_count = 0;
    } else {
        memmove(m->samples, m->samples + shift,
                (m->sample_count - shift) * sizeof(uint16_t));
        m->sample_count -= shift;
    }
    m->phase_offset = (m->phase_offset + shift) % 8;
}

// Legacy: even/odd correlation at 125kHz (1 sample per carrier cycle).
int32_t psk_correlate(psk_t *m, uint16_t start) {
    int32_t corr = 0;
    for (uint8_t s = 0; s < m->bitrate; s++) {
        if (s & 1)
            corr -= (int32_t)m->samples[start + s];
        else
            corr += (int32_t)m->samples[start + s];
    }
    return corr;
}

// DFT-based fc/2 correlation at 166.67kHz sampling rate.
// Multiplies `len` samples by cos(2π·3n/8) reference, using absolute
// sample index for phase coherence across buffer shifts.
int32_t psk166_correlate(psk_t *m, uint16_t start, uint8_t len) {
    int32_t corr = 0;
    for (uint8_t j = 0; j < len && (start + j) < m->sample_count; j++) {
        uint16_t idx = start + j;
        corr += (int32_t)m->samples[idx] *
                psk166_cos_lut[(idx + m->phase_offset) & 7];
    }
    return corr;
}

// Phase-independent fc/2 correlation using both cos and sin (IQ) components.
// Returns the component (Re or Im) with larger magnitude, preserving sign.
// This is robust against arbitrary carrier phase relative to the DFT reference.
int32_t psk166_correlate_iq(psk_t *m, uint16_t start, uint8_t len) {
    int32_t re = 0, im = 0;
    for (uint8_t j = 0; j < len && (start + j) < m->sample_count; j++) {
        uint16_t idx = start + j;
        int32_t s = (int32_t)m->samples[idx];
        uint8_t lut_idx = (idx + m->phase_offset) & 7;
        re += s * psk166_cos_lut[lut_idx];
        im += s * psk166_sin_lut[lut_idx];
    }
    // Pick the axis with larger magnitude — sign is consistent for PSK1
    int32_t abs_re = re < 0 ? -re : re;
    int32_t abs_im = im < 0 ? -im : im;
    return (abs_re >= abs_im) ? re : im;
}

psk_t *psk_alloc(uint8_t bitrate, uint16_t buf_samples) {
    psk_t *m = (psk_t *)malloc(sizeof(psk_t));
    m->bitrate = bitrate;
    m->buf_size = buf_samples;
    m->samples = buf_samples > 0 ? (uint16_t *)malloc(buf_samples * sizeof(uint16_t)) : NULL;
    m->sample_count = 0;
    m->phase_offset = 0;
    return m;
}
