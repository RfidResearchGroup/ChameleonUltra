#ifndef LF_TEST_SUITE_H
#define LF_TEST_SUITE_H

#ifdef __cplusplus
extern "C" {
#endif

// Main test runner function
// Returns 0 if all tests pass, number of failed tests otherwise
int run_lf_tests(void);

// Individual test functions (for selective testing)
bool test_lf_signal_init_uninit(void);
bool test_lf_signal_configuration(void);
bool test_lf_timing_functions(void);
bool test_lf_manchester_encoding(void);
bool test_em410x_encode_decode(void);
bool test_em410x_parity_calculation(void);
bool test_protocol_scanner(void);
bool test_crc_calculation(void);
bool test_full_lf_initialization(void);

#ifdef __cplusplus
}
#endif

#endif // LF_TEST_SUITE_H

