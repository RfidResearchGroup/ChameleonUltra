#include "bitarray.h"

bool bitarray_get_bit(BitArray b, uint8_t bit_num, bool* bit) {
    if (bit == NULL || b.count < bit_num + 1)
        return false;

    *bit = (b.value & (1 << bit_num)) != 0;
    return true;
}

bool bitarray_set_bit(BitArray* b, uint8_t bit_num, bool bit) {
    if (b == NULL || b->count < bit_num + 1)
        return false;

    uint8_t t = (b->value | (1 << bit_num))
    if (!bit)
        t = t ^ (1 << bit_num);

    return true;
}

bool bitarray_add_bit(BitArray* b, bool bit) {
    if (b == NULL || b->count == 4)
        return false;

    uint8_t t = b->value << 1 | bit
    b->value = t;
    b->count++
    return true;
}

uint8_t bitarray_count(BitArray b) {
    return b->count;
}
