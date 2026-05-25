/*
 * app_cmd_standalone.c
 *
 * Host command handlers for the standalone subsystem.
 * Bound to DATA_CMD_STANDALONE_* (7000-7006) in m_data_cmd_map[] in app_cmd.c.
 *
 * Wire formats are little-endian (matches existing CU commands).
 */

#include "app_standalone.h"
#include "app_cmd.h"
#include "data_cmd.h"
#include "app_status.h"
#include "dataframe.h"

#include <string.h>

#include "nrf_log.h"

/* Max bytes returned per GET_RESULT call. The mode tracks its own cursor;
 * host calls CLEAR_RESULT to reset and reads repeatedly until the response
 * payload is empty. Kept well under BLE notification MTU. */
#define STANDALONE_RESULT_CHUNK_MAX     200

/* -------------------------------------------------------------------------
 * Return-code -> Status mapping
 * ------------------------------------------------------------------------- */

static uint16_t rc_to_status(standalone_rc_t rc) {
    switch (rc) {
        case STANDALONE_RC_OK:              return STATUS_SUCCESS;
        case STANDALONE_RC_BUSY:            return STATUS_DEVICE_MODE_ERROR;
        case STANDALONE_RC_INVALID_STATE:   return STATUS_DEVICE_MODE_ERROR;
        case STANDALONE_RC_NOT_PERMITTED:   return STATUS_PAR_ERR;
        case STANDALONE_RC_INVALID_CFG:     return STATUS_PAR_ERR;
        case STANDALONE_RC_NO_TAG:          return STATUS_HF_TAG_NO;
        case STANDALONE_RC_NO_FREE_SLOT:    return STATUS_PAR_ERR;
        case STANDALONE_RC_WRITE_FAIL:      return STATUS_FLASH_WRITE_FAIL;
        case STANDALONE_RC_BUFFER_FULL:     return STATUS_PAR_ERR;
        case STANDALONE_RC_NO_RESULT:       return STATUS_SUCCESS;  /* empty */
        case STANDALONE_RC_INTERNAL:        /* fallthrough */
        default:                            return STATUS_NOT_IMPLEMENTED;
    }
}

/* -------------------------------------------------------------------------
 * Handlers
 * ------------------------------------------------------------------------- */

/* 7000 GET_MODE
 * Request:  empty
 * Response: { u8 state, u8 mode, u8 flags, u8 reserved }
 */
data_frame_tx_t *cmd_handler_standalone_get_mode(uint16_t cmd, uint16_t status,
                                                 uint16_t length, uint8_t *data) {
    (void)status; (void)length; (void)data;
    uint8_t resp[4] = {
        (uint8_t)app_standalone_get_state(),
        (uint8_t)app_standalone_get_mode(),
        app_standalone_get_flags(),
        0,
    };
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(resp), resp);
}

/* 7001 SET_MODE
 * Request:  { u8 mode, u8 flags }
 * Response: { u8 state, u8 mode, u8 flags, u8 reserved }
 */
data_frame_tx_t *cmd_handler_standalone_set_mode(uint16_t cmd, uint16_t status,
                                                 uint16_t length, uint8_t *data) {
    (void)status;
    if (length != 2 || data == NULL) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    standalone_mode_t mode = (standalone_mode_t)data[0];
    uint8_t           flgs = data[1];

    standalone_rc_t rc = app_standalone_set_mode(mode, flgs);
    if (rc != STANDALONE_RC_OK) {
        return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
    }

    uint8_t resp[4] = {
        (uint8_t)app_standalone_get_state(),
        (uint8_t)app_standalone_get_mode(),
        app_standalone_get_flags(),
        0,
    };
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(resp), resp);
}

/* 7002 GET_CONFIG
 * Request:  { u8 mode }
 * Response: { u8 mode, u8 cfg_len, u8[cfg_len] cfg }
 */
