#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t* data;
    size_t length;
} AesCmacSegment;
#include <stdlib.h>
#include <string.h>
#include "dfc_port.h"

bool aes_cmac(uint8_t* key, size_t key_len, uint8_t* message, size_t message_len, uint8_t* cmac);
bool aes_cmac_with_iv(
    uint8_t* key,
    size_t key_len,
    uint8_t* message,
    size_t message_len,
    uint8_t* iv,
    uint8_t* cmac);

bool aes_cmac_segments_with_iv(
    uint8_t* key,
    size_t key_len,
    const AesCmacSegment* segments,
    size_t segment_count,
    uint8_t* iv,
    uint8_t* cmac);
