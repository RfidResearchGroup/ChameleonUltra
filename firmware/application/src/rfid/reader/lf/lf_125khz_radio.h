#pragma once

#include "ble_main.h"
#include "nrfx_pwm.h"

/* Exposed so lf_gap.c can stop the PWM and drive LF_ANT_DRIVER directly
 * to create clean field gaps without relying on PWM pin release state. */
extern nrfx_pwm_t m_pwm;


/* Carrier presets.  PWM period = base_clock / top_value, so:
 *   125 kHz    : 500 kHz / 4   = 125.000 kHz  (exact)
 *   134.2 kHz  : 16 MHz  / 119 = 134.454 kHz  (+0.19%, inside FDX-B tolerance)
 * 500 kHz cannot divide to 134.2 kHz, hence the base_clock change. */
typedef enum {
    LF_CARRIER_125KHZ = 0,
    LF_CARRIER_134KHZ,
} lf_carrier_t;

/* Re-tunes the PWM carrier.  Safe to call while the radio is stopped;
 * re-inits the PWM instance if it was already initialised. */
void lf_radio_set_carrier(lf_carrier_t carrier);

void lf_125khz_radio_init(void);
void lf_125khz_radio_uninit(void);

void lf_125khz_radio_saadc_enable(lf_adc_callback_t cb);
void lf_125khz_radio_gpiote_enable(void);
void lf_125khz_radio_saadc_disable(void);
void lf_125khz_radio_gpiote_disable(void);

void start_lf_125khz_radio(void);
void stop_lf_125khz_radio(void);
