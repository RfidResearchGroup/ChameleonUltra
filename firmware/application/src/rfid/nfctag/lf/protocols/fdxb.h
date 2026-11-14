#pragma once

#include <stddef.h>

#include "protocols.h"

/* Destuffed FDX-B frame: 13 bytes.
 *   [0..7]  64-bit payload (national ID 38, country 10, app bit 1,
 *           reserved 14, animal flag 1) -- all fields LSB-first
 *   [8..9]  CRC-16 over bytes 0..7, LSB-first
 *   [10..12] 24-bit trailer / application data
 */
#define FDXB_DATA_SIZE (13)

extern const protocol fdxb;

extern const protocol *fdxb_protocols[];
extern size_t fdxb_protocols_size;

uint8_t fdxb_t55xx_writer(uint8_t *fdxb_data, uint32_t *blks);
