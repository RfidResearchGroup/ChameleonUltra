#include "lf_reader_data.h"

#include "nrf_drv_timer.h"

RIO_CALLBACK_S RIO_callback;
SAADC_CALLBACK_S SAADC_callback;

// Register recovery function
void register_rio_callback(RIO_CALLBACK_S P) {
    RIO_callback = P;
}

void unregister_rio_callback(void) {
    RIO_callback = NULL;
}

// Register recovery function
void register_saadc_callback(SAADC_CALLBACK_S P) {
    SAADC_callback = P;
}

void unregister_saadc_callback(void) {
    SAADC_callback = NULL;
}

// GPIO interrupt is the RIO pin
void gpio_int0_irq_handler(void) {
    if (RIO_callback != NULL) {
        RIO_callback();
    }
}

void saadc_irq_handler(int16_t *val, size_t size) {
    if (SAADC_callback != NULL) {
        SAADC_callback(val, size);
    }
}

extern nrfx_timer_t m_timer_lf_reader;

// Get the value of the counter
uint32_t get_lf_counter_value(void) {
    return nrfx_timer_capture(&m_timer_lf_reader, NRF_TIMER_CC_CHANNEL1);
}

// Clear the value of the counter
void clear_lf_counter_value(void) { nrfx_timer_clear(&m_timer_lf_reader); }
