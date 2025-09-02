#include "lf_reader_data.h"

#include "nrfx_timer.h"

RIO_CALLBACK_S RIO_callback;
extern nrfx_timer_t m_pwm_timer_counter;

// Register recovery function
void register_rio_callback(RIO_CALLBACK_S P) { RIO_callback = P; }

void unregister_rio_callback(void) { RIO_callback = NULL; }

// GPIO interrupt is the RIO pin
void gpio_int0_irq_handler(void)
{
    if (RIO_callback != NULL) {
        RIO_callback();
    }
}

// Get the value of the counter
uint32_t get_lf_counter_value(void) { return nrfx_timer_capture(&m_pwm_timer_counter, NRF_TIMER_CC_CHANNEL1); }

// Clear the value of the counter
void clear_lf_counter_value(void) { nrfx_timer_clear(&m_pwm_timer_counter); }
