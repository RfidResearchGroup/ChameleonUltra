#ifndef __EM_410X_DATA_H__
#define __EM_410X_DATA_H__

#include <stdbool.h>

#include "bsp_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARD_BUF_BYTES_SIZE 5  // Card byte buffer size

#define RAW_BUF_SIZE 24  // The maximum record buffer
#define CARD_BUF_SIZE 8  // Card size

#define EM410X_BUFFER_SIZE (128)

bool em410x_read(uint8_t *data, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
