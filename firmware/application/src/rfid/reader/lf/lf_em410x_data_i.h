#ifndef __EM_410X_DATA_I_H__
#define __EM_410X_DATA_I_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CARD_BUF_BYTES_SIZE 5       // Card byte buffer size

#define RAW_BUF_SIZE 24           // The maximum record buffer
#define CARD_BUF_SIZE 8           // Card size

typedef struct {
    uint8_t rawa[RAW_BUF_SIZE];    // The time difference between recording changes
    uint8_t rawb[RAW_BUF_SIZE];    // The time difference between recording changes
    uint8_t hexbuf[CARD_BUF_SIZE]; // Patriotic card data
    uint8_t startbit;
} RAWBUF_TYPE_S;

#ifdef __cplusplus
}
#endif

#endif
