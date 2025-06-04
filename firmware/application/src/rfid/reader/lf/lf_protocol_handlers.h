#ifndef LF_PROTOCOL_HANDLERS_H
#define LF_PROTOCOL_HANDLERS_H

#include <stdint.h>
#include <stdbool.h>
#include "lf_hardware_abstraction.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Protocol Handler Return Codes
// ============================================================================

#define LF_PROTOCOL_SUCCESS             0
#define LF_PROTOCOL_ERROR_INVALID_PARAM -1
#define LF_PROTOCOL_ERROR_NOT_FOUND     -2
#define LF_PROTOCOL_ERROR_TIMEOUT       -3
#define LF_PROTOCOL_ERROR_CHECKSUM      -4
#define LF_PROTOCOL_ERROR_HARDWARE      -5
#define LF_PROTOCOL_ERROR_BUFFER_FULL   -6

// ============================================================================
// EM410x Protocol Handler
// ============================================================================

typedef struct {
    uint32_t id_hi;                 // High 32 bits of ID
    uint64_t id_lo;                 // Low 64 bits of ID
    uint8_t format;                 // Format type (1=standard, 2=long, 4=extended)
    bool valid;                     // ID validity flag
    uint32_t clock;                 // Detected clock rate
    uint32_t start_idx;             // Start index in buffer
} lf_em410x_result_t;

typedef struct {
    uint32_t timeout_ms;            // Read timeout in milliseconds
    uint32_t max_errors;            // Maximum allowed errors
    bool verbose;                   // Verbose output flag
    uint8_t amplitude_threshold;    // Amplitude threshold for detection
} lf_em410x_config_t;

// EM410x functions
int lf_em410x_read(lf_em410x_result_t *result, const lf_em410x_config_t *config);
int lf_em410x_simulate(uint32_t id_hi, uint64_t id_lo, uint32_t duration_ms);
int lf_em410x_clone_to_t55xx(uint32_t id_hi, uint64_t id_lo, uint8_t clock_rate);
int lf_em410x_decode_buffer(const uint8_t *buffer, size_t buffer_size, lf_em410x_result_t *result);
int lf_em410x_encode_id(uint32_t id_hi, uint64_t id_lo, uint8_t *encoded_data, size_t *encoded_size);

// ============================================================================
// T55xx Protocol Handler
// ============================================================================

typedef struct {
    uint32_t data;                  // Block data
    uint8_t block;                  // Block number (0-7)
    bool valid;                     // Data validity flag
    uint32_t raw_data;              // Raw data before processing
} lf_t55xx_block_t;

typedef struct {
    uint32_t data_rate;             // Data rate (RF/x)
    uint8_t modulation;             // Modulation type (0=FSK, 1=ASK, 2=PSK, 3=NRZ, 4=Biphase)
    bool psk_cf;                    // PSK carrier frequency (0=RF/2, 1=RF/8)
    bool aor;                       // Answer on request
    bool otp;                       // One time pad
    uint8_t max_block;              // Maximum block number
    uint32_t pwd;                   // Password (if protected)
    bool pwd_mode;                  // Password mode enabled
    bool st_sequence;               // Sequence terminator
    bool inverse_data;              // Inverse data
    uint8_t bit_rate;               // Bit rate
} lf_t55xx_config_t;

typedef struct {
    uint32_t timeout_ms;            // Operation timeout
    uint8_t downlink_mode;          // Downlink mode (0=fixed, 1=long leading, 2=leading zero, 3=1 of 4)
    bool test_mode;                 // Test mode flag
    uint32_t start_gap;             // Start gap timing
    uint32_t write_gap;             // Write gap timing
    uint32_t write_0;               // Write 0 timing
    uint32_t write_1;               // Write 1 timing
} lf_t55xx_timing_t;

// T55xx functions
int lf_t55xx_read_block(uint8_t block, lf_t55xx_block_t *result, const lf_t55xx_timing_t *timing);
int lf_t55xx_write_block(uint8_t block, uint32_t data, uint32_t password, const lf_t55xx_timing_t *timing);
int lf_t55xx_read_config(lf_t55xx_config_t *config, const lf_t55xx_timing_t *timing);
int lf_t55xx_write_config(const lf_t55xx_config_t *config, uint32_t password, const lf_t55xx_timing_t *timing);
int lf_t55xx_detect(lf_t55xx_config_t *config);
int lf_t55xx_wakeup(uint32_t password, const lf_t55xx_timing_t *timing);
int lf_t55xx_reset_read(const lf_t55xx_timing_t *timing);

// ============================================================================
// HID Prox Protocol Handler
// ============================================================================

typedef struct {
    uint32_t facility_code;         // Facility code
    uint32_t card_number;           // Card number
    uint32_t id_hi;                 // High 32 bits of full ID
    uint32_t id_lo;                 // Low 32 bits of full ID
    uint8_t format_length;          // Format length in bits
    bool valid;                     // ID validity flag
    uint8_t format_type;            // Format type (26-bit, 35-bit, etc.)
} lf_hid_result_t;

