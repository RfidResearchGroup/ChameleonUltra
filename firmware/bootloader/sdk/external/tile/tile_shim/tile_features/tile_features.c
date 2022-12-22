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
 * @file tile_features.c
 * @brief Support for features in Tile Lib
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)
#include "nrf_drv_rng.h"
#include "ble.h"
#include "ble_hci.h"
#include "nrf_delay.h"
#include "app_timer.h"
#include "app_scheduler.h"
#include "app_button.h"
#include <string.h>

#include "nrf_log.h"
#include "nrf_log_ctrl.h"

// TileLib includes
#include "tile_config.h"
#include "tile_features.h"
#include "tile_lib.h"
#include "drivers/tile_gap_driver.h"
#include "drivers/tile_timer_driver.h"
#include "drivers/tile_button_driver.h"
#include "drivers/tile_random_driver.h"
#include "modules/tile_tdi_module.h"
#include "modules/tile_toa_module.h"
#include "modules/tile_tmd_module.h"
#include "modules/tile_tpi_module.h"
#include "modules/tile_tdt_module.h"
#include "modules/tile_tdg_module.h"
#include "tile_storage/tile_storage.h"
#include "tile_player/tile_player.h"
#include "tile_assert/tile_assert.h"
#include "tile_bdaddr/tile_bdaddr.h"

/*******************************************************************************
 * Global variables
 ******************************************************************************/
tile_ble_env_t tile_ble_env;
app_timer_id_t tile_timer_id[TILE_MAX_TIMERS];

const char tile_model_number[]      = TILE_MODEL_NUMBER;
const char tile_hw_version[]        = TILE_HARDWARE_VERSION;

/*******************************************************************************
 * Local variables
 ******************************************************************************/
static toa_channel_t tile_toa_channels[NUM_TOA_CHANNELS] __attribute__((section("retention_mem_area0"), zero_init));
static uint8_t toa_queue_buffer[TOA_QUEUE_BUFFER_SIZE];

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/
void advertising_update(void);

/* gap module*/
static int tile_disconnect(void);

/* timer module*/
static int tile_timer_start(uint8_t timer_id, uint32_t duration);
static int tile_timer_cancel(uint8_t timer_id);

/* random module*/
static int tile_random_bytes(uint8_t *dst, uint8_t len);

/* toa module*/
static int tile_send_toa_response(uint8_t *data, uint16_t len);
static int tile_associate(uint8_t* tile_id, uint8_t* tile_auth_key, uint8_t* authorization_type);

/* tmd module*/
static int tile_mode_set(uint8_t mode);
static int tile_mode_get(uint8_t *mode);

/* tdg module*/
static int tile_get_diagnostics_cb(void);

/* song module*/
// Refer tile_player.c

/* test module*/
static void test_process_reboot(uint8_t reboot_type);
static void test_process_storage(uint8_t test_type, uint8_t *payload, uint8_t payload_length);
static int test_process(uint8_t code, uint8_t *data, uint8_t datalen);

/* button driver */
int         tile_read_button_state(uint8_t *button_state);
static void tile_hdc_cb(void);
static int  tile_hdc_config_written(tdt_config_t *config);
/* TPI module */
static int      tile_tileID_counter_updated(void);

/*******************************************************************************
 * Defines & types
 ******************************************************************************/

/*******************************************************************************
 * Tile configuration structures
 ******************************************************************************/

/* gap register struct */
static struct tile_gap_driver gap_driver = 
{
  .gap_disconnect         = tile_disconnect,
  .auth_disconnect_count  = &tile_persist.unchecked.s.auth_disconnect_count,
};

/* timer driver struct */
struct tile_timer_driver timer_driver =
{
  .start  = tile_timer_start,
  .cancel = tile_timer_cancel,
};

/* random driver struct  */
static struct tile_random_driver random_driver =
{
  .random_bytes  = tile_random_bytes,
};

