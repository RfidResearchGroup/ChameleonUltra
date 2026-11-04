#include "lf_em4x05_read.h"

#include <string.h>

#include "ble_main.h"
#include "bsp_delay.h"
#include "bsp_wdt.h"
#include "em4x05_forward.h"
#include "hw_connect.h"
#include "lf_125khz_radio.h"
#include "nrf_gpio.h"
#include "timeslot.h"

static int16_t m_samples[EM4X05_CAPTURE_SAMPLES];
static volatile size_t m_count;
static uint8_t m_bits[EM4X05_LOGIN_BITS];
static size_t m_bits_count;
static bool m_busy;

_Static_assert(sizeof(nrf_saadc_value_t) == sizeof(int16_t), "signed SAADC capture");

static void capture_samples(nrf_saadc_value_t *samples, size_t count) {
    size_t offset = m_count;
    size_t available = EM4X05_CAPTURE_SAMPLES - offset;
    if (count > available) count = available;
    if (count != 0U) {
        memcpy(m_samples + offset, samples, count * sizeof(int16_t));
        m_count = offset + count;
    }
}

static void field_gap(uint32_t duration) {
    stop_lf_125khz_radio();
    /* PWM release must leave the antenna drive physically low. */
    nrf_gpio_cfg_output(LF_ANT_DRIVER);
    nrf_gpio_pin_clear(LF_ANT_DRIVER);
    bsp_delay_us(duration);
    start_lf_125khz_radio();
}

static void send_forward(void) {
    field_gap(55U * 8U);
    bsp_delay_us(18U * 8U);
    for (size_t i = 1U; i < m_bits_count; ++i) {
        if (m_bits[i] != 0U) {
            bsp_delay_us(32U * 8U);
        } else {
            field_gap(23U * 8U);
            bsp_delay_us(18U * 8U);
        }
    }
}

static void read_and_capture(void) {
    send_forward();
    /* Arm after the final command symbol, without a writer's EEPROM delay. */
    lf_125khz_radio_saadc_start_sampling();
    for (uint32_t elapsed = 0U;
            elapsed < 60000U && m_count < EM4X05_CAPTURE_SAMPLES;
            elapsed += 100U) {
        bsp_delay_us(100U);
        bsp_wdt_feed();
    }
    lf_125khz_radio_saadc_stop_sampling();
    stop_lf_125khz_radio();
}

static void pack_capture(uint8_t *output) {
    int16_t low = m_samples[0], high = low;
    for (size_t i = 1U; i < EM4X05_CAPTURE_SAMPLES; ++i) {
        if (m_samples[i] < low) low = m_samples[i];
        if (m_samples[i] > high) high = m_samples[i];
    }
    output[0] = 1U;
    output[1] = 4U;
    output[2] = EM4X05_CAPTURE_SAMPLES >> 8U;
    output[3] = EM4X05_CAPTURE_SAMPLES & 0xFFU;
    output[4] = (uint16_t)low >> 8U;
    output[5] = (uint16_t)low & 0xFFU;
    output[6] = (uint16_t)high >> 8U;
    output[7] = (uint16_t)high & 0xFFU;
    uint32_t span = (uint32_t)((int32_t)high - low);
    for (size_t i = 0U; i < EM4X05_CAPTURE_SAMPLES; i += 2U) {
        uint8_t pair = 0U;
        for (size_t half = 0U; half < 2U; ++half) {
            uint32_t relative = (uint32_t)((int32_t)m_samples[i + half] - low);
            uint8_t value = span != 0U ? (uint8_t)((relative * 15U + span / 2U) / span) : 8U;
            pair |= value << (half == 0U ? 4U : 0U);
        }
        output[8U + i / 2U] = pair;
    }
}

bool em4x05_read_capture(uint8_t address, uint32_t password, bool use_password,
                        uint8_t *output, size_t capacity, size_t *outlen) {
    if (outlen == NULL) return false;
    *outlen = 0U;
    if (address >= 16U || address == 2U || output == NULL ||
            capacity < EM4X05_CAPTURE_BYTES || m_busy || is_nus_working()) return false;
    m_busy = true;
    bool resume = false;
    if (!advertising_pause(&resume)) {
        m_busy = false;
        return false;
    }
    if (is_nus_working()) {
        if (resume) advertising_start(false);
        m_busy = false;
        return false;
    }
    start_lf_125khz_radio();
    bsp_delay_ms(4U);
    if (use_password) {
        m_bits_count = em4x05_login_bits(password, m_bits, sizeof(m_bits));
        request_timeslot(30000U, send_forward);
        /* Allow preamble/error response to finish; no password validity is claimed. */
        bsp_delay_ms(16U);
    }
    m_bits_count = em4x05_read_bits(address, m_bits, sizeof(m_bits));
    m_count = 0U;
    lf_125khz_radio_saadc_prepare(capture_samples);
    request_timeslot(75000U, read_and_capture);
    lf_125khz_radio_saadc_disable();
    bool complete = m_count == EM4X05_CAPTURE_SAMPLES;
    if (complete) {
        pack_capture(output);
        *outlen = EM4X05_CAPTURE_BYTES;
    }
    /* The destination remains leased until packing completes. */
    memset(m_bits, 0, sizeof(m_bits));
    if (resume) advertising_start(false);
    m_busy = false;
    return complete;
}
