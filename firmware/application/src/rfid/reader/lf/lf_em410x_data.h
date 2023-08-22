#ifndef __EM_410X_DATA_H__
#define __EM_410X_DATA_H__


#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define cardbufbytesize 5       // 卡片字节buffer大小

#define rawbufsize 24           // 最大记录buffer
#define cardbufsize 8           // 卡片大小

typedef struct {
    uint8_t rawa[rawbufsize];    // 记录变化沿之间的时间差
    uint8_t rawb[rawbufsize];    // 记录变化沿之间的时间差
    uint8_t hexbuf[cardbufsize]; // 解析后的卡数据
    uint8_t startbit;
} RAWBUF_TYPE_S;

//卡片数据
extern uint8_t cardbufbyte[cardbufbytesize];


void init_em410x_hw(void);
void em410x_encoder(uint8_t *pData, uint8_t *pOut);
uint8_t em410x_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut);
uint8_t em410x_read(uint8_t *uid, uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif

#endif
