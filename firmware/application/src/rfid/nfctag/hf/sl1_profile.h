#pragma once
#include <stdbool.h>
#include <stdint.h>

uint8_t sl1_profile_begin(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_write(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_save(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_cancel(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_clear(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_set_enabled(const uint8_t *data, uint16_t length);
uint8_t sl1_profile_status(const uint8_t *data, uint16_t length, uint8_t out[14]);
bool sl1_profile_invalidate(uint8_t slot);
void sl1_profile_load_active(void);
void sl1_profile_poll(void);
/* Tag-emulation owner uses this to defer sensing until nonce preparation. */
void sl1_profile_sense_request(bool enable);
/* Persist an invalidation before a changed legacy image is saved. */
bool sl1_profile_before_legacy_save(uint8_t slot, uint16_t type, const void *image);

/* Presence of the durable transaction record blocks HF owners. */
bool sl1_profile_hf_incomplete(uint8_t slot);
