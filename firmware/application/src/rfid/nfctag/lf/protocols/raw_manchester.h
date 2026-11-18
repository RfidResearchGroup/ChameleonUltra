#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LF_RAW_MANCHESTER_MAX_BITS 224
#define LF_RAW_MANCHESTER_RECORD_SIZE 32

/* v1, clock, flags (terminator/invert), bit length, 28 MSB-first payload bytes. */
bool lf_raw_manchester_validate(const uint8_t *record, size_t length);
#define LF_RAW_MANCHESTER_MAX_ENTRIES (LF_RAW_MANCHESTER_MAX_BITS + 4)
/* Finite whole-frame budget, 125 kHz PWM ticks. Zero means invalid/no frame fits. */
uint16_t lf_raw_manchester_repetitions(uint32_t frame_ticks, uint32_t ms);

/* One PWM wave-form entry: three compare values followed by COUNTERTOP. */
typedef struct {
    uint16_t compare[3];
    uint16_t top;
} lf_raw_manchester_entry_t;

/* MSB first. Returns zero on invalid input, without changing output. */
size_t lf_raw_manchester_encode(const uint8_t *data, size_t data_size,
                               uint16_t bits, uint8_t clock, bool invert,
                               bool terminator, lf_raw_manchester_entry_t *out,
                               size_t capacity);
