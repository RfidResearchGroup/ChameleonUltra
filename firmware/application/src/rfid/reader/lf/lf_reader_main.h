#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "lf_125khz_radio.h"
#if defined(PROJECT_CHAMELEON_ULTRA)
#include "lf_em4x05_data.h"
#endif
#include "lf_reader_data.h"

void set_scan_tag_timeout(uint32_t ms);
uint8_t scan_em410x(uint8_t *uid);
uint8_t scan_ioprox(uint8_t *uid, uint8_t format_hint);
uint8_t decode_ioprox_raw(uint8_t *raw8, uint8_t *output);
uint8_t encode_ioprox_params(uint8_t ver, uint8_t fc, uint16_t cn, uint8_t *out);
uint8_t scan_hidprox(uint8_t *uid, uint8_t format_hint);
uint8_t scan_pac(uint8_t *card_id);
uint8_t scan_viking(uint8_t *uid);
uint8_t write_em410x_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_em410x_electra_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_hidprox_to_t55xx(uint8_t format, uint32_t fc, uint64_t cn, uint32_t il, uint32_t oem, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count);
uint8_t write_ioprox_to_t55xx(uint8_t *raw_data, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count);
uint8_t write_viking_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_pac_to_t55xx(uint8_t *data, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count);
uint8_t write_idteck_to_t55xx(uint8_t *data, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count);
#if defined(PROJECT_CHAMELEON_ULTRA)
uint8_t lf_t55xx_write_block(uint8_t block, uint32_t word, uint32_t passwd, bool use_passwd, bool page1);
#endif
