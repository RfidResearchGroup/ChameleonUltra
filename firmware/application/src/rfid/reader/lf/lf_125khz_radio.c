#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_pwm.h"
#include "nrf_drv_clock.h"
#include "nrf_gpio.h"
#include "nrf_drv_gpiote.h"

#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "rfid_main.h"


nrf_drv_pwm_t m_pwm = NRF_DRV_PWM_INSTANCE(0);
nrf_ppi_channel_t m_ppi_channel1;
nrfx_timer_t m_timer_lf_reader = NRFX_TIMER_INSTANCE(2);

// At present, only channel 1 is used, so only one channel can be configured
nrf_pwm_values_individual_t m_lf_125khz_pwm_seq_val[] = { { 2, 0, 0, 0}, };
nrf_pwm_sequence_t const m_lf_125khz_pwm_seq_obj = {
    .values.p_individual = m_lf_125khz_pwm_seq_val,
    .length              = NRF_PWM_VALUES_LENGTH(m_lf_125khz_pwm_seq_val),
    .repeats             = 0,
    .end_delay           = 0
};
static bool m_is_125khz_radio_init = false;


/**@brief Low -frequency reading card decrease along the trigger collection event
 */
static void lf_125khz_gpio_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    // Directly transfer to the event
    GPIO_INT0_IRQHandler();
}

// Initialize 125kHz signal PWM modulation
void lf_125khz_radio_init(void) {
    nrfx_err_t err_code;

    if (!m_is_125khz_radio_init) {
        m_is_125khz_radio_init = true;

        // ******************************************************************

        // Configure pwm
        nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG;
        config.output_pins[0] = LF_ANT_DRIVER | NRF_DRV_PWM_PIN_INVERTED;
        for (uint8_t i = 1; i < NRF_PWM_CHANNEL_COUNT; i++) {
            config.output_pins[i] = NRFX_PWM_PIN_NOT_USED;
        }
        config.irq_priority = APP_IRQ_PRIORITY_LOW;
        config.base_clock = (nrf_pwm_clk_t)NRF_PWM_CLK_500kHz;
        config.count_mode = (nrf_pwm_mode_t)NRF_PWM_MODE_UP;
        config.top_value = (uint16_t)4;
        config.load_mode = (nrf_pwm_dec_load_t)NRF_PWM_LOAD_INDIVIDUAL;
        config.step_mode = (nrf_pwm_dec_step_t)NRF_PWM_STEP_AUTO;

        // Initialization PWM
        err_code = nrfx_pwm_init(&m_pwm, &config, NULL);
        APP_ERROR_CHECK(err_code);

        // ******************************************************************

        // Define the timer configuration structure, and use the default configuration parameter to initialize the structure
        nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
        timer_cfg.mode = NRF_TIMER_MODE_COUNTER;    // Use the counter mode

        // Initialized timer
        err_code = nrfx_timer_init(&m_timer_lf_reader, &timer_cfg, NULL);
        APP_ERROR_CHECK(err_code);

        // Enable timer
        nrfx_timer_enable(&m_timer_lf_reader);

        // ******************************************************************

        // Initialized PPI
        err_code = nrf_drv_ppi_init();
        APP_ERROR_CHECK(err_code);

        err_code = nrf_drv_ppi_channel_alloc(&m_ppi_channel1);
        APP_ERROR_CHECK(err_code);

        err_code = nrf_drv_ppi_channel_assign(m_ppi_channel1, nrf_drv_pwm_event_address_get(&m_pwm, NRF_PWM_EVENT_PWMPERIODEND), nrf_drv_timer_task_address_get(&m_timer_lf_reader, NRF_TIMER_TASK_COUNT));
        APP_ERROR_CHECK(err_code);

        // Enable both configured PPI channels
        err_code = nrf_drv_ppi_channel_enable(m_ppi_channel1);
        APP_ERROR_CHECK(err_code);

        // ******************************************************************

        // The LF collection decline is interrupted, and the GPIO is pulled down by default. The trigger method is triggering
        nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
        err_code = nrf_drv_gpiote_in_init(LF_OA_OUT, &in_config, lf_125khz_gpio_handler);
        APP_ERROR_CHECK(err_code);
        nrf_drv_gpiote_in_event_enable(LF_OA_OUT, true);

        // ******************************************************************
    }
}

// Anti -initialization
void lf_125khz_radio_uninit(void) {
    if (m_is_125khz_radio_init) {
        m_is_125khz_radio_init = false;
        nrf_drv_gpiote_in_event_disable(LF_OA_OUT);
        nrf_drv_gpiote_in_uninit(LF_OA_OUT);
        nrf_drv_ppi_channel_free(m_ppi_channel1);
        nrf_drv_ppi_uninit();
        nrfx_timer_uninit(&m_timer_lf_reader);
        nrfx_pwm_uninit(&m_pwm);
    }
}

/**
 * Start the 125kHz broadcast
 */
void start_lf_125khz_radio(void) {
    nrf_drv_pwm_simple_playback(&m_pwm, &m_lf_125khz_pwm_seq_obj, 1, NRF_DRV_PWM_FLAG_LOOP);
}

/**
 * Close 125kHz RF broadcast
 */
void stop_lf_125khz_radio(void) {
    nrf_drv_pwm_stop(&m_pwm, true);
}
