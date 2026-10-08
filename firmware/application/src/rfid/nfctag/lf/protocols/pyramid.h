#pragma once

#include "protocols.h"
#include "utils/fskdemod.h"

// Farpointe/Keri Pyramid: FSK2a, RF/50, 128-bit frame.
// Stored id mirrors the Flipper layout: [format][facility][card_hi][card_lo].
#define LF_PYRAMID_TAG_ID_SIZE (4)

extern const protocol pyramid;

// Build the 128-bit (16 byte) Pyramid frame from a 4-byte stored id.
void pyramid_encode_frame(const uint8_t *id, uint8_t *frame16);

uint8_t pyramid_t55xx_writer(const uint8_t *id, uint32_t *blks);
