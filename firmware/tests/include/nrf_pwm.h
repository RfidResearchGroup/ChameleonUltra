#pragma once

// Host-only subset of Nordic's nrf_pwm.h used by the LF protocol tests.
#include <stdint.h>

typedef struct {
    uint16_t channel_0;
    uint16_t channel_1;
    uint16_t channel_2;
    uint16_t counter_top;
} nrf_pwm_values_wave_form_t;

typedef union {
    const nrf_pwm_values_wave_form_t *p_wave_form;
    const uint16_t *p_raw;
} nrf_pwm_values_t;

typedef struct {
    nrf_pwm_values_t values;
    uint16_t length;
    uint32_t repeats;
    uint32_t end_delay;
} nrf_pwm_sequence_t;

#define NRF_PWM_VALUES_LENGTH(values) (sizeof(values) / 2u)
