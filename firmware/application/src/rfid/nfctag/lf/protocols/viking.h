#pragma once

#include "protocols.h"

extern const protocol viking;

uint8_t viking_t55xx_writer(uint8_t* uid, uint32_t* blks);