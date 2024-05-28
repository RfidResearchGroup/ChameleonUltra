#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t count : 2;
    uint32_t value : 6;
} BitArray;

void bitarray_clear(BitArray* b);
bool bitarray_set_bit(BitArray* b, uint8_t bit_num, bool bit);
bool bitarray_add_bit(BitArray* b, bool bit);
uint8_t bitarray_count(BitArray b);

#ifdef __cplusplus
}
#endif
