#ifndef __LF_READER_MAIN_H__
#define __LF_READER_MAIN_H__

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "lf_125khz_radio.h"
#include "lf_em410x_data.h"

void SetScanTagTimeout(uint32_t ms);

uint8_t scan_em410x(uint8_t *uid);
uint8_t write_em410x_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_hidprox_to_t55xx(uint8_t format, uint32_t fc, uint64_t cn, uint32_t il, uint32_t oem, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) ;
uint8_t scan_hidprox(uint8_t *uid, uint8_t format);
uint8_t debug_hidprox(uint8_t *data);

#endif