/* device information struct */
struct tile_tdi_module tdi_module =
{
  .tile_id                  = tile_persist.checked.s.tile_id,
  .model_number             = tile_persist.checked.s.model_number,
  .hardware_version         = tile_persist.checked.s.hardware_version,
  .bdaddr                   = bdaddr, // RAM Variable, Not stored in Flash
  .firmware_version         = TILE_FIRMWARE_VERSION,
};

/* tile over the air struct  */
struct tile_toa_module toa_module =
{
  .tile_id                  = tile_persist.checked.s.tile_id,
  .auth_key                 = tile_persist.checked.s.tile_auth_key,
  .channels                 = tile_toa_channels,
  .queue                    = toa_queue_buffer,
  .queue_size               = TOA_QUEUE_BUFFER_SIZE,
  .num_channels             = NUM_TOA_CHANNELS,
  .mic_failure_count        = &tile_persist.unchecked.s.micFailures,
  .auth_failure_count       = &tile_persist.unchecked.s.auth_fail_count,
  .channel_open_count       = &tile_persist.unchecked.s.toa_channel_open_count,
  .authenticate_count       = &tile_persist.unchecked.s.toa_authenticate_count,
  .tka_closed_channel_count = &tile_persist.unchecked.s.tka_closed_channel_count,
  .send_response            = tile_send_toa_response,
  .associate                = tile_associate
};

/* tile mode struct */
struct tile_tmd_module tmd_module =
{
  .get  = tile_mode_get,
  .set  = tile_mode_set,
};

/* tile mode struct */
static struct tile_tdg_module tdg_module = 
{
  .get_diagnostics = tile_get_diagnostics_cb,
};

/* tile song module struct */
static struct tile_song_module song_module = 
{
  .play = PlaySong,
  .stop = StopSong
};

/* tile test module struct */
static struct tile_test_module test_module =
{
  .process = test_process,
};

/* tile button driver struct */
static struct tile_button_driver button_driver =
{
  .read_state = tile_read_button_state,
};

struct tile_tpi_module tpi_module =
{
  .tileID_key               = tile_persist.checked.s.tileIDkey,
  .hashed_tileID            = tile_env.hashedTileID,
  .tileID_counter           = &tile_persist.unchecked.s.tileIDcounter,
  .tileID_counter_updated   = tile_tileID_counter_updated,
};

/* tile double tap struct */
struct tile_tdt_module tdt_module =
{
  .hdc_status           = TDT_HDC_STATUS_NORMAL,
  .single_tap           = NULL,
  .long_tap             = NULL,
  .double_tap_detect    = NULL,
  .double_tap_notify    = NULL,
  .double_tap_failure2  = NULL,

  .hdc_cb               = tile_hdc_cb,
  .config_written       = tile_hdc_config_written,
};
/*******************************************************************************
 * Functions
 ******************************************************************************/
void tile_features_init(void)
{
  /****************************************************************/
  /**** Minimum Features required for TileLib Interoperability ****/
  /****************************************************************/
  /* Initialize GAP driver */ 
  (void) tile_gap_register(&gap_driver);
  
  /* Initialize timer driver */
  (void) tile_timer_register(&timer_driver);

  /* Initialize random driver */ 
  (void) tile_random_register(&random_driver);

  /* Initialize device information module */
  /* Obtain default Bdaddr from device register at 0x100000A4*/
  get_default_macAddr();
  set_new_macAddr();
  (void) tile_tdi_register(&tdi_module);

  /* Initialize tile over the air module */
  (void) tile_toa_register(&toa_module);

  /* Initialize tile mode module */
  (void) tile_tmd_register(&tmd_module);

  /* Initialize button driver module */
  (void) tile_button_register(&button_driver);

  /* Initialize tile PrivateID module */
  (void) tile_tpi_register(&tpi_module);
        
  /* Initialize tile double tap module */
  memcpy(&tdt_module.config, &tile_checked->tdt_configuration, sizeof(tdt_config_t));

  (void) tile_tdt_register(&tdt_module);

  /****************************************************************/
  /**** Additional Features ****/
  /****************************************************************/
  /* Initialize tile diagnbostics module */
  (void) tile_tdg_register(&tdg_module);

  /* Initialize song module */
  (void) tile_song_register(&song_module);

  /* Initialize test module */
  (void) tile_test_register(&test_module);
}



