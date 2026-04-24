#pragma once

#include "protocols.h"

// IDTECK frame layout: 32-bit fixed preamble + 32-bit card payload.
#define IDTECK_DATA_SIZE   (8)             // 8 bytes = 64 bits on air
#define IDTECK_BIT_COUNT   (64)
#define IDTECK_PREAMBLE    (0x4944544BU)   // ASCII "IDTK", MSB first on air

typedef struct {
    uint8_t data[IDTECK_DATA_SIZE];
} idteck_codec;

extern const protocol idteck;

uint8_t idteck_t55xx_writer(uint8_t *uid, uint32_t *blks);
