#pragma once

#include <stdbool.h>

#include "rfid_main.h"
#include "tag_emulation.h"

#define LF_EM410X_TAG_ID_SIZE 5
#define LF_HIDPROX_TAG_ID_SIZE 13

void lf_tag_125khz_sense_switch(bool enable);
int lf_tag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_hidprox_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_hidprox_data_factory(uint8_t slot, tag_specific_type_t tag_type);
bool is_lf_field_exists(void);