/*******************************************************************************
 * Callback functions for Tile Lib
 ******************************************************************************/

/***************************** gap module *******************************/
/**
 * @brief Disconnect current connection
 */
static int tile_disconnect(void)
{
  if(BLE_CONN_HANDLE_INVALID == tile_ble_env.conn_handle)
  {
    return TILE_ERROR_ILLEGAL_OPERATION;
  }

  (void) sd_ble_gap_disconnect(tile_ble_env.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);

  return TILE_ERROR_SUCCESS;
}

/**
 * @brief Update the TileID Char
 */
void tile_update_tileID_char(void)
{
  ret_code_t err_code;
  ble_gatts_value_t gatts_value;

  // Initialize value struct.
  memset(&gatts_value, 0, sizeof(gatts_value));

  gatts_value.len     = TILE_ID_LEN;
  gatts_value.offset  = 0;
  gatts_value.p_value = tile_persist.checked.s.tile_id;

  // Update database
  err_code = sd_ble_gatts_value_set(tile_ble_env.conn_handle, tile_ble_env.service.characteristic_handles[TILE_ID_CHAR], &gatts_value);
  APP_ERROR_CHECK(err_code);

  return;
}


/************************************************************************/

/***************************** timer module *****************************/
/**
 * @brief Start a Tile timer
 *
 * @param[in] timer_id   ID for timer, as specified by Tile Lib
 * @param[in] duration   Duration (in 10ms increments) for the timer
 */
static int tile_timer_start(uint8_t timer_id, uint32_t duration)
{
  ret_code_t err_code;
  if(duration < 1)
  {
    duration++;
  }

  /* The new timer takes priority, so stop any existing timer */
  err_code = app_timer_stop(tile_timer_id[timer_id]);
  APP_ERROR_CHECK(err_code);
  
  err_code = app_timer_start(tile_timer_id[timer_id], APP_TIMER_TICKS((uint64_t) duration * 10), (void *)(uint32_t)timer_id);
  APP_ERROR_CHECK(err_code);

  return TILE_ERROR_SUCCESS;
}

/**
 * @brief Cancel a Tile timer
 *
 * @param[in] timer_id    ID for timer, as specified by Tile Lib
 */
static int tile_timer_cancel(uint8_t timer_id)
{
  uint32_t err_code = app_timer_stop(tile_timer_id[timer_id]);

  err_code = (err_code == NRF_SUCCESS) ? TILE_ERROR_SUCCESS : err_code;

  return (int) err_code;
}
/************************************************************************/

/****************************random module *******************************/
/**
 * @brief Generate some random bytes
 *
 * @param[out] dst    Destination address for the random bytes
 * @param[in]  len    Number of bytes requested
 */
static int tile_random_bytes(uint8_t *dst, uint8_t len)
{
  uint8_t  num;
  uint32_t err_code;

  /* Check if enough random bytes are available */
  nrf_drv_rng_bytes_available(&num);
  while(num < len)
  {
    /* Wait for enough random bytes to be available */
    nrf_delay_us(200);
    nrf_drv_rng_bytes_available(&num);
  }

  /* Copy over random bytes */
  err_code = nrf_drv_rng_rand(dst, len);

  err_code = (err_code == NRF_SUCCESS) ? TILE_ERROR_SUCCESS : err_code;

  return (int) err_code;
}

/************************************************************************/

/***************************** tpi module *******************************/

