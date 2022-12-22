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

/** @file tile_tpi_module.h
 ** @brief Tile Private Identification API module
 */

#ifndef TILE_TPI_MODULE_H_
#define TILE_TPI_MODULE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tile TPI API module.
 *
 * This module is used by Tile Lib in order to implement PrivateID
 */
struct tile_tpi_module
{
  /**
   * tileID_counter, Maintained by TileLib and persistently stored by the Application at each update.
   */
  uint16_t* tileID_counter;
  /**
   * Tile Identity Key. Saved by the Application in persistent memory when the key is refreshed.
   * 16 bytes.
   */
  uint8_t*  tileID_key;

  /**
   * Hashed TileID, Created by TileLib and to be accessed and used by the Application to advertise.
   * 8 bytes.
   */
  uint8_t*  hashed_tileID;

  /**
   * The tileID_counter was incremented and its new value shall be saved into flash by the application.
   * Also, Advertising Data need to be updated with the new hashed_tileID.
   */
  int (*tileID_counter_updated)(void);
};


/**
 ****************************************************************************************
 * @brief Register TPI Module.
 *
 * @param[in] module    Pointer to the TPI Module struct.
 *
 ****************************************************************************************
 */
int tile_tpi_register(struct tile_tpi_module *module);

#ifdef __cplusplus
}
#endif

#endif  // TILE_TPI_MODULE_H_
