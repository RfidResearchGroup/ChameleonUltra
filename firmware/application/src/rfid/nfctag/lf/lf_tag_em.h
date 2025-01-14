#ifndef __LF_TAG_H
#define __LF_TAG_H

#include <stdbool.h>
#include "rfid_main.h"
#include "tag_emulation.h"


/**
 * Low -frequency analog card adjustment Manchester signal
 * The definition of the packaging tool macro only needs to be modulated 0 and 1
 */
#define LF_125KHZ_EM410X_BIT_SIZE   64
#define LF_125KHZ_BROADCAST_MAX     10      // 32.768ms once, about 31 times in one second
#define LF_125KHZ_EM410X_BIT_CLOCK  256
#define LF_EM410X_TAG_ID_SIZE       5


void lf_tag_125khz_sense_switch(bool enable);
int lf_tag_em410x_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type);
bool lf_is_field_exists(void);

#endif
