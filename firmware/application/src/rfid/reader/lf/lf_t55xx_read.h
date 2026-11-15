#ifndef LF_T55XX_READ_H
#define LF_T55XX_READ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One Chameleon data-frame payload: an eight-byte header and packed nibbles. */
#define LF_T55XX_CAPTURE_HEADER_SIZE 8U
#define LF_T55XX_CAPTURE_MAX_BYTES 4096U
#define LF_T55XX_CAPTURE_MAX_SAMPLES \
    ((LF_T55XX_CAPTURE_MAX_BYTES - LF_T55XX_CAPTURE_HEADER_SIZE) * 2U)

/*
 * Send a six-bit, passwordless Page 0 direct-access READ, then capture the LF
 * envelope at one sample per carrier period (8 us/sample). Reader hardware
 * must already be initialized. This API cannot send write, lock or password
 * bits; a tag configured to require a password is outside its scope.
 *
 * On success, the output is:
 *   version=1, bits/sample=4, sample_count_be16, min_be_i16, max_be_i16,
 *   high-nibble-first quantized samples.
 *
 * Success describes ADC acquisition, not a decoded or authenticated tag.
 * The caller must provide at least LF_T55XX_CAPTURE_MAX_BYTES bytes. Failure
 * leaves *outlen zero and releases RF/SAADC owned by this operation.
 */
bool t55xx_read_block_capture(
    uint8_t block,
    uint8_t *out,
    size_t capacity,
    size_t *outlen
);

#endif
