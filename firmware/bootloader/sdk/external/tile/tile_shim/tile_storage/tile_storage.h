/**
 * NOTICE
 * 
 * Copyright 2016 Tile Inc.  All Rights Reserved.
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
 * @file tile_storage.h
 * @brief Tile storage system
 */


#ifndef TILE_STORAGE_H_
#define TILE_STORAGE_H_


#include <stdint.h>

#include "tile_lib.h"
#include "nrf_fstorage_sd.h"
#include "tile_tdt_module.h"
#include "ble_gap.h"


/*****************************************/
/* Copied from nordic14, TO DO: find correct definitions for Nordic 15.2 */
#define PAGE_SIZE                     4096

/* These addresses should be the two pages directly before the default bootloader location */
#define APP_DATA_BANK0_ADDRESS        0x76000
#define APP_DATA_BANK1_ADDRESS        0x77000


#define APP_DATA_NUM_PAGES            1
/****************************************/

#define DEFAULT_ADVERTISING_INTERVAL  160


#define PERSIST_SIGNATURE             0xA5A5
#define CHECKED_SIZE                  128
#define UNCHECKED_SIZE                256

#define CHECKED_STRUCTURE_VERSION_1   1
#define CHECKED_STRUCTURE_VERSION_2   2
#define CHECKED_STRUCTURE_VERSION_3   3
#define CHECKED_STRUCTURE_VERSION_4   4
#define CHECKED_STRUCTURE_VERSION     CHECKED_STRUCTURE_VERSION_1

extern nrf_fstorage_t    app_data_bank0;
extern nrf_fstorage_t    app_data_bank1;

extern        uint8_t bdaddr[BLE_GAP_ADDR_LEN];
extern const  uint8_t interim_tile_id[];
extern const  uint8_t interim_tile_key[];
extern const  char tile_model_number[];
extern const  char tile_hw_version[];

struct tile_checked_tag 
{
  /**************************************************************************************************/
  /*** WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING   ****/
  /*** THIS STRUCTURE IS SAVED TO FLASH AND RETRIEVED AFTER TOFU                                 ****/
  /*** THIS MEANS STUFF SHOULD NOT BE MODIFIED BUT ONLY AT THE END TO MAINTAIN COMPATIBILITY     ****/
  /**************************************************************************************************/
  uint16_t        version;
  uint8_t         id;
  uint8_t         bank;
  uint8_t         mode;
  uint16_t        adv_int;
  tdt_config_t    tdt_configuration;
  uint8_t         tile_id[TILE_ID_LEN];
  uint8_t         tile_auth_key[TILE_AUTH_KEY_LEN];
  char            model_number[TILE_MODEL_NUMBER_LEN];
  char            hardware_version[TILE_HARDWARE_VERSION_LEN];
  uint8_t         bdaddr[TILE_BDADDR_LEN];
  uint8_t         tileIDkey[TILEID_KEY_LEN];
};

struct tile_unchecked_tag 
{
  /**************************************************************************************************/
  /*** WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING   ****/
  /*** THIS STRUCTURE IS SAVED TO FLASH AND RETRIEVED AFTER TOFU                                 ****/
  /*** THIS MEANS STUFF SHOULD NOT BE MODIFIED BUT ONLY AT THE END TO MAINTAIN COMPATIBILITY     ****/
  /**************************************************************************************************/

  // Activity tracking
  uint32_t  connection_count;           /**< number of connections */
  uint32_t  disconnect_count;           /**< Number of disconnections */
  uint8_t   auth_fail_count;            /**< authentication failures count */
  uint8_t   micFailures;                /**< mic failures */
  uint8_t   reset_count;                /**< Reset Count */
  uint32_t  piezoMs;                    /**< time for which piezo was active in '10 ms' units */

  // TOA Activity monitoring
  uint32_t  toa_channel_open_count;     /**< Number of successfull TOA Channel Open (with a successfull authentication) */
  uint32_t  toa_authenticate_count;     /**< number of TOA Authenticate Commands received */
  uint16_t  tka_closed_channel_count;   /**< number of TOA Channel close triggered by TKA */
  uint16_t  auth_disconnect_count;      /**< number of disconnections triggered by Auth Timer */
  
//Counter for private ID
  uint16_t  tileIDcounter;              /**< Counter used for PrivateID */
};

struct tile_persist_tag 
{
  uint16_t crc;
  uint16_t signature;
  union
  {
    struct tile_checked_tag s;
    uint8_t d[CHECKED_SIZE-4]; /* -4 for CRC + signature */
  } checked __attribute__ ((aligned (4)));
  union
  {
    struct tile_unchecked_tag s;
    uint8_t d[UNCHECKED_SIZE];
  } unchecked __attribute__ ((aligned (4)));
};

/**
 * @brief Persistent structure, which is saved to flash. Does not need to be
 *        accessed directly. Access elements with tile_checked and tile_unchecked.
 */
extern struct tile_persist_tag tile_persist;

/**
 * @brief CRC checked portion of persistent data.
 */
extern struct tile_checked_tag * const tile_checked;

/**
 * @brief Non-CRC portion of persistent data. This get reinitialized when
 *        the CRC of the checked portion fails.
 */
extern struct tile_unchecked_tag * const tile_unchecked;

/**
 * @brief Tile environment data. Lost at reboot.
 */
struct tile_env_tag 
{
  uint16_t  last_reset_reason;      ///> Contains the reason for the last reset
  uint8_t   authorized;
  uint8_t   hashedTileID[TILE_HASHED_TILEID_LEN];
};

extern struct tile_env_tag tile_env;

void tile_storage_init(void);
void tile_store_app_data(void);
#endif
