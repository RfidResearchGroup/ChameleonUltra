#pragma once

#include "protocols.h"

#define PARADOX_DATA_SIZE (6)
#define PARADOX_T55XX_BLOCK_COUNT (4)

extern const protocol paradox;
uint8_t paradox_t55xx_writer(uint8_t *data, uint32_t *blks);
