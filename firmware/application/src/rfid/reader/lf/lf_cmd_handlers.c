#include "lf_protocol_handlers.h"
#include "lf_hardware_abstraction.h"
#include "data_cmd.h"
#include "app_status.h"
#include "app_cmd.h"

// ============================================================================
// LF Command Handler Implementation
// ============================================================================

// Initialize LF subsystem
static data_frame_tx_t *cmd_lf_init(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
static data_frame_tx_t *cmd_lf_em410x_read(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
        return data_frame_make(cmd, STATUS_NOT_FOUND, 0, NULL);
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
    
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(response), (uint8_t*)&response);
}

// EM410x simulate command handler
static data_frame_tx_t *cmd_lf_em410x_simulate(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
static data_frame_tx_t *cmd_lf_t55xx_read_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
        return data_frame_make(cmd, STATUS_NOT_FOUND, 0, NULL);
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
    
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(response), (uint8_t*)&response);
}

// T55xx write block command handler
static data_frame_tx_t *cmd_lf_t55xx_write_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
    
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

// LF scan auto command handler
static data_frame_tx_t *cmd_lf_scan_auto(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint32_t scan_time_ms;
        uint8_t scan_all;
        uint8_t verbose;
    } PACKED *params = (void*)data;
    
    lf_scan_config_t config = {
        .scan_time_ms = (length >= 4) ? params->scan_time_ms : 2000,
        .scan_all_protocols = (length >= 5) ? params->scan_all : 0,
        .signal_threshold = 50,
        .verbose = (length >= 6) ? params->verbose : 0
    };
    
    lf_scan_result_t results[5];
    uint8_t result_count = 0;
    
    int ret = lf_scan_auto(results, sizeof(results)/sizeof(results[0]), &result_count, &config);
    
    if (ret != LF_PROTOCOL_SUCCESS || result_count == 0) {
        return data_frame_make(cmd, STATUS_NOT_FOUND, 0, NULL);
    }
    
    // Prepare response with first result
    struct {
        uint8_t protocol;
        uint32_t id_hi;
        uint64_t id_lo;
        uint32_t signal_strength;
        uint32_t clock_rate;
    } PACKED response;
    
    response.protocol = results[0].protocol;
    response.signal_strength = results[0].signal_strength;
    response.clock_rate = results[0].clock_rate;
    
    // Fill in protocol-specific data
    switch (results[0].protocol) {
        case LF_PROTOCOL_EM410X:
            response.id_hi = results[0].data.em410x.id_hi;
            response.id_lo = results[0].data.em410x.id_lo;
            break;
        default:
            response.id_hi = 0;
            response.id_lo = 0;
            break;
    }
    
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(response), (uint8_t*)&response);
}

// HID Prox scan command handler
static data_frame_tx_t *cmd_lf_hid_prox_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // For now, return not implemented - would need full HID implementation
    return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
}

// HID Prox write to T55xx command handler
static data_frame_tx_t *cmd_lf_hid_prox_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // For now, return not implemented - would need full HID implementation
    return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
}

// Indala scan command handler
static data_frame_tx_t *cmd_lf_indala_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // For now, return not implemented - would need full Indala implementation
    return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
}

// LF read raw command handler
static data_frame_tx_t *cmd_lf_read_raw(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint32_t samples;
        uint32_t timeout_ms;
    } PACKED *params = (void*)data;
    
    uint32_t sample_count = (length >= 4) ? params->samples : 1000;
    uint32_t timeout = (length >= 8) ? params->timeout_ms : 1000;
    
    // Set up detection buffer
    lf_edge_event_t *events = malloc(sample_count * sizeof(lf_edge_event_t));
    if (events == NULL) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    lf_detection_buffer_t detection_buffer = {
        .events = events,
        .max_events = sample_count,
        .event_count = 0,
        .timeout_us = timeout * 1000,
        .overflow = false
    };
    
    // Start field and detection
    int ret = lf_field_on();
    if (ret != LF_SUCCESS) {
        free(events);
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    ret = lf_detection_start(&detection_buffer);
    if (ret != LF_SUCCESS) {
        lf_field_off();
        free(events);
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    // Wait for data
    lf_timing_set_timeout(detection_buffer.timeout_us);
    while (!lf_timing_check_timeout() && detection_buffer.event_count < detection_buffer.max_events) {
        lf_timing_delay_us(100);
    }
    
    lf_detection_stop();
    lf_field_off();
    
    // Prepare response (simplified - just return event count and first few events)
    struct {
        uint32_t event_count;
        uint8_t overflow;
        lf_edge_event_t events[10]; // First 10 events
    } PACKED response;
    
    response.event_count = detection_buffer.event_count;
    response.overflow = detection_buffer.overflow;
    
    uint32_t events_to_copy = (detection_buffer.event_count < 10) ? detection_buffer.event_count : 10;
    memcpy(response.events, events, events_to_copy * sizeof(lf_edge_event_t));
    
    free(events);
    
    size_t response_size = sizeof(uint32_t) + sizeof(uint8_t) + events_to_copy * sizeof(lf_edge_event_t);
    return data_frame_make(cmd, STATUS_SUCCESS, response_size, (uint8_t*)&response);
}

// LF tune antenna command handler
static data_frame_tx_t *cmd_lf_tune_antenna(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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

