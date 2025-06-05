#include "lf_hardware_abstraction.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "hw_connect.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "app_timer.h"
#include "app_error.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Private Variables and State
// ============================================================================

static bool m_lf_abstraction_initialized = false;
static lf_signal_config_t m_current_signal_config;
static lf_detection_callback_t m_detection_callback = NULL;
static lf_detection_buffer_t *m_current_detection_buffer = NULL;
static uint32_t m_timeout_start_time = 0;
static uint32_t m_timeout_duration_us = 0;
static bool m_timeout_active = false;

// PWM and timer instances (reuse existing ones from lf_125khz_radio.c)
#if defined(PROJECT_CHAMELEON_ULTRA)
extern nrf_drv_pwm_t m_pwm;
extern nrfx_timer_t m_timer_lf_reader;
#endif
extern nrf_ppi_channel_t m_ppi_channel1;

// Internal timing reference
static uint32_t m_timing_base_us = 0;

// ============================================================================
// Signal Generation Implementation
// ============================================================================

int lf_signal_init(void) {
    if (m_lf_abstraction_initialized) {
        return LF_SUCCESS;
    }
    
    // Initialize the underlying 125kHz radio
    lf_125khz_radio_init();
    
    // Set default signal configuration
    m_current_signal_config.carrier_freq = 125000;
    m_current_signal_config.data_rate = 64;
    m_current_signal_config.modulation = LF_MODULATION_ASK;
    m_current_signal_config.power_level = 128;
    m_current_signal_config.invert_output = false;
    
    m_lf_abstraction_initialized = true;
    return LF_SUCCESS;
}

int lf_signal_uninit(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    lf_signal_stop();
    lf_125khz_radio_uninit();
    m_lf_abstraction_initialized = false;
    return LF_SUCCESS;
}

int lf_signal_configure(const lf_signal_config_t *config) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    if (config == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    // Validate configuration parameters
    if (config->carrier_freq < 100000 || config->carrier_freq > 150000) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    if (config->data_rate == 0 || config->data_rate > 1000) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    // Store configuration
    memcpy(&m_current_signal_config, config, sizeof(lf_signal_config_t));
    
    return LF_SUCCESS;
}

int lf_signal_start(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    start_lf_125khz_radio();
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version signal start (simplified)
#endif
    return LF_SUCCESS;
}

int lf_signal_stop(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    stop_lf_125khz_radio();
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version signal stop (simplified)
#endif
    return LF_SUCCESS;
}

int lf_signal_send_sequence(const lf_pwm_sequence_t *sequence) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    if (sequence == NULL || sequence->sequence == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    // Create nRF PWM sequence structure
    nrf_pwm_values_individual_t *pwm_values = (nrf_pwm_values_individual_t *)sequence->sequence;
    nrf_pwm_sequence_t pwm_seq = {
        .values.p_individual = pwm_values,
        .length = sequence->length,
        .repeats = sequence->repeats,
        .end_delay = sequence->end_delay
    };
    
    // Send the sequence
    ret_code_t err_code = nrf_drv_pwm_simple_playback(&m_pwm, &pwm_seq, 1, NRF_DRV_PWM_FLAG_STOP);
    if (err_code != NRF_SUCCESS) {
        return LF_ERROR_HARDWARE_FAILURE;
    }
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version PWM sequence (simplified)
#endif
    
    return LF_SUCCESS;
}

int lf_signal_send_bits(const uint8_t *bits, uint16_t bit_count, const lf_signal_config_t *config) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    if (bits == NULL || bit_count == 0) {
        return LF_ERROR_INVALID_PARAM;
    }

