#include <stdint.h>


uint64_t atoui(const char *str) {

    uint64_t result = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') {
            result = result * 10 + str[i] - '0';
        }
    }
    return result;
}

void num_to_bytes(uint64_t n, uint32_t len, uint8_t *dest) {
    while (len--) {
        dest[len] = (uint8_t)n;
        n >>= 8;
    }
}
