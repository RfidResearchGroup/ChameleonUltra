#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocols.h"

#define IOPROX_DATA_SIZE (8)

extern const protocol ioprox;
bool ioprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms);