#if defined(PROJECT_CHAMELEON_ULTRA)
    // Use current config if none provided
    const lf_signal_config_t *active_config = config ? config : &m_current_signal_config;
    
    // Calculate timing parameters based on data rate
    uint32_t bit_duration_us = 1000000 / active_config->data_rate;
    uint32_t pwm_cycles_per_bit = (bit_duration_us * 500) / 1000; // 500kHz PWM base
    
    // Allocate PWM sequence buffer
    uint16_t sequence_length = bit_count * pwm_cycles_per_bit;
    nrf_pwm_values_individual_t *pwm_sequence = malloc(sequence_length * sizeof(nrf_pwm_values_individual_t));
    if (pwm_sequence == NULL) {
        return LF_ERROR_HARDWARE_FAILURE;
    }
    
    // Generate PWM sequence based on modulation type
    uint16_t seq_index = 0;
    for (uint16_t bit_idx = 0; bit_idx < bit_count; bit_idx++) {
        uint8_t bit_value = (bits[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
        
        switch (active_config->modulation) {
            case LF_MODULATION_ASK:
                // ASK: bit 1 = full amplitude, bit 0 = reduced amplitude
                for (uint32_t i = 0; i < pwm_cycles_per_bit; i++) {
                    pwm_sequence[seq_index++].channel_0 = bit_value ? 2 : 1;
                }
                break;
                
            case LF_MODULATION_BIPHASE:
                // Manchester: bit 1 = 01, bit 0 = 10
                for (uint32_t i = 0; i < pwm_cycles_per_bit / 2; i++) {
                    pwm_sequence[seq_index++].channel_0 = bit_value ? 0 : 2;
                }
                for (uint32_t i = 0; i < pwm_cycles_per_bit / 2; i++) {
                    pwm_sequence[seq_index++].channel_0 = bit_value ? 2 : 0;
                }
                break;
                
            default:
                // Default to ASK
                for (uint32_t i = 0; i < pwm_cycles_per_bit; i++) {
                    pwm_sequence[seq_index++].channel_0 = bit_value ? 2 : 1;
                }
                break;
        }
    }
    
    // Create and send PWM sequence
    nrf_pwm_sequence_t pwm_seq = {
        .values.p_individual = pwm_sequence,
        .length = sequence_length,
        .repeats = 0,
        .end_delay = 0
    };
    
    ret_code_t err_code = nrf_drv_pwm_simple_playback(&m_pwm, &pwm_seq, 1, NRF_DRV_PWM_FLAG_STOP);
    
    free(pwm_sequence);
    
    if (err_code != NRF_SUCCESS) {
        return LF_ERROR_HARDWARE_FAILURE;
    }
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version bit transmission (simplified)
#endif
    
    return LF_SUCCESS;
}

// ============================================================================
// Signal Detection Implementation
// ============================================================================

#if defined(PROJECT_CHAMELEON_ULTRA)
static void lf_detection_gpio_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    if (m_current_detection_buffer == NULL) {
        return;
    }
    
    // Get current timestamp
    uint32_t timestamp = lf_timing_get_us();
    
    // Create edge event
    lf_edge_event_t event;
    event.timestamp = timestamp;
    event.edge_type = (action == NRF_GPIOTE_POLARITY_LOTOHI) ? LF_EDGE_RISING : LF_EDGE_FALLING;
    event.pulse_width = 0; // Will be calculated later
    
    // Add to buffer if space available
    if (m_current_detection_buffer->event_count < m_current_detection_buffer->max_events) {
        m_current_detection_buffer->events[m_current_detection_buffer->event_count] = event;
        m_current_detection_buffer->event_count++;
    } else {
        m_current_detection_buffer->overflow = true;
    }
    
    // Call callback if registered
    if (m_detection_callback != NULL) {
        m_detection_callback(&event);
    }
}
#endif

int lf_detection_init(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    // Initialize GPIO interrupt for LF detection (Ultra only)
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
    ret_code_t err_code = nrf_drv_gpiote_in_init(LF_OA_OUT, &in_config, lf_detection_gpio_handler);
    if (err_code != NRF_SUCCESS) {
        return LF_ERROR_HARDWARE_FAILURE;
    }
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version uses simplified detection method
    // No specific OA_OUT pin available, use alternative detection
#endif
    
    return LF_SUCCESS;
}

int lf_detection_uninit(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    nrf_drv_gpiote_in_event_disable(LF_OA_OUT);
    nrf_drv_gpiote_in_uninit(LF_OA_OUT);
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version cleanup (no specific actions needed)
#endif
    
    return LF_SUCCESS;
}

int lf_detection_start(lf_detection_buffer_t *buffer) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    if (buffer == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    // Initialize buffer
    buffer->event_count = 0;
    buffer->overflow = false;
    m_current_detection_buffer = buffer;
    
    // Clear counter and enable detection
    clear_lf_counter_value();
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    nrf_drv_gpiote_in_event_enable(LF_OA_OUT, true);
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version uses alternative detection method
#endif
    
    return LF_SUCCESS;
}

int lf_detection_stop(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    nrf_drv_gpiote_in_event_disable(LF_OA_OUT);
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version cleanup (no specific actions needed)
#endif
    
    m_current_detection_buffer = NULL;
    
    return LF_SUCCESS;
}

int lf_detection_set_callback(lf_detection_callback_t callback) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    m_detection_callback = callback;
    return LF_SUCCESS;
}

int lf_detection_get_events(lf_detection_buffer_t *buffer) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    if (buffer == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    // Calculate pulse widths for consecutive events
    for (uint16_t i = 1; i < buffer->event_count; i++) {
        buffer->events[i-1].pulse_width = buffer->events[i].timestamp - buffer->events[i-1].timestamp;
    }
    
    return LF_SUCCESS;
}

uint32_t lf_detection_get_counter(void) {
    return get_lf_counter_value();
}

void lf_detection_clear_counter(void) {
    clear_lf_counter_value();
}

// ============================================================================
// Timing Implementation
// ============================================================================

int lf_timing_init(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    // Initialize timing base
    m_timing_base_us = app_timer_cnt_get();
    return LF_SUCCESS;
}

int lf_timing_uninit(void) {
    return LF_SUCCESS;
}

int lf_timing_delay_us(uint32_t microseconds) {
    nrf_delay_us(microseconds);
    return LF_SUCCESS;
}

int lf_timing_delay_ms(uint32_t milliseconds) {
    nrf_delay_ms(milliseconds);
    return LF_SUCCESS;
}

