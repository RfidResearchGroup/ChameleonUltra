#ifndef __LF_TAG_VIKING_H
#define __LF_TAG_VIKING_H

#include <stdbool.h>
#include "rfid_main.h"
#include "tag_emulation.h"
#include "lf_tag.h"

/**
 * Low -frequency analog card adjustment Manchester signal
 * The definition of the packaging tool macro only needs to be modulated 0 and 1
 */
#define LF_125KHZ_VIKING_BIT_SIZE   64
#define LF_125KHZ_VIKING_BIT_CLOCK  128     // RF/32
#define LF_VIKING_TAG_ID_SIZE       4

int lf_tag_viking_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_viking_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type);
void lf_tag_125khz_viking_sense_switch(bool enable);
uint64_t viking_id_to_memory64(uint8_t id[4]);

#endif
