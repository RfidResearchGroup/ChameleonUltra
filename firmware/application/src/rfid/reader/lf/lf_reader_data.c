#include "lf_reader_data.h"
#include "nrf_drv_timer.h"


RIO_CALLBACK_S RIO_callback;                // Create instance
uint8_t RIO_callback_state;                 // Record status


void register_rio_callback(RIO_CALLBACK_S P) { // Register recovery function
    RIO_callback = P;
    RIO_callback_state = 1;
}

void blank_function(void) {
    // This is an empty function,
    // Nothing to do
}

void unregister_rio_callback(void) {
    RIO_callback_state = 0;
    RIO_callback = blank_function;
}

// GPIO interrupt is the RIO pin
void GPIO_INT0_IRQHandler(void) {
    if (RIO_callback_state == 1) {
        RIO_callback();
    }
}


#if defined(PROJECT_CHAMELEON_ULTRA)
extern nrfx_timer_t m_timer_lf_reader;
#endif

// Get the value of the counter
uint32_t get_lf_counter_value(void) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    return nrfx_timer_capture(&m_timer_lf_reader, NRF_TIMER_CC_CHANNEL1);
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version counter (simplified)
    return 0;
#endif
}

// Clear the value of the counter
void clear_lf_counter_value(void) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    nrfx_timer_clear(&m_timer_lf_reader);
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version counter clear (simplified)
#endif
}
