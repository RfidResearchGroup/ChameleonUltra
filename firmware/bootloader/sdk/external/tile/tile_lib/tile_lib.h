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

#ifndef TILE_LIB_H_
#define TILE_LIB_H_

/** @defgroup tile_lib Tile Library API
 *  @{
 *  @ingroup TOA
 *  @brief Tile Library Api
 */

/**
 * @brief Tile Service UUIDs.
 * These are 16-bit UUIDs.
 */
#define TILE_SHIPPING_UUID    0xFEEC               /** Advertised by Tiles in Shipping Mode. */
#define TILE_ACTIVATED_UUID   0xFEED               /** Advertised by Tiles in Activated Mode. */
#define TILE_SERVICE_UUID     TILE_ACTIVATED_UUID  /** Used to declare Tile Gatt Service. */

#define TILE_SVC_BASE_UUID    { 0xC0, 0x91, 0xC4, 0x8D, 0xBD, 0xE7, 0x60, 0xBA, 0xDD, 0xF4, 0xD6, 0x35, 0x00, 0x00, 0x41, 0x9D }
#define TILE_TOA_CMD_UUID     { 0xC0, 0x91, 0xC4, 0x8D, 0xBD, 0xE7, 0x60, 0xBA, 0xDD, 0xF4, 0xD6, 0x35, 0x18, 0x00, 0x41, 0x9D }
#define TILE_TOA_RSP_UUID     { 0xC0, 0x91, 0xC4, 0x8D, 0xBD, 0xE7, 0x60, 0xBA, 0xDD, 0xF4, 0xD6, 0x35, 0x19, 0x00, 0x41, 0x9D }
#define TILE_TILEID_CHAR_UUID { 0xC0, 0x91, 0xC4, 0x8D, 0xBD, 0xE7, 0x60, 0xBA, 0xDD, 0xF4, 0xD6, 0x35, 0x07, 0x00, 0x41, 0x9D }

#define TILE_DEFAULT_ADV_INT_ACTIVATED    3200 // In 0.625 ms Units
#define TILE_DEFAULT_ADV_INT_SHIPPING     160  // In 0.625 ms Units

/**
 * TOA Command and Response characteristics lengths in octets.
 */
#define TILE_TOA_CMD_CHAR_LEN 20
#define TILE_TOA_RSP_CHAR_LEN 20

/**
 * Attribute ID's associated with each Tile attribute.
 */
enum TILE_CHARACTERISTICS
{
  TILE_TOA_CMD_CHAR,
  TILE_TOA_RSP_CHAR,
  TILE_TOA_RSP_CCCD,
  TILE_ID_CHAR,
  TILE_NUM_ATTRS
};

/**
 * Length, in bytes, of the Tile ID.
 */
#define TILE_ID_LEN                 8

/**
 * Length, in bytes, of the hashed_tileID.
 */
#define TILE_HASHED_TILEID_LEN      8

/**
 * Length, in bytes, of the Tile authentication key.
 */
#define TILE_AUTH_KEY_LEN           16

/**
 * Length, in bytes, of the Tile identity key.
 */
#define TILEID_KEY_LEN              16

/**
 * Length of the Tile firmware version string.
 */
#define TILE_FIRMWARE_VERSION_LEN   10

/**
 * Length of the Tile model number string.
 */
#define TILE_MODEL_NUMBER_LEN       10

/**
 * Length of the Tile hardware version string.
 */
#define TILE_HARDWARE_VERSION_LEN   5

/**
 * Length of the Tile BDADDR.
 */
#define TILE_BDADDR_LEN             6

#define TILE_SERVICE_DATA_MAX_LENGTH  10

/**
 * @brief Error codes returned by Tile Lib functions
 */
enum TILE_ERROR_CODES
{
  TILE_ERROR_SUCCESS = 0,
  TILE_ERROR_NOT_INITIALIZED,
  TILE_ERROR_ILLEGAL_SERVICE,
  TILE_ERROR_ILLEGAL_PARAM,
  TILE_ERROR_ILLEGAL_OPERATION,
  TILE_ERROR_BUFFER_TOO_SMALL,
  TILE_ERROR_TERMINAL,
  TILE_ERROR_REENTRANCY,
  TILE_ERROR_NUM_TOA_CHANNELS,
};

/**@}*/

#endif