typedef struct {
    uint32_t timeout_ms;            // Read timeout
    uint8_t fc_high;                // FSK high frequency
    uint8_t fc_low;                 // FSK low frequency
    uint8_t clock_rate;             // Clock rate
    bool long_format;               // Long format support
} lf_hid_config_t;

// HID Prox functions
int lf_hid_read(lf_hid_result_t *result, const lf_hid_config_t *config);
int lf_hid_simulate(uint32_t facility_code, uint32_t card_number, uint8_t format_length, uint32_t duration_ms);
int lf_hid_clone_to_t55xx(uint32_t facility_code, uint32_t card_number, uint8_t format_length);
int lf_hid_decode_26bit(uint32_t id_hi, uint32_t id_lo, uint32_t *facility_code, uint32_t *card_number);
int lf_hid_encode_26bit(uint32_t facility_code, uint32_t card_number, uint32_t *id_hi, uint32_t *id_lo);

// ============================================================================
// Indala Protocol Handler
// ============================================================================

typedef struct {
    uint64_t id;                    // Indala ID
    uint8_t id_length;              // ID length in bits
    bool valid;                     // ID validity flag
    uint8_t format_type;            // Format type
    uint32_t raw_data[4];           // Raw data (up to 128 bits)
} lf_indala_result_t;

typedef struct {
    uint32_t timeout_ms;            // Read timeout
    uint8_t carrier_freq;           // PSK carrier frequency
    uint8_t clock_rate;             // Clock rate
    bool long_format;               // Long format support
} lf_indala_config_t;

// Indala functions
int lf_indala_read(lf_indala_result_t *result, const lf_indala_config_t *config);
int lf_indala_simulate(uint64_t id, uint8_t id_length, uint32_t duration_ms);
int lf_indala_clone_to_t55xx(uint64_t id, uint8_t id_length);
int lf_indala_decode_psk(const uint8_t *buffer, size_t buffer_size, lf_indala_result_t *result);

// ============================================================================
// Generic LF Scanner
// ============================================================================

typedef enum {
    LF_PROTOCOL_UNKNOWN,
    LF_PROTOCOL_EM410X,
    LF_PROTOCOL_T55XX,
    LF_PROTOCOL_HID_PROX,
    LF_PROTOCOL_INDALA,
    LF_PROTOCOL_AWID,
    LF_PROTOCOL_IOPROX,
    LF_PROTOCOL_FDXB
} lf_protocol_type_t;

typedef struct {
    lf_protocol_type_t protocol;    // Detected protocol
    union {
        lf_em410x_result_t em410x;
        lf_hid_result_t hid;
        lf_indala_result_t indala;
    } data;
    uint32_t signal_strength;       // Signal strength indicator
    uint32_t clock_rate;            // Detected clock rate
    bool valid;                     // Detection validity
} lf_scan_result_t;

typedef struct {
    uint32_t scan_time_ms;          // Total scan time
    bool scan_all_protocols;        // Scan all protocols or stop at first
    uint8_t signal_threshold;       // Minimum signal threshold
    bool verbose;                   // Verbose output
} lf_scan_config_t;

// Scanner functions
int lf_scan_auto(lf_scan_result_t *results, uint8_t max_results, uint8_t *result_count, const lf_scan_config_t *config);
int lf_scan_protocol(lf_protocol_type_t protocol, lf_scan_result_t *result, const lf_scan_config_t *config);
const char *lf_protocol_name(lf_protocol_type_t protocol);

// ============================================================================
// Utility Functions
// ============================================================================

// Parity and checksum functions
uint8_t lf_calculate_em410x_parity(uint32_t data);
bool lf_validate_em410x_parity(uint32_t data, uint8_t parity);
uint16_t lf_calculate_hid_checksum(uint32_t id_hi, uint32_t id_lo);
bool lf_validate_hid_checksum(uint32_t id_hi, uint32_t id_lo, uint16_t checksum);

// Format conversion functions
int lf_convert_facility_card_to_hid(uint32_t facility, uint32_t card, uint32_t *id_hi, uint32_t *id_lo);
int lf_convert_hid_to_facility_card(uint32_t id_hi, uint32_t id_lo, uint32_t *facility, uint32_t *card);

// Signal analysis functions
int lf_analyze_signal_quality(const lf_edge_event_t *events, uint16_t event_count, uint8_t *quality_score);
int lf_detect_clock_rate(const lf_edge_event_t *events, uint16_t event_count, uint32_t *clock_rate);
int lf_detect_modulation_type(const lf_edge_event_t *events, uint16_t event_count, lf_modulation_t *modulation);

#ifdef __cplusplus
}
#endif

#endif // LF_PROTOCOL_HANDLERS_H

