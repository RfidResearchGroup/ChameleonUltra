#include <string.h>

#include "bsp_time.h"
#include "lf_125khz_radio.h"
#include "lf_indala_psk.h"
#include "lf_reader_data.h"
#include "lf_reader_generic.h"

#define NRF_LOG_MODULE_NAME indala_reader
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/*
 * Sample phases to try, in 62.5ns ticks within the 8us carrier period.
 *
 * Only one quadrature of the subcarrier is observable, so the phase at which the
 * ADC samples decides how much of it is seen. Measured on hardware (32 phases x
 * 5 captures, tag on the front and on the back of the device), phases 60-92 are a
 * dead band. Worse than dead: there the decoder can return a wrong word, the same
 * one on every capture, which the agreement rule below can't catch. No phase in
 * 60-92 may be added here. The list starts with the phases that decoded best with
 * the tag on either side, then a few spread out as insurance in case the map
 * differs on another unit or tag.
 */
static const uint8_t PHASE_ROTATION[] = {
    20, 12, 28, 36, 44, 16, 112, 0
};
#define PHASE_ROTATION_COUNT (sizeof(PHASE_ROTATION) / sizeof(PHASE_ROTATION[0]))

/* Captures tried at each phase before moving on. */
#define INDALA_TRIES_PER_PHASE 8

/* A capture is real time: 4096 samples at 125kHz is about 33ms. */
#define INDALA_CAPTURE_TIMEOUT_MS (200u + (INDALA_PSK_CAPTURE_SAMPLES / 125u) * 2u)

static int16_t m_samples[INDALA_PSK_CAPTURE_SAMPLES];

/*
 * Read an Indala-64 frame. Returns the 8-byte raw frame in `data`.
 *
 * A single decode isn't trusted: in testing, single decodes were sometimes off
 * by a bit or two, but each wrong frame was different while the true one
 * recurred. So two captures at the same sample phase must produce the same 64
 * bits before a frame is returned.
 */
bool indala_read(uint8_t *data, uint32_t timeout_ms) {
    bool ok = false;
    indala_psk_result_t res;
    autotimer *p_at = bsp_obtain_timer(0);

    for (size_t pi = 0; pi < PHASE_ROTATION_COUNT && !ok; pi++) {
        const uint8_t phase = PHASE_ROTATION[pi];
        lf_125khz_radio_saadc_phase_set(phase);

        // Agreement only counts within one phase: a frame from another phase is a
        // different measurement, not a repeat of this one.
        bool have_prev = false;
        uint8_t prev[sizeof(res.id)] = {0};

        for (uint8_t k = 0; k < INDALA_TRIES_PER_PHASE && !ok; k++) {
            if (!NO_TIMEOUT_1MS(p_at, timeout_ms)) {
                break;
            }
            size_t got = 0;
            if (!raw_read_samples(m_samples, INDALA_PSK_CAPTURE_SAMPLES,
                                  INDALA_CAPTURE_TIMEOUT_MS, &got)) {
                continue;
            }
            // A capture with dropped samples is two pieces of waveform with a gap
            // between them; the decoder would resynchronise and return a frame
            // with a wrong tail. Retry instead.
            if (lf_capture_dropped() != 0) {
                continue;
            }
            if (!indala_psk_decode(m_samples, got, &res)) {
                continue;
            }
            if (have_prev && memcmp(prev, res.id, sizeof(res.id)) == 0) {
                ok = true;
                NRF_LOG_INFO("indala phase %u tries %u offset %u", phase, k + 1, res.offset);
            } else {
                memcpy(prev, res.id, sizeof(res.id));
                have_prev = true;
            }
        }
    }
    bsp_return_timer(p_at);

    // Every other LF reader shares the ADC trigger and expects the default.
    lf_125khz_radio_saadc_phase_set(0);

    if (ok) {
        memcpy(data, res.id, sizeof(res.id));
    }
    return ok;
}