/**
 * @brief Update TileID counter for TPI generation
 * 
 * @param[out] int       TILE_ERROR_TYPE
 */

static int tile_tileID_counter_updated(void)
{	 
  if((BLE_CONN_HANDLE_INVALID == tile_ble_env.conn_handle))
  {
    advertising_update();
  }
  return TILE_ERROR_SUCCESS;
}

/************************************************************************/

/***************************** toa module *******************************/

/**
 * @brief Send notification on a characteristic in TOA_RSP
 * 
 * @param[in] data       Data to set attribute to.
 * @param[in] len        Length of data.
 */
static int tile_send_toa_response(uint8_t *data, uint16_t len)
{
  ret_code_t err_code;

  if(BLE_CONN_HANDLE_INVALID == tile_ble_env.conn_handle)
  {
    return TILE_ERROR_ILLEGAL_OPERATION;
  }
  uint16_t handle = tile_ble_env.service.characteristic_handles[TILE_TOA_RSP_CHAR];

  ble_gatts_hvx_params_t hvx_params =
  {
    .handle   = handle,
    .type     = BLE_GATT_HVX_NOTIFICATION,
    .offset   = 0,
    .p_len    = &len,
    .p_data   = data
  };

  err_code = sd_ble_gatts_hvx(tile_ble_env.conn_handle, &hvx_params);
  APP_ERROR_CHECK(err_code);
  return TILE_ERROR_SUCCESS;
};

/**
 * @brief Set the new Tile Id/Auth Key during Commissioning Process if in Shipping Modes
 */
static int tile_associate(uint8_t* tile_id, uint8_t* tile_auth_key, uint8_t* authorization_type)
{
  int retcode = TOA_ERROR_OK;
#ifdef INTERIM_TILE_ID
  if(TILE_MODE_SHIPPING == tile_checked->mode)
  {
    memcpy(tile_checked->tile_id, tile_id,  sizeof(tile_checked->tile_id));
    memcpy(tile_checked->tile_auth_key, tile_auth_key, sizeof(tile_checked->tile_auth_key));
    // Update the TileID Char
    tile_update_tileID_char();
    NRF_LOG_INFO("tile_associate in Shipping Mode Successful\r\n");    
  }
  else // Do not allow to set new Tile id/Auth key for an already Activated Tile. We should never come here ideally
  {
    retcode = TOA_RSP_SERVICE_UNAVAILABLE;
    NRF_LOG_INFO("tile_associate in Activated Mode Not Allowed, returning TOA_RSP_SERVICE_UNAVAILABLE \r\n");
  }
  return retcode;
#else
  return retcode;
#endif
}
/************************************************************************/

/***************************** mode module *******************************/
/**
 * @brief Set the mode of the device.
 *
 * @param[in] mode  Mode, as specified by the TMD module.
 */
static int tile_mode_set(uint8_t mode)
{
  if(TILE_MODE_ACTIVATED != mode)
  {
    /* Disregard any mode besides Shipping and Activated
     * If mode being set is not Activated, Make it Shipping
     */
    mode = TILE_MODE_SHIPPING;
    /* When the Tile is not activated, the Interim TileID, Key is used. */
    memcpy(tile_checked->tile_id, interim_tile_id, 8);
    memcpy(tile_checked->tile_auth_key, interim_tile_key, 16);
    // Update the TileID Char
    tile_update_tileID_char();
  }
  tile_checked->mode = mode;
  set_new_macAddr();
//  tile_set_params_for_mode();
  tile_store_app_data();
  return TILE_ERROR_SUCCESS;
}

/**
 * @brief Get the current mode of the device.
 *
 * @param[out] mode  Mode, as specified by the TMD module.
 */
static int tile_mode_get(uint8_t *mode)
{
  *mode = tile_checked->mode;

  return TILE_ERROR_SUCCESS;
}

/************************************************************************/

