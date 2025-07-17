#ifndef __LF_TAG_VIKING_H
#define __LF_TAG_VIKING_H

#include <stdbool.h>
#include "rfid_main.h"
#include "tag_emulation.h"


/**
 * Low -frequency analog card adjustment Manchester signal
 * The definition of the packaging tool macro only needs to be modulated 0 and 1
 */
#define LF_125KHZ_VIKING_BIT_SIZE   64
#define LF_125KHZ_VIKING_BIT_CLOCK  128     // RF/32
#define LF_VIKING_TAG_ID_SIZE       4

// REMOVED, as not specific...
// #define LF_125KHZ_BROADCAST_MAX     10      // 32.768ms once, about 31 times in one second
// bool lf_is_field_exists(void);

int lf_tag_viking_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_viking_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type);
void lf_tag_125khz_viking_sense_switch(bool enable);

#endif
