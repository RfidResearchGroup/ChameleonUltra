#pragma once

#include "protocols.h"

extern const protocol indala;

uint8_t indala_t55xx_writer(uint8_t* uid, uint32_t* blks);
