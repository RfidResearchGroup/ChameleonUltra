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
 * @file tile_service.c
 * @brief Core functionality for Tile Lib
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)
#include "ble_hci.h"
#include "nrf_delay.h"
#include "app_timer.h"
#include "app_scheduler.h"
#include "boards.h"
#include "nrf_sdh_ble.h"
#include "nrf_log.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_rng.h"


#include "tile_service/tile_service.h"
#include "tile_config.h"
#include "tile_features/tile_features.h"
#include "tile_gatt_db/tile_gatt_db.h"
#include "tile_storage/tile_storage.h"
#include "tile_assert/tile_assert.h"
#include "tile_player/tile_player.h"

// TileLib includes
#include "tile_lib.h"
#include "drivers/tile_gap_driver.h"
#include "drivers/tile_timer_driver.h"
#include "modules/tile_tmd_module.h"
#include "modules/tile_toa_module.h"

#include <stdbool.h>
#include <string.h> 


/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

static void tile_timer_timeout_handler(void *p_context);

/*******************************************************************************
 * Defines & types
 ******************************************************************************/
 
#define APP_BLE_TILE_OBSERVER_PRIO      3


/*******************************************************************************
 * Global variables
 ******************************************************************************/
static app_timer_t tile_timer_data[TILE_MAX_TIMERS] = { 0 };

/*******************************************************************************
 * Local variables
 ******************************************************************************/


/*******************************************************************************
 * Functions
 ******************************************************************************/

/**
 * @brief Initialize Tile BLE service
 */
void tile_service_init(void)
{
  nrf_drv_rng_config_t rng_config = NRF_DRV_RNG_DEFAULT_CONFIG;

  /* Initialize Tile timers */
  for(int i = 0; i < TILE_MAX_TIMERS; i++)
  {
    tile_timer_id[i] = &tile_timer_data[i];
    /* We use one universal timeout handler with p_context containing the timer ID for dispatch to the shim */
    (void) app_timer_create(&tile_timer_id[i], APP_TIMER_MODE_SINGLE_SHOT, tile_timer_timeout_handler);
  }

  tile_storage_init(); // Initialize storage before initializing features

  tile_features_init();
  tile_ble_env.conn_handle = BLE_CONN_HANDLE_INVALID;

  /* Audio Config */
  nrfx_err_t err_code = nrf_drv_ppi_init();
  APP_ERROR_CHECK(err_code);
  tile_boot_config_player();

  /* Register Tile Service Characteristics */
  tile_gatt_db_init(&tile_ble_env.service);
  
  /* Initialize RNG driver */
  err_code = nrf_drv_rng_init(&rng_config);
  APP_ERROR_CHECK(err_code);

  /* Register a handler for BLE events */
  NRF_SDH_BLE_OBSERVER(m_ble_observer2, APP_BLE_TILE_OBSERVER_PRIO, tile_on_ble_evt, NULL);

  /* Play Tile Wakeup Song when Tile Service Inits and the Tile Node is not activated.
     This may be disabled depending on the application requirements */
  if(TILE_MODE_ACTIVATED != tile_checked->mode)
  {
    (void) PlaySong(TILE_SONG_WAKEUP, 3, TILE_SONG_DURATION_ONCE);
  }
}

/**
 * @brief Handle Tile BLE events
 *
 * @param[in] p_evt    Event forwarded from BLE stack.
 */
