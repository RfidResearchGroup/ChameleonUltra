#ifndef __T_55XX_DATA_H__
#define __T_55XX_DATA_H__


#include "data_utils.h"

#ifdef __cplusplus
extern "C"
{
#endif

void init_t55xx_hw(void);
void T55xx_Reset_Passwd(uint8_t *oldpasswd, uint8_t *newpasswd);
void T55xx_Write_data(uint8_t *passwd, uint8_t *datas, uint32_t config);

#ifdef __cplusplus
}
#endif

#endif