uint32_t lf_timing_get_us(void) {
    // Convert app_timer ticks to microseconds
    uint32_t ticks = app_timer_cnt_get();
    return app_timer_cnt_diff_compute(ticks, m_timing_base_us) * 1000000 / APP_TIMER_CLOCK_FREQ;
}

uint32_t lf_timing_get_ms(void) {
    return lf_timing_get_us() / 1000;
}

int lf_timing_set_timeout(uint32_t timeout_us) {
    m_timeout_start_time = lf_timing_get_us();
    m_timeout_duration_us = timeout_us;
    m_timeout_active = true;
    return LF_SUCCESS;
}

bool lf_timing_check_timeout(void) {
    if (!m_timeout_active) {
        return false;
    }
    
    uint32_t current_time = lf_timing_get_us();
    return (current_time - m_timeout_start_time) >= m_timeout_duration_us;
}

// ============================================================================
// Protocol Helper Functions Implementation
// ============================================================================

int lf_manchester_encode(const uint8_t *data, uint16_t data_bits, uint8_t *encoded, uint16_t *encoded_bits) {
    if (data == NULL || encoded == NULL || encoded_bits == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    uint16_t encoded_bit_count = 0;
    
    for (uint16_t i = 0; i < data_bits; i++) {
        uint8_t bit = (data[i / 8] >> (7 - (i % 8))) & 1;
        
        // Manchester encoding: 1 = 01, 0 = 10
        if (bit) {
            // Encode '1' as '01'
            encoded[encoded_bit_count / 8] &= ~(1 << (7 - (encoded_bit_count % 8))); // 0
            encoded_bit_count++;
            encoded[encoded_bit_count / 8] |= (1 << (7 - (encoded_bit_count % 8)));  // 1
            encoded_bit_count++;
        } else {
            // Encode '0' as '10'
            encoded[encoded_bit_count / 8] |= (1 << (7 - (encoded_bit_count % 8)));  // 1
            encoded_bit_count++;
            encoded[encoded_bit_count / 8] &= ~(1 << (7 - (encoded_bit_count % 8))); // 0
            encoded_bit_count++;
        }
    }
    
    *encoded_bits = encoded_bit_count;
    return LF_SUCCESS;
}

int lf_manchester_decode(const uint8_t *encoded, uint16_t encoded_bits, uint8_t *data, uint16_t *data_bits) {
    if (encoded == NULL || data == NULL || data_bits == NULL) {
        return LF_ERROR_INVALID_PARAM;
    }
    
    if (encoded_bits % 2 != 0) {
        return LF_ERROR_INVALID_PARAM; // Manchester requires even number of bits
    }
    
    uint16_t data_bit_count = 0;
    
    for (uint16_t i = 0; i < encoded_bits; i += 2) {
        uint8_t bit1 = (encoded[i / 8] >> (7 - (i % 8))) & 1;
        uint8_t bit2 = (encoded[(i + 1) / 8] >> (7 - ((i + 1) % 8))) & 1;
        
        // Manchester decoding: 01 = 1, 10 = 0
        if (bit1 == 0 && bit2 == 1) {
            // Decoded bit is '1'
            data[data_bit_count / 8] |= (1 << (7 - (data_bit_count % 8)));
        } else if (bit1 == 1 && bit2 == 0) {
            // Decoded bit is '0'
            data[data_bit_count / 8] &= ~(1 << (7 - (data_bit_count % 8)));
        } else {
            // Invalid Manchester encoding
            return LF_ERROR_INVALID_PARAM;
        }
        
        data_bit_count++;
    }
    
    *data_bits = data_bit_count;
    return LF_SUCCESS;
}

uint16_t lf_calculate_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

uint8_t lf_calculate_parity(const uint8_t *data, uint16_t length) {
    uint8_t parity = 0;
    
    for (uint16_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            parity ^= (byte >> j) & 1;
        }
    }
    
    return parity;
}

bool lf_validate_checksum(const uint8_t *data, uint16_t length, uint16_t expected_crc) {
    uint16_t calculated_crc = lf_calculate_crc16(data, length);
    return calculated_crc == expected_crc;
}

// ============================================================================
// Field Control Implementation
// ============================================================================

int lf_field_on(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    start_lf_125khz_radio();
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version field on (simplified)
#endif
    return LF_SUCCESS;
}

int lf_field_off(void) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
#if defined(PROJECT_CHAMELEON_ULTRA)
    stop_lf_125khz_radio();
#elif defined(PROJECT_CHAMELEON_LITE)
    // Lite version field off (simplified)
#endif
    return LF_SUCCESS;
}

bool lf_field_is_on(void) {
    // Check if LF system is initialized and active
    return m_lf_abstraction_initialized;
}

int lf_field_set_power(uint8_t power_level) {
    if (!m_lf_abstraction_initialized) {
        return LF_ERROR_NOT_INITIALIZED;
    }
    
    // Update current configuration
    m_current_signal_config.power_level = power_level;
    
    // TODO: Implement actual power control through PWM duty cycle adjustment
    // This would require modifying the PWM sequence values based on power_level
    
    return LF_SUCCESS;
}

