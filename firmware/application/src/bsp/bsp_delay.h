#ifndef __DELAY_H__
#define __DELAY_H__

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_delay_init(void);
void bsp_delay_ms(uint16_t nms);
void bsp_delay_us(uint32_t nus);

#ifdef __cplusplus
}
#endif

#endif
