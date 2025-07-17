#ifndef __VIKING_DATA_H__
#define __VIKING_DATA_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

void init_viking_hw(void);
void viking_encoder(uint8_t *pData, uint8_t *pOut);
uint8_t viking_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut);
uint8_t viking_read(uint8_t *uid, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
