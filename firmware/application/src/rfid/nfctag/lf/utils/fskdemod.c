#include "fskdemod.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979f
#define GOERTZEL(FREQ, SAMPLE_RATE) (2.0 * cos((2.0 * PI * FREQ) / (SAMPLE_RATE)))

float goertzel_mag(float coef, uint16_t samples[], int n) {
    float z1 = 0;
    float z2 = 0;
    for (int i = 0; i < n; i++) {
        float z0 = coef * z1 - z2 + (float)(samples[i]);
        z2 = z1;
        z1 = z0;
    }
    return sqrt(z1 * z1 + z2 * z2 - coef * z1 * z2);
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
    m->samples[m->c++] = sample;
    if (m->c < m->bitrate) {
        return false;
    }
    float bit0 = goertzel_mag(m->goertzel_fc_8,  m->samples, m->bitrate);
    float bit1 = goertzel_mag(m->goertzel_fc_10, m->samples, m->bitrate);
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
    m->goertzel_fc_8 = GOERTZEL(15625.0f, 125000.0f);
    m->goertzel_fc_10 = GOERTZEL(12500.0f, 125000.0f);
    return m;
}
