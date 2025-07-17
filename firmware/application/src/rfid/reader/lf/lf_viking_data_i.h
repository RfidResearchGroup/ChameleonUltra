#ifndef __VIKING_DATA_I_H__
#define __VIKING_DATA_I_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CARD_BUF_BYTES_SIZE 4       // Card byte buffer size

#define RAW_BUF_SIZE 30           // The maximum record buffer
#define CARD_BUF_SIZE 8           // Card size

typedef struct {
    uint8_t rawa[RAW_BUF_SIZE];    // The time difference between recording changes
    uint8_t rawb[RAW_BUF_SIZE];    // The time difference between recording changes
    uint8_t hexbuf[CARD_BUF_SIZE]; // Patriotic card data
    uint8_t timebuf[RAW_BUF_SIZE*8];
    uint8_t startbit;
} RAWBUF_TYPE_S;

//Card data
// extern uint8_t cardbufbyte[CARD_BUF_BYTES_SIZE];

#ifdef __cplusplus
}
#endif

#endif
