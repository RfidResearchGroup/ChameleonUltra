#ifndef DESFIRE_H
#define DESFIRE_H

#include <stdint.h>
#include <stdbool.h>

bool desfire_scan(char *buffer, uint16_t max_len);

#endif
