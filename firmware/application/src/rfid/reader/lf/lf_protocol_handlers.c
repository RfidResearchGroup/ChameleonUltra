#include "lf_protocol_handlers.h"
#include "lf_hardware_abstraction.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Private Helper Functions
// ============================================================================

// Adapted from Proxmark3's lfdemod.c - EM410x parity removal
static size_t remove_em410x_parity(uint8_t *bits, size_t start_idx, size_t *size, 
                                   bool *valid_short, bool *valid_short_extended, bool *valid_long) {
    *valid_short = false;
    *valid_short_extended = false;
    *valid_long = false;
    
    if (*size < 64) return 0;
    
    size_t bit_idx = start_idx;
    size_t output_idx = 0;
    uint8_t output_bits[128] = {0};
    
    // Process 4-bit groups with parity
    for (int group = 0; group < 10; group++) {
        if (bit_idx + 5 > *size) break;
        
        // Extract 4 data bits + 1 parity bit
        uint8_t data_bits = 0;
        uint8_t parity_bit = bits[bit_idx + 4];
        
        for (int i = 0; i < 4; i++) {
            data_bits |= (bits[bit_idx + i] << (3 - i));
        }
        
        // Calculate expected parity (even parity)
        uint8_t calculated_parity = 0;
        for (int i = 0; i < 4; i++) {
            calculated_parity ^= (data_bits >> i) & 1;
        }
        
        // Check parity
        if (calculated_parity != parity_bit) {
            return 0; // Parity error
        }
        
        // Store data bits
        for (int i = 0; i < 4; i++) {
            output_bits[output_idx++] = (data_bits >> (3 - i)) & 1;
        }
        
        bit_idx += 5;
    }
    
    // Check column parity
    for (int col = 0; col < 4; col++) {
        if (bit_idx >= *size) break;
        
        uint8_t column_parity = 0;
        for (int row = 0; row < 10; row++) {
            column_parity ^= output_bits[row * 4 + col];
        }
        
        if (column_parity != bits[bit_idx++]) {
            return 0; // Column parity error
        }
    }
    
    // Copy output back to input buffer
    memcpy(bits, output_bits, output_idx);
    
    if (output_idx == 40) {
        *valid_short = true;
    } else if (output_idx == 64) {
        *valid_long = true;
    }
    
    return output_idx;
}

// Adapted from Proxmark3's lfdemod.c - ASK demodulation
static int ask_demod(uint8_t *bits, size_t *size, int *clock, int *invert, int max_err) {
    if (*size < 100) return -1;
    
    // Simplified ASK demodulation
    // In a real implementation, this would include:
    // - Amplitude threshold detection
    // - Clock recovery
    // - Error correction
    // - Noise filtering
    
    // For now, implement basic threshold-based demodulation
    uint8_t threshold = 128; // Mid-point threshold
    int errors = 0;
    
    for (size_t i = 0; i < *size; i++) {
        if (bits[i] > threshold) {
            bits[i] = 1;
        } else {
            bits[i] = 0;
        }
    }
    
    // Detect clock if not provided
    if (*clock == 0) {
        *clock = 64; // Default clock
    }
    
    return errors;
}

// Adapted from Proxmark3's lfdemod.c - Manchester decoding
static int manchester_decode_buffer(uint8_t *bits, size_t *size) {
    if (*size < 2) return -1;
    
    size_t output_idx = 0;
    uint8_t output_bits[*size / 2];
    
    for (size_t i = 0; i < *size - 1; i += 2) {
        uint8_t bit1 = bits[i];
        uint8_t bit2 = bits[i + 1];
        
        // Manchester decoding: 01 = 1, 10 = 0
        if (bit1 == 0 && bit2 == 1) {
            output_bits[output_idx++] = 1;
        } else if (bit1 == 1 && bit2 == 0) {
            output_bits[output_idx++] = 0;
        } else {
            // Invalid Manchester encoding - try to recover
            output_bits[output_idx++] = bit1; // Use first bit as fallback
        }
    }
    
    memcpy(bits, output_bits, output_idx);
    *size = output_idx;
    
    return 0;
}

