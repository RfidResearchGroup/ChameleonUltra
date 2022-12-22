/**
 * NOTICE
 * 
 * Copyright 2017 Tile Inc.  All Rights Reserved.
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

/** @file tile_button_driver.h
 ** @brief Tile Button driver
 */

#ifndef TILE_BUTTON_DRIVER_H_
#define TILE_BUTTON_DRIVER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button states
 */
enum TILE_BUTTON_STATES
{
  TILE_BUTTON_PRESSED,
  TILE_BUTTON_RELEASED,
};


/**
 * Tile button driver.
 */
struct tile_button_driver
{
  /**
   * Read the state of the Tile button.
   *
   * @param[out] button_state  State of the button. See TILE_BUTTON_STATES.
   *
   * @return See @ref TILE_ERROR_CODES.
   */
  int (*read_state)(uint8_t *button_state);
};


/**
 * Register the button module.
 *
 * @param[in] driver Driver for the Tile button.
 *
 * @return TILE_ERROR_SUCCESS.
 */
int tile_button_register(struct tile_button_driver *driver);


/**
 * Call when the Tile button has been pressed.
 *
 * @return TILE_ERROR_SUCCESS.
 */
int tile_button_pressed(void);

#ifdef __cplusplus
}
#endif

#endif // TILE_BUTTON_DRIVER_H_
