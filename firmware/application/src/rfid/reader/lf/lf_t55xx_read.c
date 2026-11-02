#include "lf_t55xx_read.h"

#include <string.h>

#include "bsp_delay.h"
#include "bsp_wdt.h"
#include "hw_connect.h"
#include "lf_125khz_radio.h"
#include "nrf_gpio.h"
#include "timeslot.h"

/* All timings below are field periods at 125 kHz. No EEPROM command is used. */
#define T55XX_CARRIER_US          8U
#define T55XX_START_GAP_US        (30U * T55XX_CARRIER_US)
#define T55XX_BIT_GAP_US          (9U * T55XX_CARRIER_US)
#define T55XX_ZERO_ON_US          (24U * T55XX_CARRIER_US)
#define T55XX_ONE_ON_US           (54U * T55XX_CARRIER_US)
#define T55XX_POWER_UP_US         4000U
#define T55XX_CAPTURE_TIMEOUT_US  70000U
#define T55XX_CAPTURE_POLL_US     100U
#define T55XX_READ_TIMESLOT_US    90000U

static int16_t m_samples[LF_T55XX_CAPTURE_MAX_SAMPLES];
_Static_assert(sizeof(nrf_saadc_value_t) == sizeof(m_samples[0]),
               "LF capture expects signed 16-bit SAADC samples");
static volatile size_t m_sample_count;
static uint8_t m_block;
static bool m_capture_busy;

static void capture_samples(nrf_saadc_value_t *samples, size_t count) {
    size_t offset = m_sample_count;
    size_t available = LF_T55XX_CAPTURE_MAX_SAMPLES - offset;
    if (count > available) {
        count = available;
    }
    if (count != 0U) {
        memcpy(&m_samples[offset], samples, count * sizeof(m_samples[0]));
        m_sample_count = offset + count;
    }
}

static void send_gap(uint32_t duration_us) {
    /*
     * Stopping PWM releases the output pin to GPIO. Drive it low explicitly,
     * otherwise its previous GPIO value could leave the field enabled.
     */
    stop_lf_125khz_radio();
    nrf_gpio_cfg_output(LF_ANT_DRIVER);
    nrf_gpio_pin_clear(LF_ANT_DRIVER);
    bsp_delay_us(duration_us);
    start_lf_125khz_radio();
}

static void send_bit(uint8_t value) {
    bsp_delay_us(value != 0U ? T55XX_ONE_ON_US : T55XX_ZERO_ON_US);
    send_gap(T55XX_BIT_GAP_US);
}

static void read_and_capture(void) {
    start_lf_125khz_radio();
    bsp_delay_us(T55XX_POWER_UP_US);
    send_gap(T55XX_START_GAP_US);

    /*
     * Ordinary Page 0 direct-access READ is 1,0,0,A2,A1,A0. In particular, the
     * zero after the opcode is not a request to lock a word: the 32-bit data
     * field that distinguishes WRITE is never present in this transmitter.
     */
    send_bit(1U);
    send_bit(0U);
    send_bit(0U);
    send_bit((m_block >> 2U) & 1U);
    send_bit((m_block >> 1U) & 1U);
    send_bit(m_block & 1U);

    /*
     * ADC DMA was prepared before the timing window, but PPI sampling begins
     * only after the final command gap. The existing writer's six-ms delay
     * would miss the start of this response, so the reader has its own path.
     */
    lf_125khz_radio_saadc_start_sampling();
    for (uint32_t waited_us = 0U;
            m_sample_count < LF_T55XX_CAPTURE_MAX_SAMPLES &&
            waited_us < T55XX_CAPTURE_TIMEOUT_US;
            waited_us += T55XX_CAPTURE_POLL_US) {
        bsp_delay_us(T55XX_CAPTURE_POLL_US);
        bsp_wdt_feed();
    }
    lf_125khz_radio_saadc_stop_sampling();
    stop_lf_125khz_radio();
}

static void put_i16_be(uint8_t *destination, int16_t value) {
    uint16_t encoded = (uint16_t)value;
    destination[0] = (uint8_t)(encoded >> 8U);
    destination[1] = (uint8_t)encoded;
}

static void pack_capture(uint8_t *out) {
    int16_t low = m_samples[0];
    int16_t high = low;
    for (size_t i = 1U; i < LF_T55XX_CAPTURE_MAX_SAMPLES; i++) {
        if (m_samples[i] < low) low = m_samples[i];
        if (m_samples[i] > high) high = m_samples[i];
    }

    out[0] = 1U;
    out[1] = 4U;
    out[2] = (uint8_t)(LF_T55XX_CAPTURE_MAX_SAMPLES >> 8U);
    out[3] = (uint8_t)LF_T55XX_CAPTURE_MAX_SAMPLES;
    put_i16_be(&out[4], low);
    put_i16_be(&out[6], high);

    uint32_t span = (uint32_t)((int32_t)high - low);
    for (size_t i = 0U; i < LF_T55XX_CAPTURE_MAX_SAMPLES; i += 2U) {
        uint8_t pair = 0U;
        for (size_t half = 0U; half < 2U; half++) {
            uint32_t shifted = (uint32_t)((int32_t)m_samples[i + half] - low);
            uint8_t quantized = span != 0U
                                ? (uint8_t)((shifted * 15U + span / 2U) / span)
                                : 8U;
            pair |= (uint8_t)(quantized << (half == 0U ? 4U : 0U));
        }
        out[LF_T55XX_CAPTURE_HEADER_SIZE + i / 2U] = pair;
    }
}

bool t55xx_read_block_capture(
    uint8_t block,
    uint8_t *out,
    size_t capacity,
    size_t *outlen
) {
    if (outlen == NULL) {
        return false;
    }
    *outlen = 0U;
    if (block > 7U || out == NULL || capacity < LF_T55XX_CAPTURE_MAX_BYTES ||
            m_capture_busy || is_nus_working()) {
        return false;
    }

    m_capture_busy = true;
    /*
     * This first acquisition path requires an unconnected BLE radio. Existing
     * LF work shows advertising can disturb the carrier; resume it only when
     * this operation actually paused it. Do not take ownership of advertising
     * that was already stopped by another caller.
     */
    bool resume_advertising = false;
    if (!advertising_pause(&resume_advertising)) {
        m_capture_busy = false;
        return false;
    }
    if (is_nus_working()) {
        m_capture_busy = false;
        if (resume_advertising) {
            advertising_start(false);
        }
        return false;
    }
    m_block = block;
    m_sample_count = 0U;
    lf_125khz_radio_saadc_prepare(capture_samples);

    /*
     * Reserve the acquisition as well as the six transmitted bits. BLE clock
     * changes inside a capture can introduce false field gaps. The bounded
     * 70-ms wait plus power-up/transmit time fits in this 90-ms reservation.
     * RTC1 is masked by request_timeslot(), so use a finite microsecond loop
     * instead of waiting for a BSP timer that cannot advance here.
     */
    request_timeslot(T55XX_READ_TIMESLOT_US, read_and_capture);
    lf_125khz_radio_saadc_disable();

    /* Keep the sample buffer leased until quantization has finished. */
    bool captured = m_sample_count == LF_T55XX_CAPTURE_MAX_SAMPLES;
    if (captured) {
        pack_capture(out);
        *outlen = LF_T55XX_CAPTURE_MAX_BYTES;
    }
    if (resume_advertising) {
        advertising_start(false);
    }
    m_capture_busy = false;
    /* A partial DMA batch or timeout must never look like a complete word. */
    return captured;
}
