#ifndef LF_HARDWARE_ABSTRACTION_H
#define LF_HARDWARE_ABSTRACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "nrf_drv_pwm.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_gpiote.h"

#ifdef __cplusplus
extern "C" {
#endif

// LF Hardware Abstraction Layer
// This layer provides a unified interface for LF operations that bridges
// Proxmark3's FPGA-based approach with ChameleonUltra's PWM/GPIO architecture

// ============================================================================
// Signal Generation Abstraction
// ============================================================================

typedef enum {
    LF_MODULATION_ASK,      // Amplitude Shift Keying
    LF_MODULATION_FSK,      // Frequency Shift Keying  
    LF_MODULATION_PSK,      // Phase Shift Keying
    LF_MODULATION_BIPHASE,  // Biphase/Manchester
    LF_MODULATION_NRZ       // Non-Return-to-Zero
} lf_modulation_t;

typedef struct {
    uint32_t carrier_freq;      // Carrier frequency in Hz (typically 125000)
    uint32_t data_rate;         // Data rate in Hz
    lf_modulation_t modulation; // Modulation type
    uint8_t power_level;        // Power level (0-255)
    bool invert_output;         // Invert output signal
} lf_signal_config_t;

typedef struct {
    uint16_t *sequence;         // PWM sequence values
    uint16_t length;            // Sequence length
    uint16_t repeats;           // Number of repeats
    uint32_t end_delay;         // End delay in PWM cycles
} lf_pwm_sequence_t;

// Signal generation functions
int lf_signal_init(void);
int lf_signal_uninit(void);
int lf_signal_configure(const lf_signal_config_t *config);
int lf_signal_start(void);
int lf_signal_stop(void);
int lf_signal_send_sequence(const lf_pwm_sequence_t *sequence);
int lf_signal_send_bits(const uint8_t *bits, uint16_t bit_count, const lf_signal_config_t *config);

// ============================================================================
// Signal Detection Abstraction  
// ============================================================================

typedef enum {
    LF_EDGE_RISING,
    LF_EDGE_FALLING,
    LF_EDGE_BOTH
} lf_edge_type_t;

typedef struct {
    uint32_t timestamp;         // Timestamp of edge in microseconds
    lf_edge_type_t edge_type;   // Type of edge detected
    uint32_t pulse_width;       // Width of pulse in microseconds
} lf_edge_event_t;

typedef struct {
    lf_edge_event_t *events;    // Buffer for edge events
    uint16_t max_events;        // Maximum number of events
    uint16_t event_count;       // Current number of events
    uint32_t timeout_us;        // Timeout in microseconds
    bool overflow;              // Buffer overflow flag
} lf_detection_buffer_t;

typedef void (*lf_detection_callback_t)(const lf_edge_event_t *event);

// Signal detection functions
int lf_detection_init(void);
int lf_detection_uninit(void);
int lf_detection_start(lf_detection_buffer_t *buffer);
int lf_detection_stop(void);
int lf_detection_set_callback(lf_detection_callback_t callback);
int lf_detection_get_events(lf_detection_buffer_t *buffer);
uint32_t lf_detection_get_counter(void);
void lf_detection_clear_counter(void);

// ============================================================================
// Timing Abstraction
// ============================================================================

typedef struct {
    uint32_t start_gap_us;      // Start gap in microseconds
    uint32_t write_gap_us;      // Write gap in microseconds  
    uint32_t bit_0_us;          // Bit 0 duration in microseconds
    uint32_t bit_1_us;          // Bit 1 duration in microseconds
    uint32_t response_timeout_us; // Response timeout in microseconds
} lf_timing_config_t;

// Timing functions
int lf_timing_init(void);
int lf_timing_uninit(void);
int lf_timing_delay_us(uint32_t microseconds);
int lf_timing_delay_ms(uint32_t milliseconds);
uint32_t lf_timing_get_us(void);
uint32_t lf_timing_get_ms(void);
int lf_timing_set_timeout(uint32_t timeout_us);
bool lf_timing_check_timeout(void);

// ============================================================================
// Protocol Helper Functions
// ============================================================================

// Manchester encoding/decoding
int lf_manchester_encode(const uint8_t *data, uint16_t data_bits, uint8_t *encoded, uint16_t *encoded_bits);
int lf_manchester_decode(const uint8_t *encoded, uint16_t encoded_bits, uint8_t *data, uint16_t *data_bits);

// FSK encoding/decoding  
int lf_fsk_encode(const uint8_t *data, uint16_t data_bits, uint8_t fc_high, uint8_t fc_low, 
                  uint8_t *encoded, uint16_t *encoded_bits);
int lf_fsk_decode(const lf_edge_event_t *events, uint16_t event_count, uint8_t fc_high, uint8_t fc_low,
                  uint8_t *data, uint16_t *data_bits);

// PSK encoding/decoding
int lf_psk_encode(const uint8_t *data, uint16_t data_bits, uint8_t carrier_freq, 
                  uint8_t *encoded, uint16_t *encoded_bits);
int lf_psk_decode(const lf_edge_event_t *events, uint16_t event_count, uint8_t carrier_freq,
                  uint8_t *data, uint16_t *data_bits);

// Checksum and validation
uint16_t lf_calculate_crc16(const uint8_t *data, uint16_t length);
uint8_t lf_calculate_parity(const uint8_t *data, uint16_t length);
bool lf_validate_checksum(const uint8_t *data, uint16_t length, uint16_t expected_crc);

// ============================================================================
// Hardware-Specific Adaptations
// ============================================================================

// Proxmark3 compatibility layer
typedef struct {
    uint32_t samples;           // Number of samples
    uint32_t decimation;        // Decimation factor
    bool trigger_threshold;     // Trigger threshold enable
    uint32_t threshold_value;   // Threshold value
} lf_acquisition_config_t;

// Proxmark3-style acquisition functions
int lf_acquire_raw_samples(lf_acquisition_config_t *config, uint8_t *buffer, uint32_t buffer_size);
int lf_acquire_correlation(const uint8_t *pattern, uint16_t pattern_length, uint32_t *correlation);

// Field control functions (equivalent to Proxmark3 FPGA control)
int lf_field_on(void);
int lf_field_off(void);
bool lf_field_is_on(void);
int lf_field_set_power(uint8_t power_level);

// ============================================================================
// Error Codes
// ============================================================================

#define LF_SUCCESS                  0
#define LF_ERROR_INVALID_PARAM     -1
#define LF_ERROR_NOT_INITIALIZED   -2
#define LF_ERROR_HARDWARE_FAILURE  -3
#define LF_ERROR_TIMEOUT           -4
#define LF_ERROR_BUFFER_OVERFLOW   -5
#define LF_ERROR_INVALID_STATE     -6
#define LF_ERROR_NOT_SUPPORTED     -7

#ifdef __cplusplus
}
#endif

#endif // LF_HARDWARE_ABSTRACTION_H

