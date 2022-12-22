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

/** @file tile_timer_driver.h
 ** @brief Tile Timer Driver interface. Provides TileLib an interface to use timers.
 */

#ifndef TILE_TIMER_DRIVER_H_
#define TILE_TIMER_DRIVER_H_

#include <stdint.h>


/**
 * Number of Tile ticks in one second. All Tile timer durations are
 * specified in Tile ticks.
 */
#define TILE_TICKS_PER_SEC ((uint32_t)100)

/**
 * IDs to associate with each Tile timer.
 */
enum TILE_TIMER_IDS
{
  TILE_CONNECTION_TIMER,
  TILE_AUTHENTICATION_TIMER,
  TILE_TDT_DOUBLETAP_TIMER,
  TILE_TDT_HDC_TIMER,
  TILE_TCU_PARAM_UPDATE_TIMER,
  TILE_TKA_TIMER1,
  TILE_TKA_TIMER2,
  TILE_TKA_TIMER3,
  TILE_TEST_TIMER1,
  TILE_TEST_TIMER2,
  TILE_TEST_TIMER3,
  TILE_TEST_TIMER4,
  TILE_TEST_TIMER5,
  TILE_TEST_TIMER6,
  TILE_TEST_TIMER7,
  TILE_TEST_TIMER8,
  TILE_TILEID_COUNTER_TIMER,
  TILE_MAX_TIMERS /* < Number of timers used by Tile Lib. */
};

struct tile_timer_driver
{
  /**
   * Start a timer. duration is in 10ms increments.
   */
  int (*start)(uint8_t timer_id, uint32_t duration);

  /**
   * Cancel a timer.
   */
  int (*cancel)(uint8_t timer_id);
};


/**
 * Timer registration function.
 */
int tile_timer_register(struct tile_timer_driver *driver);


/**
 * Call when a Tile timer has expired.
 */
int tile_timer_expired(uint8_t timer_id);


#endif // TILE_TIMER_DRIVER_H_
