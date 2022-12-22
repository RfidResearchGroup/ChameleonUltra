#ifndef _LFCOPIER_H_
#define _LFCOPIER_H_

#include <stdint.h>


#include "lf_em410x_data.h"
#include "lf_t55xx_data.h"
#include "app_status.h"

extern uint32_t g_timeout_readem_ms;

void SetEMScanTagTimeout(uint32_t ms);

uint8_t PcdScanEM410X(uint8_t* uid);
uint8_t PcdWriteT55XX(uint8_t* uid, uint8_t* newkey, uint8_t* old_keys, uint8_t old_key_count);

#endif
