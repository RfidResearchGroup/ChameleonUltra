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

/** @file tile_tmd_module.h
 ** @brief Tile Mode module interface. Controls Tile Mode.
 */

#ifndef TILE_TMD_MODULE_H_
#define TILE_TMD_MODULE_H_

#include <stdint.h>

/** \defgroup TMD Tile mode
 *  \ingroup TOA
 *  @{
 */
/**
 * @brief TILE_MODE <br>
 */
enum TILE_MODE
{
  TILE_MODE_MANUFACTURING = 0x0,
  TILE_MODE_SHIPPING      = 0x1,
  TILE_MODE_ACTIVATED     = 0x2
};

/** @} */


/**
 * Tile Mode module.
 *
 * This module is used by Tile Lib to get and set the mode.
 */
struct tile_tmd_module
{
  /**
   * Get the current mode
   */
  int (*get)(uint8_t *mode);

  /**
   * Set the mode. Value should be saved in NVM.
   */
  int (*set)(uint8_t mode);
};


/**
 * Register the TMD module
 */
int tile_tmd_register(struct tile_tmd_module *module);


#endif
