#pragma once

#include <stdbool.h>

#include "rfid_main.h"
#include "tag_emulation.h"

#define LF_EM410X_TAG_ID_SIZE 5
#define LF_EM410X_ELECTRA_TAG_ID_SIZE 13
#define LF_IOPROX_TAG_ID_SIZE 16
#define LF_HIDPROX_TAG_ID_SIZE 13
#define LF_VIKING_TAG_ID_SIZE 4
#define LF_PAC_TAG_ID_SIZE 8
#define LF_JABLOTRON_TAG_ID_SIZE 5
#define LF_IDTECK_TAG_ID_SIZE 8
#define LF_PYRAMID_TAG_ID_SIZE 16

void lf_tag_125khz_sense_switch(bool enable);
/* Thread-only: confirms PWM stopped before invalidating its DMA source. */
void lf_tag_data_clear(void);
typedef enum {
    LF_RAW_DIAGNOSTIC_IDLE,
    LF_RAW_DIAGNOSTIC_RUNNING,
    LF_RAW_DIAGNOSTIC_STOPPED,
} lf_raw_diagnostic_status_t;
/* Experimental thread-only API. Requires loaded raw data and LF sense enabled.
 * start accepts 1..5000 ms, rounds DOWN to whole frames (false if none fit),
 * never loops indefinitely. stop confirms DMA stopped before returning.
 * start rejects IRQ context; stop is a no-op there (no blocking from IRQ).
 * Rejects reader mode and an already RUNNING diagnostic. STOPPED remains until
 * explicit start/load; stop/expiry re-arm normal field detection without replay.
 * Normal raw resumes on the next field UP edge (remove/reapply existing field).
 * status may be read from IRQ. No reader compatibility promised. */
bool lf_tag_raw_diagnostic_start(uint32_t ms);
void lf_tag_raw_diagnostic_stop(void);
lf_raw_diagnostic_status_t lf_tag_raw_diagnostic_status(void);
int lf_tag_raw_manchester_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_raw_manchester_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_hidprox_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_hidprox_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_ioprox_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_ioprox_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_viking_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_pac_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_pac_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_jablotron_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_jablotron_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_idteck_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_idteck_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int lf_tag_pyramid_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool lf_tag_pyramid_data_factory(uint8_t slot, tag_specific_type_t tag_type);
bool is_lf_field_exists(void);
