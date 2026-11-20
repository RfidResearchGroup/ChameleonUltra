#ifndef MFU_PROFILE_WIRE_H
#define MFU_PROFILE_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MFU_PROFILE_PREFIX_SIZE 11U

/* Validate before any config/image byte is changed, including hidden pages. */
static inline bool mfu_profile_write_valid(const uint8_t *data, size_t length,
                                           const uint8_t *expected_header,
                                           size_t total_pages, uint8_t current_mode) {
    if (data == NULL || expected_header == NULL || total_pages == 0U ||
            total_pages > 255U || length != MFU_PROFILE_PREFIX_SIZE + total_pages * 4U ||
            current_mode > 2U) return false;
    if ((((size_t)expected_header[6] << 8U) | expected_header[7]) != total_pages) return false;
    return memcmp(data, expected_header, 8U) == 0 && ((data[8] >> 1U) & 7U) <= 2U;
}

#endif
