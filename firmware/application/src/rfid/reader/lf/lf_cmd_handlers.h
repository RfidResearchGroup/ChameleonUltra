#ifndef LF_CMD_HANDLERS_H
#define LF_CMD_HANDLERS_H

#include <stdint.h>
#include "data_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// LF Command Handler Function Declarations
// These functions replace the "not implemented" stubs in app_cmd.c

// EM410x command handlers
static data_frame_tx_t *cmd_lf_em410x_read(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
static data_frame_tx_t *cmd_lf_em410x_simulate(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// T55xx command handlers  
static data_frame_tx_t *cmd_lf_t55xx_read_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
static data_frame_tx_t *cmd_lf_t55xx_write_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// Scanner command handlers
static data_frame_tx_t *cmd_lf_scan_auto(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// HID Prox command handlers
static data_frame_tx_t *cmd_lf_hid_prox_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
static data_frame_tx_t *cmd_lf_hid_prox_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// Indala command handlers
static data_frame_tx_t *cmd_lf_indala_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// Raw LF command handlers
static data_frame_tx_t *cmd_lf_read_raw(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
static data_frame_tx_t *cmd_lf_tune_antenna(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// Initialization
static data_frame_tx_t *cmd_lf_init(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif // LF_CMD_HANDLERS_H

