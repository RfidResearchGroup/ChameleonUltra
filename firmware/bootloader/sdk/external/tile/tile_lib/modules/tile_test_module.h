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

/** @file tile_test_module.h
 ** @brief Receive TEST commands over TOA
 */

#ifndef TILE_TEST_MODULE_H_
#define TILE_TEST_MODULE_H_

#include <stdint.h>


/**
 * @brief All TEST commands/responses through this module will need a code
 *        greater than @ref TILE_TEST_MODULE_CODE_BASE.
 */
#define TILE_TEST_MODULE_CODE_BASE   0x80

struct tile_test_module
{
  /**
   * @brief Receive a TEST message
   */
  int (*process)(uint8_t code, uint8_t *message, uint8_t length);
};


int tile_test_register(struct tile_test_module *module);


int tile_test_response(uint8_t code, uint8_t *response, uint8_t length);

#endif
