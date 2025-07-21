#ifndef __LF_MANCHESTER_H__
#define __LF_MANCHESTER_H__

#include "data_utils.h"
#include "bsp_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

uint8_t mcst(uint8_t *rawa, uint8_t *rawb, uint8_t *hexbuf, uint8_t startbit, uint8_t rawbufsize, uint8_t sync);

#ifdef __cplusplus
}
#endif

#endif
