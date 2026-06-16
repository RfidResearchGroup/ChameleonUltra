#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Capture raw ADC samples from the LF antenna field.
 *
 * The SAADC samples at the PWM period rate (125kHz = 8µs/sample).
 * Each sample is an 8-bit value (14-bit ADC >> 5, clamped to 0xFF).
 * A steady carrier reads ~0x80-0x82; a gap reads noticeably lower.
 *
 * @param data        Output buffer for raw samples
 * @param maxlen      Max bytes to capture (max 4000 for USB frame limit)
 * @param timeout_ms  Stop after this many ms even if buffer not full
 * @param outlen      Actual number of bytes written
 * @return            true on success
 */
/** Maximum bytes a single raw capture can return (USB frame limit). */
#define LF_SNIFF_MAX_SAMPLES  4000

bool raw_read_to_buffer(uint8_t *data, size_t maxlen, uint32_t timeout_ms, size_t *outlen);
