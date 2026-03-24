#include "lf_gap.h"

#include "bsp_delay.h"
#include "hw_connect.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "nrf_gpio.h"

#define NRF_LOG_MODULE_NAME lf_gap
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/* -----------------------------------------------------------------------
 * Transmit side
 *
 * All functions must be called from within a timeslot callback.
 *
 * Gap generation: we cannot rely on nrfx_pwm_stop() to cut the field
 * because when the PWM stops it releases LF_ANT_DRIVER to GPIO state,
 * which may leave the antenna driver enabled.  Instead we:
 *   1. Stop the PWM (releases pin to GPIO)
 *   2. Explicitly drive LF_ANT_DRIVER low (field off)
 *   3. Delay for the gap duration
 *   4. Drive LF_ANT_DRIVER high then restart PWM (field on)
 * --------------------------------------------------------------------- */

static inline void field_off(void) {
    nrfx_pwm_stop(&m_pwm, true);           /* stop PWM, releases pin    */
    nrf_gpio_cfg_output(LF_ANT_DRIVER);
    nrf_gpio_pin_clear(LF_ANT_DRIVER);     /* drive low = field off     */
}

static inline void field_on(void) {
    nrf_gpio_pin_set(LF_ANT_DRIVER);       /* drive high briefly        */
    start_lf_125khz_radio();               /* restart PWM on pin        */
}

void lf_gap_send_start(void) {
    field_off();
    bsp_delay_us(GAP_START_US);
    field_on();
}

void lf_gap_send_bit(uint8_t bit) {
    if (bit & 1) {
        bsp_delay_us(GAP_BIT1_US);
    } else {
        bsp_delay_us(GAP_BIT0_US);
    }
    field_off();
    bsp_delay_us(GAP_WRITE_US);
    field_on();
}

void lf_gap_send_u32(uint32_t word) {
    lf_gap_send_bits(word, 32);
}

void lf_gap_send_bits(uint32_t value, uint8_t nbits) {
    for (int8_t i = (int8_t)(nbits - 1); i >= 0; i--) {
        lf_gap_send_bit((value >> i) & 1);
    }
}

bool lf_gap_detect(uint32_t last_count, uint32_t *gap_tc) {
    uint32_t now = get_lf_counter_value();
    uint32_t elapsed = now - last_count;
    if (elapsed >= GAP_DETECT_TIMEOUT_TC) {
        *gap_tc = elapsed;
        return true;
    }
    return false;
}
