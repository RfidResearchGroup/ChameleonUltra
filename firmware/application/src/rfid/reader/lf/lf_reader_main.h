#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_status.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"

void set_scan_tag_timeout(uint32_t ms);
uint8_t scan_em410x(uint8_t *uid);
uint8_t scan_hidprox(uint8_t *uid, uint8_t format_hint);
uint8_t scan_viking(uint8_t *uid);
uint8_t write_em410x_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_em410x_electra_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
uint8_t write_hidprox_to_t55xx(uint8_t format, uint32_t fc, uint64_t cn, uint32_t il, uint32_t oem, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count);
uint8_t write_viking_to_t55xx(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count);
