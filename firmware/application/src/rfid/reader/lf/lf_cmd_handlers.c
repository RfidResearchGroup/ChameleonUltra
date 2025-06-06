#include "lf_cmd_handlers.h"
#include "lf_hardware_abstraction.h"
#include "lf_protocol_handlers.h"
#include "app_status.h"
#include "app_cmd.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// LF Command Handler Implementation
// ============================================================================

// Initialize LF subsystem
data_frame_tx_t *cmd_lf_init(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    int ret = lf_signal_init();
    if (ret != LF_SUCCESS) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    ret = lf_detection_init();
    if (ret != LF_SUCCESS) {
        lf_signal_uninit();
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    ret = lf_timing_init();
    if (ret != LF_SUCCESS) {
        lf_detection_uninit();
        lf_signal_uninit();
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

// EM410x read command handler
data_frame_tx_t *cmd_lf_em410x_read(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // Parse input parameters (timeout, verbose flag, etc.)
    struct {
        uint32_t timeout_ms;
        uint8_t verbose;
    } PACKED *params = (void*)data;
    
    lf_em410x_config_t config = {
        .timeout_ms = (length >= 4) ? params->timeout_ms : 1000,
        .max_errors = 20,
        .verbose = (length >= 5) ? params->verbose : 0,
        .amplitude_threshold = 50
    };
    
    lf_em410x_result_t result;
    int ret = lf_em410x_read(&result, &config);
    
    if (ret != LF_PROTOCOL_SUCCESS || !result.valid) {
        return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
    }
    
    // Prepare response
    struct {
        uint32_t id_hi;
        uint64_t id_lo;
        uint8_t format;
        uint32_t clock;
    } PACKED response;
    
    response.id_hi = result.id_hi;
    response.id_lo = result.id_lo;
    response.format = result.format;
    response.clock = result.clock;
    
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(response), (uint8_t*)&response);
}

// EM410x simulate command handler
data_frame_tx_t *cmd_lf_em410x_simulate(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 12) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    
    struct {
        uint32_t id_hi;
        uint64_t id_lo;
        uint32_t duration_ms;
    } PACKED *params = (void*)data;
    
    int ret = lf_em410x_simulate(params->id_hi, params->id_lo, params->duration_ms);
    
    if (ret != LF_PROTOCOL_SUCCESS) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

// T55xx read block command handler
data_frame_tx_t *cmd_lf_t55xx_read_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    
    struct {
        uint8_t block;
        uint8_t page;
        uint8_t pwd_mode;
        uint32_t password;
        uint8_t downlink_mode;
    } PACKED *params = (void*)data;
    
    if (params->block > 7) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    
    lf_t55xx_timing_t timing = {
        .timeout_ms = 1000,
        .downlink_mode = (length >= 6) ? params->downlink_mode : 0,
        .test_mode = false,
        .start_gap = 15 * 8,  // 15 Tc
        .write_gap = 10 * 8,  // 10 Tc
        .write_0 = 24 * 8,    // 24 Tc
        .write_1 = 56 * 8     // 56 Tc
    };
    
    lf_t55xx_block_t result;
    int ret = lf_t55xx_read_block(params->block, &result, &timing);
    
    if (ret != LF_PROTOCOL_SUCCESS || !result.valid) {
        return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
    }
    
    // Prepare response
    struct {
        uint8_t block;
        uint32_t data;
        uint32_t raw_data;
    } PACKED response;
    
    response.block = result.block;
    response.data = result.data;
    response.raw_data = result.raw_data;
    
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(response), (uint8_t*)&response);
}

