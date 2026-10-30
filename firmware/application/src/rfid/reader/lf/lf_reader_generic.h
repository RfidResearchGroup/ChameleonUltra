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

/*
 * Capture `count` full-resolution samples (14-bit, 0-16383), one per carrier
 * period, at the sample phase set with lf_125khz_radio_saadc_phase_set().
 *
 * @return true if all `count` samples arrived within `timeout_ms`.
 *         Check lf_capture_dropped() as well: a capture can be complete but
 *         have lost samples in the middle.
 */
bool raw_read_samples(int16_t *samples, size_t count, uint32_t timeout_ms, size_t *outlen);

/** Samples dropped during the last capture (ring buffer full). */
uint32_t lf_capture_dropped(void);
