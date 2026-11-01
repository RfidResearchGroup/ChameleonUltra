#pragma once

#include "protocols.h"

#define PRESCO_DATA_SIZE 4
#define PRESCO_FRAME_BITS 128

extern const protocol presco;
uint8_t presco_t55xx_writer(uint8_t *code, uint32_t *blocks);
