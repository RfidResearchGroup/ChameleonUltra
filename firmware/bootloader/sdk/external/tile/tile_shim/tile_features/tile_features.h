/**
 * NOTICE
 * 
 * Copyright 2020 Tile Inc.  All Rights Reserved.
 * All code or other information included in the accompanying files ("Tile Source Material")
 * is PROPRIETARY information of Tile Inc. ("Tile") and access and use of the Tile Source Material
 * is subject to these terms. The Tile Source Material may only be used for demonstration purposes,
 * and may not be otherwise distributed or made available to others, including for commercial purposes.
 * Without limiting the foregoing , you understand and agree that no production use
 * of the Tile Source Material is allowed without a Tile ID properly obtained under a separate
 * agreement with Tile.
 * You also understand and agree that Tile may terminate the limited rights granted under these terms
 * at any time in its discretion.
 * All Tile Source Material is provided AS-IS without warranty of any kind.
 * Tile does not warrant that the Tile Source Material will be error-free or fit for your purposes.
 * Tile will not be liable for any damages resulting from your use of or inability to use
 * the Tile Source Material.
 *
 * Support: firmware_support@tile.com
 */

/**
 * @file tile_features.h
 * @brief Support for features in Tile Lib
 */

#ifndef TILE_FEATURES_H_
#define TILE_FEATURES_H_

#include "app_timer.h"
#include "tile_gatt_db/tile_gatt_db.h"
#include "drivers/tile_timer_driver.h"
#include "modules/tile_test_module.h"


typedef struct
{
  tile_gatt_db_t service;
  uint16_t conn_handle;
} tile_ble_env_t;

enum CUSTOM_EVENTS
{
  NOTIFICATION_WRITTEN_EVT
};

struct my_evt
{
  uint8_t type;
};

enum TILE_APP_TEST_CMDS
{
  TEST_CMD_REBOOT = TILE_TEST_MODULE_CODE_BASE,
  TEST_CMD_STORAGE,
};

/**
 * @brief Types of reboots which can be triggered by \ref TEST_CMD_REBOOT
 */
enum TEST_REBOOT
{
  TEST_CMD_REBOOT_RESET         = 0x00,
  TEST_CMD_REBOOT_WATCHDOG      = 0x01,
  TEST_CMD_REBOOT_MEMORY_FAULT  = 0x02,
  TEST_CMD_REBOOT_OTHER         = 0x03,
  TEST_CMD_REBOOT_ASSERT        = 0x04,
  TEST_CMD_REBOOT_DURING_FLASH  = 0x05,
};

extern tile_ble_env_t tile_ble_env;
extern app_timer_id_t tile_timer_id[TILE_MAX_TIMERS];
void tile_features_init(void);
void tile_button_was_pressed(void);
int tile_read_button_state(uint8_t *button_state);
void tile_update_tileID_char(void);

#endif
