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
 * @file tile_assert.h
 ** @brief Define a standard Tile assert interface
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)
#include "tile_assert.h"
#include "app_util_platform.h"
#include "nrf_log.h"
#include "nrf_nvic.h"

/* @brief    Tile Assert Interface
 * @details  Reset the MCU
 */
void tile_assert(bool cond, uint32_t line, const char file[], const char func[], bool ignore)
{
  if(!cond)
  {
    NRF_LOG_WARNING("System reset");
    NRF_BREAKPOINT_COND;
    (void) sd_nvic_SystemReset();
  }
}

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
