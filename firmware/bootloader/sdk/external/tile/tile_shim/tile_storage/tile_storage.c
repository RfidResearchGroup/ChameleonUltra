/**
 * NOTICE
 * 
 * Copyright 2019 Tile Inc.  All Rights Reserved.
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
 * @file tile_storage.c
 * @brief Tile storage system
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)

#include "tile_storage.h"

#include "tile_lib.h"
#include "tile_config.h"
#include "modules/tile_tmd_module.h"

#include "crc16.h"
#include "nrf_fstorage.h"
#include "nrf_soc.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "app_error.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*******************************************************************************
 * Global variables
 ******************************************************************************/

struct tile_persist_tag tile_persist  __attribute__( (section("NoInit")) );

struct tile_checked_tag   * const tile_checked   = &tile_persist.checked.s;
struct tile_unchecked_tag * const tile_unchecked = &tile_persist.unchecked.s;

struct tile_env_tag tile_env;

/*******************************************************************************
 * Local variables
 ******************************************************************************/
 
static volatile bool write_in_progress   = false;
static volatile bool write_one_more_time = false;

const uint8_t interim_tile_id[8]    = INTERIM_TILE_ID;
const uint8_t interim_tile_key[16]  = INTERIM_AUTH_KEY;
/*******************************************************************************
 * Forward declarations
 ******************************************************************************/
uint16_t tile_get_adv_uuid(void);
static void tile_app_on_flash_evt(nrf_fstorage_evt_t * evt);
static int compare_versions(uint8_t v1, uint8_t v2);
static struct tile_persist_tag * active_app_data_bank(void);

/*******************************************************************************
 * Flash region configuration
 ******************************************************************************/
 
NRF_FSTORAGE_DEF(nrf_fstorage_t app_data_bank0) = {
  .start_addr   = APP_DATA_BANK0_ADDRESS,
  .end_addr     = (APP_DATA_BANK0_ADDRESS + APP_DATA_NUM_PAGES * PAGE_SIZE),
  .evt_handler  = tile_app_on_flash_evt,
};

NRF_FSTORAGE_DEF(nrf_fstorage_t app_data_bank1) = {
  .start_addr   = APP_DATA_BANK1_ADDRESS,
  .end_addr     = (APP_DATA_BANK1_ADDRESS + APP_DATA_NUM_PAGES * PAGE_SIZE),
  .evt_handler  = tile_app_on_flash_evt,
};

/*******************************************************************************
 * Global functions
 ******************************************************************************/

/**@brief   Sleep until an event is received. */
static void power_manage(void)
{
#ifdef SOFTDEVICE_PRESENT
    (void) sd_app_evt_wait();
#else
    __WFE();
#endif
}

void wait_for_flash_ready(nrf_fstorage_t const * p_fstorage)
{
  /* While fstorage is busy, sleep and wait for an event. */
  while (nrf_fstorage_is_busy(p_fstorage))
  {
    power_manage();
  }
}

#if 0
/**@brief   Helper function to obtain the last address on the last page of the on-chip flash that
 *          can be used to write user data.
 */
static uint32_t nrf5_flash_end_addr_get()
{
  uint32_t const bootloader_addr = NRF_UICR->NRFFW[0];
  uint32_t const page_sz         = NRF_FICR->CODEPAGESIZE;
  uint32_t const code_sz         = NRF_FICR->CODESIZE;

  return (bootloader_addr != 0xFFFFFFFF ?
          bootloader_addr : (code_sz * page_sz));
}
#endif

/**@brief   This function initializes two banks to be used in ping-pong manner in the Flash memory for usage by Tile Service.
 *          Purpose of two banks is to provide a back-up in case memory storage fails at some point in time
 *          This function checks for RAM and flash memory validity.
 *          a) If RAM data is valid, it stores the data in the newer bank in flash.
 *          b) If neither RAM nor flash is valid, it initializes the data to default values and stores in flash. 
 *             This should happen only at very first boot
 *          c) If RAM data is not valid, but flash is, it gets latest data from flash, copies it to RAM, and updates newer flash bank
 */
void tile_storage_init(void)
{
  ret_code_t ret;

  ret = nrf_fstorage_init(&app_data_bank0, &nrf_fstorage_sd, NULL);
  APP_ERROR_CHECK(ret);
  ret = nrf_fstorage_init(&app_data_bank1, &nrf_fstorage_sd, NULL);
  APP_ERROR_CHECK(ret);

  /* Check if RAM is still okay. Read from flash if not. */
  if(PERSIST_SIGNATURE == tile_persist.signature
    && tile_persist.crc == crc16_compute(tile_persist.checked.d, sizeof(tile_persist.checked.d), NULL))
  {
    /* RAM checks out. No need to load from flash. */
  }
  else
  {
    // Determine current tile_persist bank 
    struct tile_persist_tag *p = active_app_data_bank();

    if(NULL == p)
    {
      // Initialize to sane values 
      memset(&tile_persist, 0, sizeof(tile_persist));
      tile_checked->mode              = TILE_MODE_SHIPPING;
      tile_checked->tdt_configuration = DEFAULT_TDT_CONFIG;
      memcpy(tile_checked->model_number, tile_model_number, TILE_MODEL_NUMBER_LEN);
      memcpy(tile_checked->hardware_version, tile_hw_version, TILE_HARDWARE_VERSION_LEN);
    }
    else
    {
      memcpy(&tile_persist, p, sizeof(tile_persist));
    }
  }

  tile_unchecked->reset_count++;

  tile_store_app_data();
}

/**
 * @brief Save tile_persist to flash
 */