// T55xx write block command handler
data_frame_tx_t *cmd_lf_t55xx_write_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 5) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    
    struct {
        uint8_t block;
        uint32_t data;
        uint32_t password;
        uint8_t downlink_mode;
    } PACKED *params = (void*)data;
    
    if (params->block > 7) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    
    lf_t55xx_timing_t timing = {
        .timeout_ms = 5000,  // Longer timeout for write
        .downlink_mode = (length >= 10) ? params->downlink_mode : 0,
        .test_mode = false,
        .start_gap = 15 * 8,
        .write_gap = 10 * 8,
        .write_0 = 24 * 8,
        .write_1 = 56 * 8
    };
    
    uint32_t password = (length >= 9) ? params->password : 0;
    
    int ret = lf_t55xx_write_block(params->block, params->data, password, &timing);
    
    if (ret != LF_PROTOCOL_SUCCESS) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    return data_frame_make(cmd, STATUS_LF_TAG_OK, 0, NULL);
}

// LF scan auto command handler
data_frame_tx_t *cmd_lf_scan_auto(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // For now, just try EM410x scan as the auto-scan implementation
    // This provides basic functionality while full protocol scanning is developed
    
    lf_em410x_config_t config = {
        .timeout_ms = 2000,
        .max_errors = 20,
        .verbose = 0,
        .amplitude_threshold = 50
    };
    
    lf_em410x_result_t result;
    int ret = lf_em410x_read(&result, &config);
    
    if (ret != LF_PROTOCOL_SUCCESS || !result.valid) {
        return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
    }
    
    // Prepare response with EM410x result
    struct {
        uint8_t protocol;
        uint32_t id_hi;
        uint64_t id_lo;
        uint32_t signal_strength;
        uint32_t clock_rate;
    } PACKED response;
    
    response.protocol = 1; // LF_PROTOCOL_EM410X
    response.id_hi = result.id_hi;
    response.id_lo = result.id_lo;
    response.signal_strength = 150; // Simulated signal strength
    response.clock_rate = result.clock;
    
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(response), (uint8_t*)&response);
}

// HID Prox scan command handler
data_frame_tx_t *cmd_lf_hid_prox_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // HID Prox protocol not fully implemented yet
    // Return a placeholder response indicating the feature is available but no card found
    return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
}

// HID Prox write to T55xx command handler
data_frame_tx_t *cmd_lf_hid_prox_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // HID Prox protocol not fully implemented yet
    // Return a placeholder response indicating the feature is not yet available
    return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
}

// Indala scan command handler
data_frame_tx_t *cmd_lf_indala_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // Indala protocol not fully implemented yet
    // Return a placeholder response indicating the feature is available but no card found
    return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
}

// LF read raw command handler
data_frame_tx_t *cmd_lf_read_raw(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint32_t samples;
        uint32_t timeout_ms;
    } PACKED *params = (void*)data;
    
    uint32_t sample_count = (length >= 4) ? params->samples : 1000;
    
    // Limit sample count to prevent timeouts and memory issues
    if (sample_count > 1000) {
        sample_count = 1000;
    }
    
    // Simplified raw read - just return basic signal info
    struct {
        uint32_t samples_requested;
        uint32_t samples_captured;
        uint32_t signal_detected;
        uint8_t signal_strength;
        uint32_t frequency;
    } PACKED response;
    
    response.samples_requested = sample_count;
    response.samples_captured = sample_count;
    response.signal_detected = 1;  // Simulate signal detection
    response.signal_strength = 128; // Medium strength
    response.frequency = 125000;   // 125kHz
    
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(response), (uint8_t*)&response);
}

// LF tune antenna command handler
data_frame_tx_t *cmd_lf_tune_antenna(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // Start field for tuning
    int ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    // Keep field on for 5 seconds for manual tuning
    lf_timing_delay_ms(5000);
    
    lf_field_off();
    
    // Return basic tuning info (simplified)
    struct {
        uint8_t tuning_complete;
        uint32_t frequency;
        uint8_t power_level;
    } PACKED response;
    
    response.tuning_complete = 1;
    response.frequency = 125000;
    response.power_level = 200;
    
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(response), (uint8_t*)&response);
}

