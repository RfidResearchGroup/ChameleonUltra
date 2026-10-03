#include "lf_reader_generic.h"
#include "lf_reader_data.h"

#include "bsp_delay.h"
#include "bsp_wdt.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME lfgeneric
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/*
 * Circular buffer for SAADC samples.
 *
 * It must hold a whole SAADC batch: the SAADC hands saadc_cb() ADC_BUF_SIZE
 * (2048, ble_main.c) samples at once, every 16.4ms, and anything that doesn't
 * fit is dropped. 2560 holds one batch with slack; the main loop drains it
 * long before the next. It is malloc'd, so __HEAP_SIZE in application/Makefile
 * must cover it.
 */
#define CIRCULAR_BUFFER_SIZE (2560)
static circular_buffer cb;

// Samples lost because the ring was full. A capture with drops is pieces of
// waveform joined across unknown gaps, so callers that decode across the whole
// capture should discard it (see lf_capture_dropped()).
static volatile uint32_t m_cb_dropped = 0;

static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (int i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            m_cb_dropped += (uint32_t)(size - i);  /* buffer full: the rest of this batch is lost */
            return;
        }
    }
}

static void init_saadc_hw(void) {
    lf_125khz_radio_saadc_enable(saadc_cb);
}

static void uninit_saadc_hw(void) {
    lf_125khz_radio_saadc_disable();
}

bool raw_read_to_buffer(uint8_t *data, size_t maxlen, uint32_t timeout_ms, size_t *outlen) {
    *outlen = 0;

    cb_init(&cb, CIRCULAR_BUFFER_SIZE, sizeof(uint16_t));
    init_saadc_hw();
    start_lf_125khz_radio();

    /* Wait for antenna to settle before capturing.
     * The LC circuit rings for ~400µs on field startup, then takes
     * another ~800µs to reach steady state. Skip 2ms to be safe. */
    bsp_delay_ms(2);

    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, timeout_ms) && *outlen < maxlen) {
        uint16_t val = 0;
        while (cb_pop_front(&cb, &val) && *outlen < maxlen) {
            val = val >> 5;  /* 14-bit ADC → 9-bit, then >>5 gives 8-bit */
            data[*outlen] = val > 0xff ? 0xff : (uint8_t)val;
            ++(*outlen);
        }
        bsp_wdt_feed();  /* prevent watchdog reset during long captures */
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_saadc_hw();
    cb_free(&cb);

    return true;
}

uint32_t lf_capture_dropped(void) {
    return m_cb_dropped;
}

bool raw_read_samples(int16_t *samples, size_t count, uint32_t timeout_ms, size_t *outlen) {
    *outlen = 0;
    m_cb_dropped = 0;

    if (!cb_init(&cb, CIRCULAR_BUFFER_SIZE, sizeof(uint16_t))) {
        return false;
    }
    init_saadc_hw();
    start_lf_125khz_radio();

    /* Same settle as raw_read_to_buffer(). The SAADC delivers its first batch
     * about 16ms after start, so this rarely drains anything and the capture
     * includes the start-up transient. The Indala decoder relies on that: its
     * mix and filter remove the transient, and the first frame's preamble is
     * in those early samples. */
    bsp_delay_ms(2);
    uint16_t val = 0;
    while (cb_pop_front(&cb, &val)) {
    }

    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, timeout_ms) && *outlen < count) {
        while (*outlen < count && cb_pop_front(&cb, &val)) {
            samples[(*outlen)++] = (int16_t)(val & 0x3FFF);
        }
        bsp_wdt_feed();
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_saadc_hw();
    cb_free(&cb);

    return *outlen == count;
}
