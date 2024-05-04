#ifndef __LF_READ_H__
#define __LF_READ_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define LF_CARD_BUF_SIZE (512)

void lf_read_init_hw(void);
void lf_read_encoder(uint8_t *pData, uint8_t *pOut);
uint8_t lf_read_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut);
uint8_t lf_read_reader(uint8_t *uid, uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif

#endif
