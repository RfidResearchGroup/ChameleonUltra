#ifndef __CRC_UTILS_H
#define __CRC_UTILS_H

#include <stdint.h>

void calc_14a_crc_lut(uint8_t* data, int length, uint8_t* output);

#endif