/***************************** tdg module *******************************/
static int tile_get_diagnostics_cb(void)
{
  uint8_t version = DIAGNOSTIC_VERSION;

  (void) tdg_add_data(&version, 1);
  (void) tdg_add_data(&tile_checked->mode, 1);
  (void) tdg_add_data(&tile_unchecked->reset_count, 1);
  (void) tdg_add_data(&tile_unchecked->piezoMs, 4);
  (void) tdg_add_data(&tile_unchecked->connection_count, 3);
  (void) tdg_add_data(&tile_unchecked->auth_fail_count, 1);
  (void) tdg_add_data(&tile_unchecked->micFailures, 1);
  (void) tdg_add_data(&tile_unchecked->disconnect_count, 3);
  (void) tdg_add_data(&tile_unchecked->toa_channel_open_count, 3);
  (void) tdg_add_data(&tile_unchecked->toa_authenticate_count, 3);
  (void) tdg_add_data(&tile_unchecked->tka_closed_channel_count, 2);
  (void) tdg_add_data(&tile_unchecked->auth_disconnect_count, 2);

  (void) tdg_finish();

  return TILE_ERROR_SUCCESS;
}
/************************************************************************/

/***************************** song module ******************************/
// Refer tile_player.c
/************************************************************************/

void tile_button_was_pressed(void)
{
  /* Abort SONG if Find Song is currently playing*/
  if (CheckFindSong())
  {
    (void) StopSong();
  }

  if(TILE_MODE_ACTIVATED == tile_checked->mode)
  {
    // Forward to TileLib
    (void) tile_button_pressed();
  }
  else
  {
    /* Play the right Song according to Mode */
    if(TILE_MODE_SHIPPING == tile_checked->mode)
    {
      (void) PlaySong(TILE_SONG_WAKEUP_PART, 3, TILE_SONG_DURATION_ONCE);
    }
  }
}

int tile_read_button_state(uint8_t *button_state)
{
  bool is_button_pushed = app_button_is_pushed(0);
  
  
  /* pin is pulled high */
  if(true == is_button_pushed)
  {
    *button_state = TILE_BUTTON_PRESSED;
  }
  else
  {
    *button_state = TILE_BUTTON_RELEASED;
  }
  
  return TILE_ERROR_SUCCESS;
};


void tile_hdc_cb(void)
{
  /* Currently Disconnected, Update Advertising Data and Parameters based on the TDT State */
  if(BLE_CONN_HANDLE_INVALID == tile_ble_env.conn_handle)
  {
    advertising_update();
  }
};


int tile_hdc_config_written(tdt_config_t *config)
{  
  return TILE_ERROR_SUCCESS;
};


/***************************** test module ******************************/

static int test_process(uint8_t code, uint8_t *data, uint8_t datalen)
{
  switch(code) {
    case TEST_CMD_REBOOT:
      test_process_reboot(data[0]);
      break;
    case TEST_CMD_STORAGE:
      test_process_storage(data[0], data+1, datalen-1);
      break;
    default:
      break;
  }

  return TILE_ERROR_SUCCESS;
}

static void test_process_reboot(uint8_t reboot_type)
{
  switch(reboot_type) {
    case TEST_CMD_REBOOT_RESET:
      (void) sd_nvic_SystemReset();
      break;
    case TEST_CMD_REBOOT_WATCHDOG:
      while(1);
      //break;
    case TEST_CMD_REBOOT_MEMORY_FAULT:
      *((uint8_t*)0xFFFFFFFF) = 0;
      break;
    case TEST_CMD_REBOOT_OTHER:
      /* ? */
      break;
    case TEST_CMD_REBOOT_ASSERT:
      TILE_ASSERT(0);
      /* TODO */
      break;
    case TEST_CMD_REBOOT_DURING_FLASH:
      /* TODO */
      break;
  }
}

static void test_process_storage(uint8_t test_type, uint8_t *payload, uint8_t payload_length)
{
  /* TODO */
}
/************************************************************************/

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