void tile_store_app_data(void)
{
  ret_code_t ret; 
  /* Compute CRC, to ensure most up-to-date version remains in RAM */
  tile_persist.crc = crc16_compute(tile_persist.checked.d, sizeof(tile_persist.checked.d), NULL);
  
  if(write_in_progress)
  {
    write_one_more_time = true;
    return;
  }

  write_in_progress   = true;
  write_one_more_time = false;
  /* Update bank and ID */
  tile_checked->bank = !tile_checked->bank;
  tile_checked->id++;
  tile_checked->version = CHECKED_STRUCTURE_VERSION;
  tile_persist.signature = PERSIST_SIGNATURE;
  /* Recompute CRC, to account for bank switch */
  tile_persist.crc = crc16_compute(tile_persist.checked.d, sizeof(tile_persist.checked.d), NULL);

  /* Save */
  if(0 == tile_checked->bank)
  {
    ret = nrf_fstorage_erase(&app_data_bank0, app_data_bank0.start_addr, APP_DATA_NUM_PAGES, NULL);
    APP_ERROR_CHECK(ret);
    ret = nrf_fstorage_write(&app_data_bank0, app_data_bank0.start_addr, &tile_persist, sizeof(tile_persist), NULL);
    APP_ERROR_CHECK(ret);

    //wait_for_flash_ready(&app_data_bank1);
  }
  else
  {
    ret = nrf_fstorage_erase(&app_data_bank1, app_data_bank1.start_addr, APP_DATA_NUM_PAGES, NULL);
    APP_ERROR_CHECK(ret);
    ret = nrf_fstorage_write(&app_data_bank1, app_data_bank1.start_addr, &tile_persist, sizeof(tile_persist), NULL);
    APP_ERROR_CHECK(ret);

    //wait_for_flash_ready(&app_data_bank1);
  }

}



/*******************************************************************************
 * Fstorage callbacks
 ******************************************************************************/

/**
 * @brief Callback for flash activity not initiated by Tile Lib.
 */
static void tile_app_on_flash_evt(nrf_fstorage_evt_t * evt)
{ 
  if (evt->result != NRF_SUCCESS)
  {
    NRF_LOG_INFO("--> Event received: ERROR while executing an fstorage operation.");
    return;
  }
  if(NRF_FSTORAGE_EVT_WRITE_RESULT == evt->id)
  {
    NRF_LOG_DEBUG("Fstorage Write Event Callback\n");

    write_in_progress = false;
    if(write_one_more_time)
    {
      tile_store_app_data();
    }
  }
  else if (NRF_FSTORAGE_EVT_ERASE_RESULT == evt->id)
  {
    NRF_LOG_DEBUG("Fstorage Erase Event Callback\n");
  }
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
/**
 * @brief Compare 1-byte cyclic version counters
 *
 * We will define v1 < v2 if the difference (v2 - v1) mod 0x100
 * is less than 0x80 (this is equivalent to having v2 - v1 come out
 * positive in signed, 8-bit, 2's-complement arithmetic).
 *
 * @return 1 if v1 > v2, 0 if v1 = v2, and -1 if v1 < v2
 */
static int compare_versions(uint8_t v1, uint8_t v2)
{
  /*
   * This returns (v1 > v2) - (v1 < v2), i.e.
   * 1 if v1 > v2, 0 if v1 = v2, and -1 if v1 < v2 
   */
  return (((v2 - v1) & 0xFF) > 0x80) - (((v2 - v1) & 0xFF) < 0x80);
}

/**
 * @brief Decide which bank is active, based on the validity of the banks and their IDs
 *
 * @param[in] valid0    True if bank 0 is valid.
 * @param[in] valid1    True if bank 1 is valid.
 * @param[in] id0       ID of bank 0.
 * @param[in] id1       ID of bank 1.
 *
 * @return 0 if bank 0 is the active bank, 1 if bank 1 is the active bank, and
 *         -1 if neither bank is valid.
 */
static int active_bank(bool valid0, bool valid1, uint8_t id0, uint8_t id1)
{
  if(valid0 && valid1)
  {
    if(compare_versions(id0, id1) >= 0)
    {
      return 0;
    }
    else
    {
      return 1;
    }
  }
  else if(valid0)
  {
    return 0;
  }
  else if(valid1)
  {
    return 1;
  }
  else
  {
    return -1;
  }
}

/**
 * @brief Find the active tile_checked bank.
 *
 * @return A pointer to the active tile_checked structure in flash, or NULL if
 *         there is no active bank.
 */
static struct tile_persist_tag * active_app_data_bank(void)
{
  struct tile_persist_tag *p0 = (void*)APP_DATA_BANK0_ADDRESS;
  struct tile_persist_tag *p1 = (void*)APP_DATA_BANK1_ADDRESS;

  bool p0_valid = false;
  bool p1_valid = false;

  if(PERSIST_SIGNATURE == p0->signature
    && 0 == p0->checked.s.bank
    && p0->crc == crc16_compute(p0->checked.d, sizeof(p0->checked.d), NULL))
  {
    p0_valid = true;
  }

  if(PERSIST_SIGNATURE == p1->signature
    && 1 == p1->checked.s.bank
    && p1->crc == crc16_compute(p1->checked.d, sizeof(p1->checked.d), NULL))
  {
    p1_valid = true;
  }
  
  int bank = active_bank(p0_valid, p1_valid, p0->checked.s.id, p1->checked.s.id);

  if(bank < 0)
  {
    return NULL;
  }
  else if(0 == bank)
  {
    return p0;
  }
  else if(1 == bank)
  {
    return p1;
  }
  else
  {
    /* Assert! We don't expect any other value to be returned. */
    return NULL;
  }
}

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
