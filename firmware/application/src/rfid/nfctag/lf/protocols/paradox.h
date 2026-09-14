#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocols.h"

// Stored emulator payload (word-aligned):
//   0:   facility code
//   1-2: card number (big-endian)
//   3:   reserved
#define PARADOX_DATA_SIZE 4
#define PARADOX_RAW_SIZE 12

extern const protocol paradox;

// Encode a Paradox credential into the complete 96-bit FSK2a on-air frame.
bool paradox_encode_raw(uint8_t facility_code, uint16_t card_number,
                        uint8_t raw[PARADOX_RAW_SIZE]);
