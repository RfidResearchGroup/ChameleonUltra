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
 * @file tile_bdaddr.c
 ** @brief
 **        Use part of TileId, modified to make it Public Random Mac Addr compatible, is used as BdAddr
 **        FYI on BLE Mac Address Types : https://devzone.nordicsemi.com/f/nordic-q-a/27012/how-to-distinguish-between-random-and-public-gap-addresses
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
 #include "sdk_common.h"
 #if NRF_MODULE_ENABLED(TILE_SUPPORT)
 
#include "tile_bdaddr.h"
#include "tile_assert/tile_assert.h"
#include "tile_storage/tile_storage.h"
#include "tile_config.h"
#include "tile_tmd_module.h"
#include "tile_toa_module.h"

#include "nrf_log.h"
#include "ble_gap.h"
/*******************************************************************************
 * Macro Definitions
 ******************************************************************************/

/*******************************************************************************
 * Global variables
 ******************************************************************************/
/* @brief Should contain currently used MacAddr value
 *        Used to assign to member of tdi_module
 */
uint8_t bdaddr[BLE_GAP_ADDR_LEN];

/* @brief Used when switching from Act->Manu or Shipping->Manu before a reboot
 *        Contains default MacAddr value provided by Nordic FICR register
 *        No need to save in Flash
 */
uint8_t default_bdaddr[BLE_GAP_ADDR_LEN];

/***********************************************************************
 * Local functions
 ***********************************************************************/


/***********************************************************************
 * Global functions
 ***********************************************************************/
 
 /**
 * @brief Update destination bdaddr from source bdaddr array
 */
void update_default_bdaddr(uint8_t* dest_bdaddr, uint8_t* source_bdaddr)
{
 /* Select new Addr Value */
 for(uint8_t i=0; i<BLE_GAP_ADDR_LEN; i++)
 {
   dest_bdaddr[i] = source_bdaddr[BLE_GAP_ADDR_LEN-1-i];
 }
}

/**
 * @brief Update MacAddr value from first 6 bytes of TileId
 *        We need to use Nordic Set API to update the value used in advertising
 */
void set_tileid_macAddr(void)
{
  NRF_LOG_INFO("set_tileid_macAddr\n");
  
  /********** Update MacAddr used while advertising using Nordic API ************/

  ble_gap_addr_t addr;

  /* Select new Addr Type */
  addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;

  /* Select new Addr Value from Tile Id */
  update_default_bdaddr(addr.addr, tile_checked->tile_id); 
    
  /* Make it RANDOM STATIC to match addr_type, by setting first 2 bits high, else set function will return error */
  addr.addr[BLE_GAP_ADDR_LEN-1] |= 0xC0;
  /* Need to set the updated Mac Addr using API, so that ble_advdata_encode() can use the updated value
   * sd_ble_gap_addr_get() will now start returning updated value, till a power cycle
   * At boot, sd_ble_gap_addr_get() will return default value again
   */
  (void) sd_ble_gap_addr_set(&addr); 
  
  /************  Update Internal bdaddr value, used by tdi module *************/
  update_default_bdaddr(bdaddr, addr.addr); 

  /****************************************************************************/
}

/**
 * @brief Obtain default Mac Address from FICR register
 */
void get_default_macAddr(void)
{
  /* Obtain Default MacAddr from FICR register */
  ble_gap_addr_t addr;
    
  (void) sd_ble_gap_addr_get(&addr);
  
  /* Store value in global variable */
  update_default_bdaddr(default_bdaddr, addr.addr); 

  /* Update Internal bdaddr value, used by tdi module */
  for(uint8_t i=0; i<BLE_GAP_ADDR_LEN; i++)
  {
    bdaddr[i]         = default_bdaddr[i];
  }
}

/**
 * @brief Set the Mac Address to be used internally by tdi module and to be used for Advertising
 *        Select this based on the MacAddress Mechanism configured for that Product, and based on the Tile mode
 */
void set_new_macAddr(void)
{
  set_tileid_macAddr();
}

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
