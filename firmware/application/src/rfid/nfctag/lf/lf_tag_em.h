#ifndef __LF_TAG_H
#define __LF_TAG_H

#include <stdbool.h>
#include "rfid_main.h"
#include "tag_emulation.h"


/**
 * 低频模拟卡调制曼彻斯特信号
 * 封装工具宏定义只需要调制0和1
 */
#define LF_125KHZ_EM410X_BIT_SIZE   64
#define LF_125KHZ_BORADCAST_MAX     3      // 32.768ms一次，一秒大概能广播31次
#define LF_125KHZ_EM410X_BIT_CLOCK  256
#define LF_EM410X_TAG_ID_SIZE       5


void lf_tag_125khz_sense_switch(bool enable);
int lf_tag_em410x_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type);
bool lf_is_field_exists(void);

#endif
