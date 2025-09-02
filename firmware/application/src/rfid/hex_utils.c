#include "hex_utils.h"

/**
 * @brief Convert the large number to the hex byte array
 * @param n    : The value of the conversion
 * @param len  : The byte length of the value after the conversion is stored
 * @param dest : Caps that store conversion results
 * @retval none
 *
 */
void num_to_bytes(uint64_t n, uint8_t len, uint8_t *dest)
{
    while (len--) {
        dest[len] = (uint8_t)n;
        n >>= 8;
    }
}

/**
 * @brief Convert byte array to large number
 * @param len  : The byte length of the buffer of the value of the value
 * @param src  : Byte buffer stored in the numerical
 * @retval Converting result
 *
 */
uint64_t bytes_to_num(uint8_t *src, uint8_t len)
{
    uint64_t num = 0;
    while (len--) {
        num = (num << 8) | (*src);
        src++;
    }
    return num;
}