// Convert bit array to bytes
static uint32_t bits_to_uint32(const uint8_t *bits, size_t start_bit, size_t num_bits) {
    uint32_t result = 0;
    
    for (size_t i = 0; i < num_bits && i < 32; i++) {
        if (start_bit + i < 8 * sizeof(uint32_t)) {
            result |= ((uint32_t)(bits[start_bit + i] & 1)) << (num_bits - 1 - i);
        }
    }
    
    return result;
}

static uint64_t bits_to_uint64(const uint8_t *bits, size_t start_bit, size_t num_bits) {
    uint64_t result = 0;
    
    for (size_t i = 0; i < num_bits && i < 64; i++) {
        if (start_bit + i < 8 * sizeof(uint64_t)) {
            result |= ((uint64_t)(bits[start_bit + i] & 1)) << (num_bits - 1 - i);
        }
    }
    
    return result;
}

// ============================================================================
// EM410x Protocol Implementation
// ============================================================================

int lf_em410x_read(lf_em410x_result_t *result, const lf_em410x_config_t *config) {
    if (result == NULL) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    // Initialize result
    memset(result, 0, sizeof(lf_em410x_result_t));
    
    // Set up detection buffer
    lf_edge_event_t events[1000];
    lf_detection_buffer_t detection_buffer = {
        .events = events,
        .max_events = sizeof(events) / sizeof(events[0]),
        .event_count = 0,
        .timeout_us = (config ? config->timeout_ms : 1000) * 1000,
        .overflow = false
    };
    
    // Start field and detection
    int ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    ret = lf_detection_start(&detection_buffer);
    if (ret != LF_SUCCESS) {
        lf_field_off();
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Set timeout
    lf_timing_set_timeout(detection_buffer.timeout_us);
    
    // Wait for data or timeout
    while (!lf_timing_check_timeout() && detection_buffer.event_count < detection_buffer.max_events) {
        lf_timing_delay_us(100); // Small delay to prevent busy waiting
        
        if (detection_buffer.event_count > 100) {
            // We have enough data to try decoding
            break;
        }
    }
    
    // Stop detection and field
    lf_detection_stop();
    lf_field_off();
    
    if (detection_buffer.event_count < 64) {
        return LF_PROTOCOL_ERROR_NOT_FOUND;
    }
    
    // Convert edge events to bit stream
    uint8_t bit_buffer[512];
    size_t bit_count = 0;
    
    // Simple edge-to-bit conversion (this would be more sophisticated in practice)
    for (uint16_t i = 1; i < detection_buffer.event_count && bit_count < sizeof(bit_buffer); i++) {
        uint32_t pulse_width = detection_buffer.events[i].timestamp - detection_buffer.events[i-1].timestamp;
        
        // Convert pulse width to bits based on expected clock rate
        uint32_t expected_bit_time = 1000000 / 64; // 64 Hz default
        uint32_t num_bits = (pulse_width + expected_bit_time / 2) / expected_bit_time;
        
        if (num_bits > 0 && num_bits < 10) { // Reasonable range
            uint8_t bit_value = (detection_buffer.events[i-1].edge_type == LF_EDGE_RISING) ? 1 : 0;
            for (uint32_t j = 0; j < num_bits && bit_count < sizeof(bit_buffer); j++) {
                bit_buffer[bit_count++] = bit_value;
            }
        }
    }
    
    // Decode EM410x from bit buffer
    return lf_em410x_decode_buffer(bit_buffer, bit_count, result);
}

int lf_em410x_decode_buffer(const uint8_t *buffer, size_t buffer_size, lf_em410x_result_t *result) {
    if (buffer == NULL || result == NULL || buffer_size < 64) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    // Initialize result
    memset(result, 0, sizeof(lf_em410x_result_t));
    
    // Copy buffer for processing
    uint8_t *bits = malloc(buffer_size);
    if (bits == NULL) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    memcpy(bits, buffer, buffer_size);
    
    size_t size = buffer_size;
    int clock = 64;
    int invert = 0;
    
    // ASK demodulation
    int errors = ask_demod(bits, &size, &clock, &invert, 20);
    if (errors > 50) {
        free(bits);
        return LF_PROTOCOL_ERROR_NOT_FOUND;
    }
    
    // Look for EM410x preamble (9 ones)
    uint8_t preamble[] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    size_t start_idx = 0;
    bool found_preamble = false;
    
    for (size_t i = 0; i <= size - sizeof(preamble); i++) {
        if (memcmp(bits + i, preamble, sizeof(preamble)) == 0) {
            start_idx = i + sizeof(preamble);
            found_preamble = true;
            break;
        }
    }
    
    if (!found_preamble) {
        free(bits);
        return LF_PROTOCOL_ERROR_NOT_FOUND;
    }
    
    // Remove parity and validate
    bool valid_short, valid_short_extended, valid_long;
    size_t data_size = size - start_idx;
    size_t decoded_size = remove_em410x_parity(bits + start_idx, 0, &data_size, 
                                               &valid_short, &valid_short_extended, &valid_long);
    
    if (decoded_size == 0) {
        free(bits);
        return LF_PROTOCOL_ERROR_CHECKSUM;
    }
    
    // Extract ID based on format
    if (valid_short) {
        result->format = 1;
        result->id_hi = 0;
        result->id_lo = ((uint64_t)bits_to_uint32(bits + start_idx, 0, 8) << 32) | 
                        bits_to_uint32(bits + start_idx, 8, 32);
    } else if (valid_long || valid_short_extended) {
        result->format = valid_long ? 2 : 4;
        result->id_hi = bits_to_uint32(bits + start_idx, 0, 24);
        result->id_lo = ((uint64_t)bits_to_uint32(bits + start_idx, 24, 32) << 32) | 
                        bits_to_uint32(bits + start_idx, 56, 32);
    }
    
    result->valid = true;
    result->clock = clock;
    result->start_idx = start_idx;
    
    free(bits);
    return LF_PROTOCOL_SUCCESS;
}

int lf_em410x_simulate(uint32_t id_hi, uint64_t id_lo, uint32_t duration_ms) {
    // Encode EM410x ID
    uint8_t encoded_data[128];
    size_t encoded_size;
    
    int ret = lf_em410x_encode_id(id_hi, id_lo, encoded_data, &encoded_size);
    if (ret != LF_PROTOCOL_SUCCESS) {
        return ret;
    }
    
    // Configure signal for EM410x
    lf_signal_config_t config = {
        .carrier_freq = 125000,
        .data_rate = 64,
        .modulation = LF_MODULATION_ASK,
        .power_level = 200,
        .invert_output = false
    };
    
    ret = lf_signal_configure(&config);
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Start simulation
    ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    uint32_t start_time = lf_timing_get_ms();
    while (lf_timing_get_ms() - start_time < duration_ms) {
        // Send encoded data
        ret = lf_signal_send_bits(encoded_data, encoded_size, &config);
        if (ret != LF_SUCCESS) {
            lf_field_off();
            return LF_PROTOCOL_ERROR_HARDWARE;
        }
        
        // Small gap between transmissions
        lf_timing_delay_ms(10);
    }
    
    lf_field_off();
    return LF_PROTOCOL_SUCCESS;
}

int lf_em410x_encode_id(uint32_t id_hi, uint64_t id_lo, uint8_t *encoded_data, size_t *encoded_size) {
    if (encoded_data == NULL || encoded_size == NULL) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    size_t bit_idx = 0;
    
    // Add preamble (9 ones)
    for (int i = 0; i < 9; i++) {
        encoded_data[bit_idx++] = 1;
    }
    
    // Encode data with parity
    uint64_t full_id = ((uint64_t)id_hi << 32) | (id_lo & 0xFFFFFFFF);
    
    // Process 4-bit groups
    for (int group = 0; group < 10; group++) {
        uint8_t nibble = (full_id >> (36 - group * 4)) & 0x0F;
        
        // Add 4 data bits
        for (int bit = 0; bit < 4; bit++) {
            encoded_data[bit_idx++] = (nibble >> (3 - bit)) & 1;
        }
        
        // Calculate and add parity bit (even parity)
        uint8_t parity = 0;
        for (int bit = 0; bit < 4; bit++) {
            parity ^= (nibble >> bit) & 1;
        }
        encoded_data[bit_idx++] = parity;
    }
    
    // Add column parity
    for (int col = 0; col < 4; col++) {
        uint8_t column_parity = 0;
        for (int row = 0; row < 10; row++) {
            column_parity ^= encoded_data[9 + row * 5 + col];
        }
        encoded_data[bit_idx++] = column_parity;
    }
    
    *encoded_size = bit_idx;
    return LF_PROTOCOL_SUCCESS;
}

// ============================================================================
// T55xx Protocol Implementation (Basic)
// ============================================================================

int lf_t55xx_read_block(uint8_t block, lf_t55xx_block_t *result, const lf_t55xx_timing_t *timing) {
    if (result == NULL || block > 7) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    // Initialize result
    memset(result, 0, sizeof(lf_t55xx_block_t));
    result->block = block;
    
    // T55xx read command: start gap + opcode + block address + stop bit
    uint8_t command_bits[32];
    size_t cmd_bit_count = 0;
    
    // Start gap (implemented as field off time)
    lf_field_off();
    lf_timing_delay_us(timing ? timing->start_gap : 8 * 8); // Default 8 Tc
    
    // Opcode for read (10)
    command_bits[cmd_bit_count++] = 1;
    command_bits[cmd_bit_count++] = 0;
    
    // Block address (3 bits)
    for (int i = 2; i >= 0; i--) {
        command_bits[cmd_bit_count++] = (block >> i) & 1;
    }
    
    // Configure signal for T55xx downlink
    lf_signal_config_t config = {
        .carrier_freq = 125000,
        .data_rate = 32, // Slower for T55xx
        .modulation = LF_MODULATION_ASK,
        .power_level = 255,
        .invert_output = false
    };
    
    int ret = lf_signal_configure(&config);
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Send command
    ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    ret = lf_signal_send_bits(command_bits, cmd_bit_count, &config);
    if (ret != LF_SUCCESS) {
        lf_field_off();
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Write gap
    lf_timing_delay_us(timing ? timing->write_gap : 10 * 8); // Default 10 Tc
    
    // Turn field back on for response
    lf_field_on();
    lf_timing_delay_us(137 * 8); // Wait for response
    
    // Set up detection for response
    lf_edge_event_t events[200];
    lf_detection_buffer_t detection_buffer = {
        .events = events,
        .max_events = sizeof(events) / sizeof(events[0]),
        .event_count = 0,
        .timeout_us = 50000, // 50ms timeout
        .overflow = false
    };
    
    ret = lf_detection_start(&detection_buffer);
    if (ret != LF_SUCCESS) {
        lf_field_off();
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Wait for response
    lf_timing_set_timeout(detection_buffer.timeout_us);
    while (!lf_timing_check_timeout() && detection_buffer.event_count < 100) {
        lf_timing_delay_us(100);
    }
    
    lf_detection_stop();
    lf_field_off();
    
    if (detection_buffer.event_count < 32) {
        return LF_PROTOCOL_ERROR_TIMEOUT;
    }
    
    // Decode response (simplified - would need proper T55xx demodulation)
    result->data = 0x12345678; // Placeholder
    result->valid = true;
    
    return LF_PROTOCOL_SUCCESS;
}

int lf_t55xx_write_block(uint8_t block, uint32_t data, uint32_t password, const lf_t55xx_timing_t *timing) {
    if (block > 7) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    // T55xx write command: start gap + opcode + password + data + block address
    uint8_t command_bits[128];
    size_t cmd_bit_count = 0;
    
    // Start gap
    lf_field_off();
    lf_timing_delay_us(timing ? timing->start_gap : 15 * 8); // Default 15 Tc
    
    // Opcode for write (00)
    command_bits[cmd_bit_count++] = 0;
    command_bits[cmd_bit_count++] = 0;
    
    // Password (32 bits) - if password mode
    if (password != 0) {
        for (int i = 31; i >= 0; i--) {
            command_bits[cmd_bit_count++] = (password >> i) & 1;
        }
    }
    
    // Data (32 bits)
    for (int i = 31; i >= 0; i--) {
        command_bits[cmd_bit_count++] = (data >> i) & 1;
    }
    
    // Block address (3 bits)
    for (int i = 2; i >= 0; i--) {
        command_bits[cmd_bit_count++] = (block >> i) & 1;
    }
    
    // Configure signal for T55xx programming
    lf_signal_config_t config = {
        .carrier_freq = 125000,
        .data_rate = 32,
        .modulation = LF_MODULATION_ASK,
        .power_level = 255,
        .invert_output = false
    };
    
    int ret = lf_signal_configure(&config);
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Send programming sequence
    ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    ret = lf_signal_send_bits(command_bits, cmd_bit_count, &config);
    if (ret != LF_SUCCESS) {
        lf_field_off();
        return LF_PROTOCOL_ERROR_HARDWARE;
    }
    
    // Programming time
    lf_timing_delay_ms(4); // T55xx programming time
    
    lf_field_off();
    return LF_PROTOCOL_SUCCESS;
}

// ============================================================================
// Protocol Scanner Implementation
// ============================================================================

int lf_scan_auto(lf_scan_result_t *results, uint8_t max_results, uint8_t *result_count, const lf_scan_config_t *config) {
    if (results == NULL || result_count == NULL || max_results == 0) {
        return LF_PROTOCOL_ERROR_INVALID_PARAM;
    }
    
    *result_count = 0;
    
    // Try EM410x first (most common)
    if (*result_count < max_results) {
        lf_em410x_config_t em_config = {
            .timeout_ms = 1000,
            .max_errors = 20,
            .verbose = config ? config->verbose : false,
            .amplitude_threshold = config ? config->signal_threshold : 50
        };
        
        lf_em410x_result_t em_result;
        if (lf_em410x_read(&em_result, &em_config) == LF_PROTOCOL_SUCCESS && em_result.valid) {
            results[*result_count].protocol = LF_PROTOCOL_EM410X;
            results[*result_count].data.em410x = em_result;
            results[*result_count].valid = true;
            results[*result_count].clock_rate = em_result.clock;
            (*result_count)++;
            
            if (!config || !config->scan_all_protocols) {
                return LF_PROTOCOL_SUCCESS;
            }
        }
    }
    
    // Try other protocols...
    // (HID, Indala, etc. would be implemented similarly)
    
    return (*result_count > 0) ? LF_PROTOCOL_SUCCESS : LF_PROTOCOL_ERROR_NOT_FOUND;
}

const char *lf_protocol_name(lf_protocol_type_t protocol) {
    switch (protocol) {
        case LF_PROTOCOL_EM410X: return "EM410x";
        case LF_PROTOCOL_T55XX: return "T55xx";
        case LF_PROTOCOL_HID_PROX: return "HID Prox";
        case LF_PROTOCOL_INDALA: return "Indala";
        case LF_PROTOCOL_AWID: return "AWID";
        case LF_PROTOCOL_IOPROX: return "ioProx";
        case LF_PROTOCOL_FDXB: return "FDX-B";
        default: return "Unknown";
    }
}

// ============================================================================
// Utility Functions Implementation
// ============================================================================

uint8_t lf_calculate_em410x_parity(uint32_t data) {
    uint8_t parity = 0;
    for (int i = 0; i < 32; i++) {
        parity ^= (data >> i) & 1;
    }
    return parity;
}

bool lf_validate_em410x_parity(uint32_t data, uint8_t parity) {
    return lf_calculate_em410x_parity(data) == parity;
}

