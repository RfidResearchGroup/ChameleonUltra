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

/** @file tile_tdg_module.h
 ** @brief Tile GATT Server Driver interface
 */

#ifndef TILE_TDG_MODULE_H_
#define TILE_TDG_MODULE_H_

#include <stdint.h>

#include "modules/tile_toa_module.h"

/**
 * @defgroup tile_tdg Tile Diagnostics module
 * @{
 * @ingroup TOA
 *
 * @brief Tile Diagnostics module.
 *
 * @details This module is used by Tile Lib to send diagnostic information to the Tile
 * data collection system. Consult with Tile for the proper format for
 * diagnostic data, if it is to be automatically parsed by the Tile backend.
 */
struct tile_tdg_module {
  /**
   * Retrieve diagnostic information.
   *
   * This function should call @ref tdg_add_data for each diagnostic data
   * field to be added, and then @ref tdg_finish when all data has been added.
   */
  int (*get_diagnostics)(void);

  uint8_t buffer[TOA_MPS];
  uint8_t buffer_pos;
};


/**
 * Register the TDG module.
 */
int tile_tdg_register(struct tile_tdg_module *module);

/**
 * @brief Add diagnostic data.
 * @details Should be called during the call to get_diagnostics.
 * This function can be called multiple times, for each piece of diagnostic
 * info that is to be added.
 *
 * @param[in] data   Data to add to diagnostics.
 * @param[in] length Length of data to add.
 */
int tdg_add_data(void *data, uint8_t length);

/**
 * @brief Finish adding diagnostic data.
 * @details Should be called during the call to
 * get_diagnostics, after all data has been added.
 */
int tdg_finish(void);

/**@}*/

#endif
