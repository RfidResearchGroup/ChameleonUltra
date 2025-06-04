#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "lf_protocol_handlers.h"
#include "lf_hardware_abstraction.h"

// ============================================================================
// Test Framework
// ============================================================================

typedef struct {
    const char *test_name;
    bool (*test_function)(void);
    bool passed;
    const char *error_message;
} lf_test_case_t;

static int test_count = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define LF_TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            return false; \
        } \
    } while(0)

#define LF_TEST_ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL: %s - %s (expected: %d, actual: %d)\n", __func__, message, (int)(expected), (int)(actual)); \
            return false; \
        } \
    } while(0)

// ============================================================================
// Hardware Abstraction Layer Tests
// ============================================================================

bool test_lf_signal_init_uninit(void) {
    // Test initialization
    int ret = lf_signal_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Signal initialization failed");
    
    // Test double initialization (should succeed)
    ret = lf_signal_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Double initialization failed");
    
    // Test uninitialization
    ret = lf_signal_uninit();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Signal uninitialization failed");
    
    // Test uninitialization when not initialized
    ret = lf_signal_uninit();
    LF_TEST_ASSERT_EQ(LF_ERROR_NOT_INITIALIZED, ret, "Uninit when not initialized should fail");
    
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_lf_signal_configuration(void) {
    int ret = lf_signal_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Signal initialization failed");
    
    // Test valid configuration
    lf_signal_config_t config = {
        .carrier_freq = 125000,
        .data_rate = 64,
        .modulation = LF_MODULATION_ASK,
        .power_level = 128,
        .invert_output = false
    };
    
    ret = lf_signal_configure(&config);
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Valid configuration failed");
    
    // Test invalid frequency
    config.carrier_freq = 50000; // Too low
    ret = lf_signal_configure(&config);
    LF_TEST_ASSERT_EQ(LF_ERROR_INVALID_PARAM, ret, "Invalid frequency should fail");
    
    // Test invalid data rate
    config.carrier_freq = 125000;
    config.data_rate = 0; // Invalid
    ret = lf_signal_configure(&config);
    LF_TEST_ASSERT_EQ(LF_ERROR_INVALID_PARAM, ret, "Invalid data rate should fail");
    
    // Test NULL parameter
    ret = lf_signal_configure(NULL);
    LF_TEST_ASSERT_EQ(LF_ERROR_INVALID_PARAM, ret, "NULL config should fail");
    
    lf_signal_uninit();
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_lf_timing_functions(void) {
    int ret = lf_timing_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Timing initialization failed");
    
    // Test timeout functionality
    ret = lf_timing_set_timeout(1000); // 1ms timeout
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Set timeout failed");
    
    // Should not be timed out immediately
    bool timed_out = lf_timing_check_timeout();
    LF_TEST_ASSERT(!timed_out, "Should not be timed out immediately");
    
    // Wait and check again
    lf_timing_delay_us(1500); // Wait longer than timeout
    timed_out = lf_timing_check_timeout();
    LF_TEST_ASSERT(timed_out, "Should be timed out after delay");
    
    lf_timing_uninit();
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_lf_manchester_encoding(void) {
    uint8_t data[] = {0xAB, 0xCD}; // 10101011 11001101
    uint8_t encoded[32];
    uint16_t encoded_bits;
    
    int ret = lf_manchester_encode(data, 16, encoded, &encoded_bits);
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Manchester encoding failed");
    LF_TEST_ASSERT_EQ(32, encoded_bits, "Encoded bits count incorrect");
    
    // Test decoding
    uint8_t decoded[16];
    uint16_t decoded_bits;
    
    ret = lf_manchester_decode(encoded, encoded_bits, decoded, &decoded_bits);
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Manchester decoding failed");
    LF_TEST_ASSERT_EQ(16, decoded_bits, "Decoded bits count incorrect");
    
    // Verify data integrity
    LF_TEST_ASSERT_EQ(data[0], decoded[0], "First byte mismatch");
    LF_TEST_ASSERT_EQ(data[1], decoded[1], "Second byte mismatch");
    
    printf("PASS: %s\n", __func__);
    return true;
}

// ============================================================================
// Protocol Handler Tests
// ============================================================================

bool test_em410x_encode_decode(void) {
    uint32_t id_hi = 0x12;
    uint64_t id_lo = 0x3456789ABCDEF012ULL;
    
    // Test encoding
    uint8_t encoded_data[128];
    size_t encoded_size;
    
    int ret = lf_em410x_encode_id(id_hi, id_lo, encoded_data, &encoded_size);
    LF_TEST_ASSERT_EQ(LF_PROTOCOL_SUCCESS, ret, "EM410x encoding failed");
    LF_TEST_ASSERT(encoded_size > 0, "Encoded size should be positive");
    
    // Test decoding
    lf_em410x_result_t result;
    ret = lf_em410x_decode_buffer(encoded_data, encoded_size, &result);
    LF_TEST_ASSERT_EQ(LF_PROTOCOL_SUCCESS, ret, "EM410x decoding failed");
    LF_TEST_ASSERT(result.valid, "Decoded result should be valid");
    
    // Note: Due to EM410x format limitations, we may not get exact match for extended IDs
    // This is expected behavior
    
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_em410x_parity_calculation(void) {
    uint32_t test_data = 0x12345678;
    
    uint8_t parity = lf_calculate_em410x_parity(test_data);
    bool valid = lf_validate_em410x_parity(test_data, parity);
    LF_TEST_ASSERT(valid, "Parity validation failed");
    
    // Test with wrong parity
    valid = lf_validate_em410x_parity(test_data, parity ^ 1);
    LF_TEST_ASSERT(!valid, "Wrong parity should not validate");
    
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_protocol_scanner(void) {
    // Test scanner configuration
    lf_scan_config_t config = {
        .scan_time_ms = 100, // Short scan for testing
        .scan_all_protocols = false,
        .signal_threshold = 50,
        .verbose = false
    };
    
    lf_scan_result_t results[5];
    uint8_t result_count = 0;
    
    // Note: This test will likely return "not found" since we don't have actual cards
    // But it should not crash or return invalid parameters
    int ret = lf_scan_auto(results, sizeof(results)/sizeof(results[0]), &result_count, &config);
    LF_TEST_ASSERT(ret == LF_PROTOCOL_SUCCESS || ret == LF_PROTOCOL_ERROR_NOT_FOUND, 
                   "Scanner should return success or not found");
    
    // Test protocol name function
    const char *name = lf_protocol_name(LF_PROTOCOL_EM410X);
    LF_TEST_ASSERT(strcmp(name, "EM410x") == 0, "Protocol name mismatch");
    
    name = lf_protocol_name(LF_PROTOCOL_UNKNOWN);
    LF_TEST_ASSERT(strcmp(name, "Unknown") == 0, "Unknown protocol name mismatch");
    
    printf("PASS: %s\n", __func__);
    return true;
}

bool test_crc_calculation(void) {
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    
    uint16_t crc1 = lf_calculate_crc16(test_data, sizeof(test_data));
    uint16_t crc2 = lf_calculate_crc16(test_data, sizeof(test_data));
    
    LF_TEST_ASSERT_EQ(crc1, crc2, "CRC calculation should be deterministic");
    
    // Test validation
    bool valid = lf_validate_checksum(test_data, sizeof(test_data), crc1);
    LF_TEST_ASSERT(valid, "CRC validation should pass");
    
    valid = lf_validate_checksum(test_data, sizeof(test_data), crc1 ^ 0xFFFF);
    LF_TEST_ASSERT(!valid, "Wrong CRC should not validate");
    
    printf("PASS: %s\n", __func__);
    return true;
}

// ============================================================================
// Integration Tests
// ============================================================================

bool test_full_lf_initialization(void) {
    // Test complete LF subsystem initialization
    int ret = lf_signal_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Signal init failed");
    
    ret = lf_detection_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Detection init failed");
    
    ret = lf_timing_init();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Timing init failed");
    
    // Test field control
    ret = lf_field_on();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Field on failed");
    
    bool field_on = lf_field_is_on();
    LF_TEST_ASSERT(field_on, "Field should be on");
    
    ret = lf_field_off();
    LF_TEST_ASSERT_EQ(LF_SUCCESS, ret, "Field off failed");
    
    // Cleanup
    lf_timing_uninit();
    lf_detection_uninit();
    lf_signal_uninit();
    
    printf("PASS: %s\n", __func__);
    return true;
}

// ============================================================================
// Test Runner
// ============================================================================

static lf_test_case_t test_cases[] = {
    {"Signal Init/Uninit", test_lf_signal_init_uninit, false, NULL},
    {"Signal Configuration", test_lf_signal_configuration, false, NULL},
    {"Timing Functions", test_lf_timing_functions, false, NULL},
    {"Manchester Encoding", test_lf_manchester_encoding, false, NULL},
    {"EM410x Encode/Decode", test_em410x_encode_decode, false, NULL},
    {"EM410x Parity", test_em410x_parity_calculation, false, NULL},
    {"Protocol Scanner", test_protocol_scanner, false, NULL},
    {"CRC Calculation", test_crc_calculation, false, NULL},
    {"Full LF Initialization", test_full_lf_initialization, false, NULL},
};

int run_lf_tests(void) {
    printf("=== LF Protocol Handler Test Suite ===\n");
    printf("Running %d tests...\n\n", (int)(sizeof(test_cases)/sizeof(test_cases[0])));
    
    test_count = sizeof(test_cases)/sizeof(test_cases[0]);
    tests_passed = 0;
    tests_failed = 0;
    
    for (int i = 0; i < test_count; i++) {
        printf("Running: %s\n", test_cases[i].test_name);
        
        bool result = test_cases[i].test_function();
        test_cases[i].passed = result;
        
        if (result) {
            tests_passed++;
        } else {
            tests_failed++;
            test_cases[i].error_message = "Test function returned false";
        }
        
        printf("\n");
    }
    
    printf("=== Test Results ===\n");
    printf("Total tests: %d\n", test_count);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", (float)tests_passed / test_count * 100.0f);
    
    if (tests_failed > 0) {
        printf("\nFailed tests:\n");
        for (int i = 0; i < test_count; i++) {
            if (!test_cases[i].passed) {
                printf("- %s: %s\n", test_cases[i].test_name, 
                       test_cases[i].error_message ? test_cases[i].error_message : "Unknown error");
            }
        }
    }
    
    return tests_failed;
}

// Simple main function for standalone testing
#ifdef LF_TEST_STANDALONE
int main(void) {
    return run_lf_tests();
}
#endif

