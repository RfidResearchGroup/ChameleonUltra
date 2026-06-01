#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>
#include "dataframe.h"


typedef data_frame_tx_t *(*cmd_processor)(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

typedef struct {
    uint16_t cmd;
    cmd_processor cmd_before;
    cmd_processor cmd_processor;
    cmd_processor cmd_after;
} cmd_data_map_t;

void on_data_frame_received(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

/* -------------------------------------------------------------------------
 * Reader-side MIFARE Classic auth trace.
 *
 * Polls for a tag in the field with up to `timeout_ms` of patience, then
 * performs a single auth attempt capturing every TX/RX wire frame
 * (REQA/ATQA/anticoll/SELECT/SAK/RATS/ATS/auth/NT/NR||AR/AT) into the
 * static auth_trace buffer.
 *
 * Returns:
 *   STATUS_HF_TAG_OK   tag found, auth completed (full trace captured)
 *   STATUS_HF_TAG_NO   no tag found within timeout
 *   STATUS_MF_ERR_AUTH auth failed (partial trace still captured)
 *   STATUS_HF_ERR_STAT wire failure mid-auth (no NT)
 *   STATUS_PAR_ERR     invalid type
 *
 * Caller is responsible for ensuring reader mode is active and the
 * antenna is powered (typically via before_hf_reader_run /
 * after_hf_reader_run hooks).
 */
uint8_t hf14a_auth_trace_run(uint8_t type, uint8_t block,
                             const uint8_t key[6], uint32_t timeout_ms);

/* Pointer to the static auth-trace buffer and the number of valid bytes
 * captured by the most recent hf14a_auth_trace_run() call. The pointer
 * is valid until the next run(). */
const uint8_t *hf14a_auth_trace_get_buf(uint16_t *out_len);

/* -------------------------------------------------------------------------
 * Standalone subsystem command handlers (see app_cmd_standalone.c).
 * Bound to DATA_CMD_STANDALONE_* (7000-7006) in the m_data_cmd_map[] table.
 */
data_frame_tx_t *cmd_handler_standalone_get_mode    (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_set_mode    (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_get_config  (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_set_config  (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_get_result  (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_clear_result(uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_trigger     (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_get_sizes   (uint16_t, uint16_t, uint16_t, uint8_t *);
data_frame_tx_t *cmd_handler_standalone_relay_diag   (uint16_t, uint16_t, uint16_t, uint8_t *);

#endif