data_frame_tx_t *cmd_handler_standalone_get_config(uint16_t cmd, uint16_t status,
                                                   uint16_t length, uint8_t *data) {
    (void)status;
    if (length != 1 || data == NULL) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    standalone_mode_t mode = (standalone_mode_t)data[0];
    uint8_t  resp[STANDALONE_RESULT_CHUNK_MAX];
    size_t   cfg_len = 0;

    standalone_rc_t rc = app_standalone_get_config(mode, resp + 2,
                                                   sizeof(resp) - 2, &cfg_len);
    if (rc == STANDALONE_RC_NO_RESULT) {
        resp[0] = (uint8_t)mode;
        resp[1] = 0;
        return data_frame_make(cmd, STATUS_SUCCESS, 2, resp);
    }
    if (rc != STANDALONE_RC_OK) {
        return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
    }

    resp[0] = (uint8_t)mode;
    resp[1] = (uint8_t)cfg_len;
    return data_frame_make(cmd, STATUS_SUCCESS, 2 + cfg_len, resp);
}

/* 7003 SET_CONFIG
 * Request:  { u8 mode, u8[] cfg }
 * Response: empty
 */
data_frame_tx_t *cmd_handler_standalone_set_config(uint16_t cmd, uint16_t status,
                                                   uint16_t length, uint8_t *data) {
    (void)status;
    if (length < 1 || data == NULL) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    standalone_mode_t mode    = (standalone_mode_t)data[0];
    const uint8_t    *cfg     = (length > 1) ? &data[1] : NULL;
    size_t            cfg_len = length - 1;

    /* Sanity-check length before handing off to FDS - a cfg blob larger
     * than our staging buffer would just trip persist_config_save's
     * STANDALONE_RC_INVALID_CFG check, but bouncing it here keeps the
     * status code accurate (PAR_ERR vs INTERNAL). */
    if (cfg_len > 64 /* STANDALONE_CONFIG_MAX_BYTES */) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    standalone_rc_t rc = app_standalone_set_config(mode, cfg, cfg_len);
    return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
}

/* 7004 GET_RESULT
 * Request:  empty
 * Response: { u32 total_size_le, u8[] chunk }
 *
 * Read cursor is mode-internal. Repeat-call until chunk is empty.
 */
data_frame_tx_t *cmd_handler_standalone_get_result(uint16_t cmd, uint16_t status,
                                                   uint16_t length, uint8_t *data) {
    (void)status; (void)length; (void)data;

    uint8_t resp[4 + STANDALONE_RESULT_CHUNK_MAX];
    size_t  chunk_len = 0;

    standalone_rc_t rc = app_standalone_read_result(resp + 4,
                                                    STANDALONE_RESULT_CHUNK_MAX,
                                                    &chunk_len);
    if (rc == STANDALONE_RC_NO_RESULT) {
        memset(resp, 0, 4);
        return data_frame_make(cmd, STATUS_SUCCESS, 4, resp);
    }
    if (rc != STANDALONE_RC_OK) {
        return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
    }

    /* total_size = this chunk's byte count. Host loops until it receives
     * an empty chunk (4 zero bytes), which the firmware sends when the
     * read cursor reaches the end of the buffer. */
    uint32_t total = (uint32_t)chunk_len;
    resp[0] = (uint8_t)(total      );
    resp[1] = (uint8_t)(total >>  8);
    resp[2] = (uint8_t)(total >> 16);
    resp[3] = (uint8_t)(total >> 24);

    return data_frame_make(cmd, STATUS_SUCCESS, 4 + chunk_len, resp);
}

/* 7005 CLEAR_RESULT */
data_frame_tx_t *cmd_handler_standalone_clear_result(uint16_t cmd, uint16_t status,
                                                     uint16_t length, uint8_t *data) {
    (void)status; (void)length; (void)data;
    standalone_rc_t rc = app_standalone_clear_result();
    return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
}

/* 7006 TRIGGER */
data_frame_tx_t *cmd_handler_standalone_trigger(uint16_t cmd, uint16_t status,
                                                uint16_t length, uint8_t *data) {
    (void)status; (void)length; (void)data;
    standalone_rc_t rc = app_standalone_trigger();
    return data_frame_make(cmd, rc_to_status(rc), 0, NULL);
}
