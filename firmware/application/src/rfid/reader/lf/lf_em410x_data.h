#ifndef __EM_410X_DATA_H__
#define __EM_410X_DATA_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define cardbufbytesize 5       // Card byte buffer size

#define rawbufsize 24           // The maximum record buffer
#define cardbufsize 8           // Card size

typedef struct {
    uint8_t rawa[rawbufsize];    // The time difference between recording changes
    uint8_t rawb[rawbufsize];    // The time difference between recording changes
    uint8_t hexbuf[cardbufsize]; // Patriotic card data
    uint8_t startbit;
} RAWBUF_TYPE_S;

//Card data
extern uint8_t cardbufbyte[cardbufbytesize];


void init_em410x_hw(void);
void em410x_encoder(uint8_t *pData, uint8_t *pOut);
uint8_t em410x_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut);
uint8_t em410x_read(uint8_t *uid, uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif

#endif