void tile_on_ble_evt(ble_evt_t const * p_evt, void * p_context)
{
  struct tile_conn_params params;
  uint16_t handle;

  switch (p_evt->header.evt_id)
  {
    case BLE_GAP_EVT_DISCONNECTED:
      tile_ble_env.conn_handle = BLE_CONN_HANDLE_INVALID;
      tile_unchecked->disconnect_count++;
      (void) tile_gap_disconnected();
      /********************************
      After disconnect, initialize the advertising payload,
      This is done in case the Tile Mode changes between Shipping and Advertising, it reflects in the advertising payload.
    
      If we advertise 0xFEEC, instead of 0xFEED, this will cause discoverability issues in following cases:
        a. Community Find
        b. Access Points
        c. If owner tries to discover activated Tiles on another phone, from same account.
      Phone from where Tile was activated might still be able to connect as it may read the Mac address and not the advertising payload.
    
      If we advertise 0xFEED, instead of 0xFEEC, this will cause issues after decommissioning, and we will not be able to commission/activate again
      ********************************/

      break; // BLE_GAP_EVT_DISCONNECTED

    case BLE_GAP_EVT_CONNECTED:
      /* Save connection handle */
      tile_ble_env.conn_handle = p_evt->evt.gap_evt.conn_handle;
      tile_unchecked->connection_count++;
      if(TILE_MODE_ACTIVATED != tile_checked->mode)
      {
        //  when the Tile is not activated, the Interim TileID, Key is used.
        memcpy(tile_checked->tile_id, interim_tile_id, TILE_ID_LEN);
        memcpy(tile_checked->tile_auth_key, interim_tile_key, TILE_AUTH_KEY_LEN);
      }
      // Update the TileID Char
      tile_update_tileID_char();
      /* Tell Tile Lib about the connection */
      params.conn_interval = p_evt->evt.gap_evt.params.connected.conn_params.max_conn_interval;
      params.slave_latency = p_evt->evt.gap_evt.params.connected.conn_params.slave_latency;
      params.conn_sup_timeout = p_evt->evt.gap_evt.params.connected.conn_params.conn_sup_timeout;
      (void) tile_gap_connected(&params);

      break; // BLE_GAP_EVT_CONNECTED

    case BLE_GAP_EVT_CONN_PARAM_UPDATE:
      params = (struct tile_conn_params)
      {
        .conn_interval    = p_evt->evt.gap_evt.params.conn_param_update.conn_params.max_conn_interval,
        .slave_latency    = p_evt->evt.gap_evt.params.conn_param_update.conn_params.slave_latency,
        .conn_sup_timeout = p_evt->evt.gap_evt.params.conn_param_update.conn_params.conn_sup_timeout,
      };
      (void) tile_gap_params_updated(&params);
      break;

    case BLE_GATTS_EVT_WRITE:
      // Find which ID is associated with the handle
      handle = p_evt->evt.gatts_evt.params.write.handle;

      for(int i = 0; i < TILE_NUM_ATTRS; i++)
      {
        if(handle == tile_ble_env.service.characteristic_handles[i])
        {
          if (i == TILE_TOA_RSP_CCCD)
          {
              tile_toa_transport_ready(p_evt->evt.gatts_evt.params.write.data[0]); // initialite RSP
          }
          else if (i == TILE_TOA_CMD_CHAR)
          {
            // Tell Tile Lib about the write 
            tile_toa_command_received(p_evt->evt.gatts_evt.params.write.data,p_evt->evt.gatts_evt.params.write.len);
            break; // Break from the loop 
          }
          else
          {
            // Error Case
            TILE_ASSERT(0);
          }
        }
      }
      break;

    case BLE_GATTS_EVT_HVN_TX_COMPLETE:
        tile_toa_response_sent_ok();
        break;

    default:
      break;
  }
}


/*******************************************************************************
 * Local functions
 ******************************************************************************/


/**
 * @brief Timer handler for Tile timers
 */
static void tile_timer_timeout_handler(void *p_context)
{
  (void) tile_timer_expired((uint32_t)p_context & 0xFF);
}


/**
 * @brief Retrieve correct 16-bit UUID to advertise and interval on the Tile mode
 */
void tile_get_adv_params(uint16_t* uuid, uint16_t* interval)
{
  if(TILE_MODE_ACTIVATED == tile_checked->mode)
  {
    *uuid     = TILE_ACTIVATED_UUID;
    *interval = tile_checked->adv_int;
  }
  else
  {
    *uuid     = TILE_SHIPPING_UUID;
    *interval = TILE_DEFAULT_ADV_INT_SHIPPING;
  }
}

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
