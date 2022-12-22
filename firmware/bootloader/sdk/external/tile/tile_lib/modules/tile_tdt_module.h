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

/** @file tile_tdt_module.h
 ** @brief Tile Double Tap module
 */

#ifndef TILE_TDT_MODULE_H_
#define TILE_TDT_MODULE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TDT_HDC_IBEACON_DURATION                200 ///< in tens of miliseconds
#define TDT_HDC_ADVERTISING_STEP_DURATION       200 ///< Advertising step in tens of miliseconds
#define TDT_HDC_ADVERTISING_LAST_STEP_DURATION  100 ///< Advertising last step in tens of miliseconds

#define TDT_HDC_IBEACON_INTERVAL                40  ///< in 0.625 milisecond increments
#define TDT_HDC_ADVERTISING_INTERVAL            160 ///< in 0.625 milisecond increments


/**
 * @brief TDT Local Config Struct
 */
typedef struct
{
  uint16_t  SE_LTF:1;           ///< [0] Song Enable: LongTap Failure
  uint16_t  SE_LTS:1;           ///< [1] Song Enable: LongTap Success
  uint16_t  SE_DTF:1;           ///< [2] Song Enable: DoubleTap Failure
  uint16_t  SE_DTS:1;           ///< [3] Song Enable: DoubleTap Success
  uint16_t  SE_STIF:1;          ///< [4] Song Enable: SingleTapImmediate Failure
  uint16_t  SE_STIS:1;          ///< [5] Song Enable: SingleTapImmediate Success
  uint16_t  SE_STDF:1;          ///< [6] Song Enable: SingleTapDelayed Failure
  uint16_t  SE_STDS:1;          ///< [7] Song Enable: SingleTapDelayed Success
  uint16_t  EN_DT:1;            ///< [8] Enable: DoubleTap
  uint16_t  EN_LT:1;            ///< [9] Enable: LongTap
  uint16_t  EN_STI:1;           ///< [10] Enable: SingleTapImmediate
  uint16_t  EN_STD:1;           ///< [11] Enable: SingleTapDelayed
  uint16_t  SS_Strength:2;      ///< [12:13] Success Song Strength (0/1: Low; 2: Med; 3: High)
  uint16_t  FS_Strength:2;      ///< [14:15] Fail Song Strength (0/1: Low; 2: Med; 3: High)
  uint8_t   Delay;              ///< DoubleTap and LongTap detection delay: in units of 20 ms, plus an offset of 10ms.
  uint8_t   NotifDebounceDelay; ///< DoubleTap Notification Debouncing Delay: in units of 100ms. 0 means no debouncing.

} tdt_config_t;


/**
 * Tile DoubleTap module.
 *
 * This module is used by Tile Lib to detect various types of button press.
 * Furthermore, this module also supports the "TDT HDC" feature, which is used
 * to advertise with a high duty cycle when a double tap is detected.
 */
struct tile_tdt_module
{
  /**
   * Configuration for TDT. Used internally by Tile Lib.
   */
  tdt_config_t config;

  /**
   * Variables to support optional TDT HDC feature.
   */
  uint8_t hdc_status;

  /*** Diagnostic information ***/
  uint16_t  *single_tap;
  uint8_t   *long_tap;
  uint16_t  *double_tap_detect;
  uint16_t  *double_tap_notify;
  uint16_t  *double_tap_failure2;

  /**
   * Configuration was written by the app. Should be stored to NVM.
   */
  int (*config_written)(tdt_config_t *config);

  /**
   * Called when a double tap is detected and the Tile should move into
   * high duty cycle advertising.
   */
  void (*hdc_cb)(void);
};

/**
 * TDT_HDC_STATUS.
 *
 * Used by the TDT HDC optional feature (see @ref tile_tdt_module).
 */
enum TDT_HDC_STATUS
{
  TDT_HDC_STATUS_NORMAL     = 0x00, // Default state, Nothing special
  TDT_HDC_STATUS_IBEACON    = 0x01, // Advertise iBeacon
  TDT_HDC_STATUS_FAST_ADV   = 0x02, // Advertise fast
  TDT_HDC_STATUS_FAST_ADV2  = 0x03, // Advertise fast
  TDT_HDC_STATUS_FAST_ADV3  = 0x04, // Advertise fast
  TDT_HDC_STATUS_FAST_ADV4  = 0x05, // Advertise fast
  TDT_HDC_STATUS_FAST_ADV5  = 0x06, // Advertise fast
  TDT_HDC_STATUS_NOTIFY     = 0x07, // Send a TDT notification
};


/**
 * Register TDT module
 */
int tile_tdt_register(struct tile_tdt_module *module);

#ifdef __cplusplus
}
#endif

#endif // TILE_TDT_MODULE_H_
