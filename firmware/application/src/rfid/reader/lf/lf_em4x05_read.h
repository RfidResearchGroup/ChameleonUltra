#ifndef LF_EM4X05_READ_H
#define LF_EM4X05_READ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EM4X05_CAPTURE_SAMPLES 6144U
#define EM4X05_CAPTURE_BYTES (8U + EM4X05_CAPTURE_SAMPLES / 2U)

/* READ only; optional LOGIN uses a supplied password, with no search or writes. */
bool em4x05_read_capture(uint8_t address, uint32_t password, bool use_password,
                        uint8_t *output, size_t capacity, size_t *outlen);

#endif
