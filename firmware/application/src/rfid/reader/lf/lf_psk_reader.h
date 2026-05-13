#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocols/protocols.h"

bool psk_generic_read(const protocol *p, uint8_t *data, uint32_t timeout_ms);
