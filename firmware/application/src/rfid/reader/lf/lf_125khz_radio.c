#include "lf_125khz_radio.h"

#include "lf_reader_data.h"
#include "nrf_gpio.h"
#include "nrfx_clock.h"
#include "nrfx_gpiote.h"
#include "nrfx_ppi.h"
#include "nrfx_pwm.h"
#include "nrfx_saadc.h"
#include "nrfx_timer.h"
#include "rfid_main.h"

#define SAADC_BUF_SIZE (2048)
#define SAADC_BUF_COUNT (2)

static nrf_saadc_value_t samples[SAADC_BUF_SIZE][SAADC_BUF_COUNT];
nrfx_pwm_t m_pwm = NRFX_PWM_INSTANCE(0);
nrf_ppi_channel_t m_ppi_channel;
nrfx_timer_t m_timer_lf_reader = NRFX_TIMER_INSTANCE(2);

static void pwm_init(void);
static void saadc_init(void);
static void timer_counter_init(void);
static void gpiote_init(void);
static void pwm_saadc_sample_ppi_init(void);
static void pwm_timer_counter_ppi_init(void);

// Simple function to provide an index to the next input buffer
// Will simply alernate between 0 and 1 when SAADC_BUF_COUNT is 2
static uint32_t next_free_buf_index(void) {
    static uint32_t buffer_index = -1;
    buffer_index = (buffer_index + 1) % SAADC_BUF_COUNT;
    return buffer_index;
}

// At present, only channel 1 is used, so only one channel can be configured
static nrf_pwm_values_individual_t m_lf_125khz_pwm_seq_val[] = {
    {2, 0, 0, 0},
};

nrf_pwm_sequence_t const m_lf_125khz_pwm_seq_obj = {
    .values.p_individual = m_lf_125khz_pwm_seq_val,
    .length = NRF_PWM_VALUES_LENGTH(m_lf_125khz_pwm_seq_val),
    .repeats = 0,
    .end_delay = 0};

typedef enum {
    LF_125K_RADIO_MODE_NONE,
    LF_125K_RADIO_MODE_SAADC,
    LF_125K_RADIO_MODE_GPIOTE,
} lf_125k_radio_mode_t;
static lf_125k_radio_mode_t m_lf_125k_radio_mode = LF_125K_RADIO_MODE_NONE;

/**@brief Low -frequency reading card decrease along the trigger collection
 * event
 */
static void lf_125khz_gpio_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    // Directly transfer to the event
    gpio_int0_irq_handler();
}

// initialize 125kHz signal PWM modulation (use saadc, FSK)
void lf_125khz_radio_saadc_init(void) {
    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_SAADC) {
        return;
    }

    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_GPIOTE) {
        lf_125khz_radio_uninit();
    }

    pwm_init();
    saadc_init();
    pwm_saadc_sample_ppi_init();
    m_lf_125k_radio_mode = LF_125K_RADIO_MODE_SAADC;
}

// initialize 125kHz signal PWM modulation (use gpiote, ASK)
void lf_125khz_radio_gpiote_init(void) {
    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_GPIOTE) {
        return;
    }

    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_SAADC) {
        lf_125khz_radio_uninit();
    }

    pwm_init();
    timer_counter_init();
    pwm_timer_counter_ppi_init();
    gpiote_init();
    m_lf_125k_radio_mode = LF_125K_RADIO_MODE_GPIOTE;
}

static void gpiote_init(void) {
    nrfx_err_t err_code;

    // The LF collection decline is interrupted, and the GPIO is pulled down
    // by default. The trigger method is triggering
    nrfx_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
    err_code = nrfx_gpiote_in_init(LF_OA_OUT, &in_config, lf_125khz_gpio_handler);
    APP_ERROR_CHECK(err_code);
    nrfx_gpiote_in_event_enable(LF_OA_OUT, true);
}

// Anti -initialization
void lf_125khz_radio_uninit(void) {
    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_NONE) {
        return;
    }

    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_SAADC) {
        nrfx_ppi_channel_free(m_ppi_channel);
        nrfx_saadc_uninit();
    }

    if (m_lf_125k_radio_mode == LF_125K_RADIO_MODE_GPIOTE) {
        nrfx_gpiote_in_event_disable(LF_OA_OUT);
        nrfx_gpiote_in_uninit(LF_OA_OUT);
        nrfx_ppi_channel_free(m_ppi_channel);
        nrfx_timer_uninit(&m_timer_lf_reader);
    }

    nrfx_ppi_free_all();  // nrf_drv_ppi_uninit();
    nrfx_pwm_uninit(&m_pwm);
    m_lf_125k_radio_mode = LF_125K_RADIO_MODE_NONE;
}

