#include "lf_gap.h"

#include "bsp_delay.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"

#define NRF_LOG_MODULE_NAME lf_gap
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/* -----------------------------------------------------------------------
 * Transmit side
 *
 * All functions must be called from within a timeslot callback, exactly
 * as t55xx_timeslot_callback() does in lf_t55xx_data.c.  The timeslot
 * gives us uninterrupted CPU time so the µs-precision delays are accurate.
 * --------------------------------------------------------------------- */

void lf_gap_send_start(void) {
    stop_lf_125khz_radio();
    bsp_delay_us(GAP_START_US);
    start_lf_125khz_radio();
}

void lf_gap_send_bit(uint8_t bit) {
    /* Field on for the bit duration, then a write gap */
    if (bit & 1) {
        bsp_delay_us(GAP_BIT1_US);
    } else {
        bsp_delay_us(GAP_BIT0_US);
    }
    stop_lf_125khz_radio();
    bsp_delay_us(GAP_WRITE_US);
    start_lf_125khz_radio();
}

void lf_gap_send_u32(uint32_t word) {
    lf_gap_send_bits(word, 32);
}

void lf_gap_send_bits(uint32_t value, uint8_t nbits) {
    for (int8_t i = (int8_t)(nbits - 1); i >= 0; i--) {
        lf_gap_send_bit((value >> i) & 1);
    }
}

/* -----------------------------------------------------------------------
 * Receive side
 *
 * The GPIOTE edge counter (m_pwm_timer_counter via get_lf_counter_value)
 * increments once per carrier cycle while the field is present and edges
 * arrive.  During a gap, no edges arrive so the counter freezes.
 *
 * We detect a gap by comparing the current counter value against the value
 * at the last known edge.  If the difference exceeds GAP_DETECT_TIMEOUT_TC
 * we declare a gap.
 *
 * Note: get_lf_counter_value() returns the captured counter (snapshot),
 * not a live read.  The counter is captured by the PPI on each PWM period
 * end.  At 125kHz this gives 8µs granularity which is sufficient.
 * --------------------------------------------------------------------- */

bool lf_gap_detect(uint32_t last_count, uint32_t *gap_tc) {
    uint32_t now = get_lf_counter_value();

    /*
     * Handle counter wrap (32-bit, wraps at 2^32 carrier cycles ≈ 9.5 hours
     * of continuous field — effectively never, but handle it correctly).
     */
    uint32_t elapsed = now - last_count;

    if (elapsed >= GAP_DETECT_TIMEOUT_TC) {
        *gap_tc = elapsed;
        return true;
    }
    return false;
}
