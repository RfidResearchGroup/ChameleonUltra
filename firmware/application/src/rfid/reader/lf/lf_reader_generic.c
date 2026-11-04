#include "lf_reader_generic.h"
#include "ble_main.h"
#include "bsp_delay.h"
#include "bsp_wdt.h"
#include "lf_125khz_radio.h"
#include "lf_sample_window.h"
#include "timeslot.h"

#define LF_CAPTURE_TIMESLOT_US 90000U
#define LF_CAPTURE_MAX_WAIT_US 80000U
#define LF_CAPTURE_POLL_US       100U
#define LF_CAPTURE_SETTLE_US    2000U

static lf_sample_window_t m_capture;
static uint32_t m_wait_us;
static bool m_capture_busy;

_Static_assert(sizeof(nrf_saadc_value_t) == sizeof(int16_t),
               "LF capture expects signed 16-bit SAADC samples");

static void capture_samples(nrf_saadc_value_t *values, size_t count) {
    /* Copy a full DMA batch directly instead of dropping its tail in a ring. */
    lf_sample_window_feed(&m_capture, values, count);
}

static void capture_timeslot(void) {
    start_lf_125khz_radio();
    bsp_delay_us(LF_CAPTURE_SETTLE_US);
    /* DMA is prepared already; no samples precede field stabilization. */
    lf_125khz_radio_saadc_start_sampling();
    for (uint32_t waited = 0U;
            !lf_sample_window_complete(&m_capture) && waited < m_wait_us;
            waited += LF_CAPTURE_POLL_US) {
        bsp_delay_us(LF_CAPTURE_POLL_US);
        bsp_wdt_feed();
    }
    lf_125khz_radio_saadc_stop_sampling();
    stop_lf_125khz_radio();
}

bool raw_read_to_buffer(uint8_t *data, size_t maxlen, uint32_t timeout_ms, size_t *outlen) {
    if (outlen == NULL) return false;
    *outlen = 0U;
    if (data == NULL || maxlen == 0U || maxlen > LF_SNIFF_MAX_SAMPLES ||
            timeout_ms == 0U || m_capture_busy || is_nus_working()) {
        return false;
    }
    m_capture_busy = true;
    bool resume_advertising = false;
    if (!advertising_pause(&resume_advertising)) {
        m_capture_busy = false;
        return false;
    }
    if (is_nus_working()) {
        if (resume_advertising) advertising_start(false);
        m_capture_busy = false;
        return false;
    }
    lf_sample_window_init(&m_capture, data, maxlen);
    m_wait_us = timeout_ms >= LF_CAPTURE_MAX_WAIT_US / 1000U
        ? LF_CAPTURE_MAX_WAIT_US : timeout_ms * 1000U;
    lf_125khz_radio_saadc_prepare(capture_samples);
    /*
     * RTC1 is masked inside request_timeslot, so a BSP millisecond timer cannot
     * implement this deadline. This finite microsecond loop fits in 90 ms.
     */
    request_timeslot(LF_CAPTURE_TIMESLOT_US, capture_timeslot);
    lf_125khz_radio_saadc_disable();
    bool complete = lf_sample_window_complete(&m_capture);
    if (complete) *outlen = m_capture.count;
    /* Stop the producer before releasing the caller's destination/resuming BLE. */
    lf_sample_window_init(&m_capture, NULL, 0U);
    if (resume_advertising) advertising_start(false);
    m_capture_busy = false;
    return complete;
}