/**
 * Start the 125kHz broadcast
 */
void start_lf_125khz_radio(void) {
    nrfx_pwm_simple_playback(&m_pwm, &m_lf_125khz_pwm_seq_obj, 1, NRFX_PWM_FLAG_LOOP);
    TAG_FIELD_LED_ON();
}

/**
 * Close 125kHz RF broadcast
 */
void stop_lf_125khz_radio(void) {
    nrfx_pwm_stop(&m_pwm, true);
    TAG_FIELD_LED_OFF();
}

static void pwm_init(void) {
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG;
    config.output_pins[0] = LF_ANT_DRIVER | NRFX_PWM_PIN_INVERTED;
    for (uint8_t i = 1; i < NRF_PWM_CHANNEL_COUNT; i++) {
        config.output_pins[i] = NRFX_PWM_PIN_NOT_USED;
    }
    config.irq_priority = APP_IRQ_PRIORITY_LOW;
    config.base_clock = (nrf_pwm_clk_t)NRF_PWM_CLK_500kHz;
    config.count_mode = (nrf_pwm_mode_t)NRF_PWM_MODE_UP;
    config.top_value = (uint16_t)4;
    config.load_mode = (nrf_pwm_dec_load_t)NRF_PWM_LOAD_INDIVIDUAL;
    config.step_mode = (nrf_pwm_dec_step_t)NRF_PWM_STEP_AUTO;

    nrfx_err_t err_code = nrfx_pwm_init(&m_pwm, &config, NULL);
    APP_ERROR_CHECK(err_code);
}

static void timer_counter_init(void) {
    nrfx_err_t err_code;

    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    timer_cfg.mode = NRF_TIMER_MODE_COUNTER;

    err_code = nrfx_timer_init(&m_timer_lf_reader, &timer_cfg, NULL);
    APP_ERROR_CHECK(err_code);

    nrfx_timer_enable(&m_timer_lf_reader);
}

static void pwm_timer_counter_ppi_init() {
    nrfx_err_t err_code;

    err_code = nrfx_ppi_channel_alloc(&m_ppi_channel);
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_ppi_channel_assign(
        m_ppi_channel,
        nrfx_pwm_event_address_get(&m_pwm, NRF_PWM_EVENT_PWMPERIODEND),
        nrfx_timer_task_address_get(&m_timer_lf_reader, NRF_TIMER_TASK_COUNT));
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_ppi_channel_enable(m_ppi_channel);
    APP_ERROR_CHECK(err_code);
}

// trigger saadc sample task from pwm
static void pwm_saadc_sample_ppi_init(void) {
    nrfx_err_t err_code;

    err_code = nrfx_ppi_channel_alloc(&m_ppi_channel);
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_ppi_channel_assign(
        m_ppi_channel,
        nrfx_pwm_event_address_get(&m_pwm, NRF_PWM_EVENT_PWMPERIODEND),
        nrf_saadc_task_address_get(NRF_SAADC_TASK_SAMPLE));
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_ppi_channel_enable(m_ppi_channel);
    APP_ERROR_CHECK(err_code);
}

void lf_saadc_event_handler(nrfx_saadc_evt_t const* p_event) {
    if (p_event->type == NRFX_SAADC_EVT_DONE) {
        ret_code_t err_code;
        err_code = nrfx_saadc_buffer_convert(&samples[next_free_buf_index()][0], SAADC_BUF_SIZE);
        APP_ERROR_CHECK(err_code);
        saadc_irq_handler(p_event->data.done.p_buffer, p_event->data.done.size);
    }
}

static void saadc_init(void) {
    nrfx_saadc_uninit();

    ret_code_t err_code;

    nrfx_saadc_config_t config = NRFX_SAADC_DEFAULT_CONFIG;
    err_code = nrfx_saadc_init(&config, lf_saadc_event_handler);
    APP_ERROR_CHECK(err_code);

    nrf_saadc_channel_config_t ch_config = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN5);
    ch_config.acq_time = NRF_SAADC_ACQTIME_5US;
    err_code = nrfx_saadc_channel_init(0, &ch_config);
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_saadc_buffer_convert(&samples[next_free_buf_index()][0], SAADC_BUF_SIZE);
    APP_ERROR_CHECK(err_code);

    err_code = nrfx_saadc_buffer_convert(&samples[next_free_buf_index()][0], SAADC_BUF_SIZE);
    APP_ERROR_CHECK(err_code);
}