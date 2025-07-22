#ifndef __LF_HIDPROX_DATA_H__
#define __LF_HIDPROX_DATA_H__

#include <stdbool.h>

#include "bsp_time.h"

#ifdef __cplusplus
extern "C" {
#endif

bool hidprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms);
bool hidprox_debug(uint8_t *data, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
