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

/** @file tile_toa_module.h
 ** @brief Tile Over-the-air API module
 */

#ifndef TILE_TOA_MODULE_H_
#define TILE_TOA_MODULE_H_

#include <stdint.h>
#include <stdbool.h>


/**
 * @defgroup toa_module Tile Over-the-air API module
 * @{
 * @ingroup TOA
 */

/**
 * @brief TOA Max Payload Size.
 * This is the maximum Payload that can be carried by a TOA Command or Response.
 * It excludes the TOA_CMD/TOA_RSP Code and excludes the MIC.
 */
#define TOA_MPS  14


#define TILE_SESSION_KEY_LEN 16


/**
 * Session information for a TOA channel
 */
struct toa_channel_tag
{
  uint8_t session_key[TILE_SESSION_KEY_LEN];
  uint32_t nonceA;
  uint32_t nonceT;
  uint16_t state;
  uint16_t check_delay;
  uint16_t ack_delay;
};

typedef struct toa_channel_tag toa_channel_t;   //!< Structure containing session information for a TOA channel


/**
 * Tile Over-the-air API module.
 *
 * This module is used by Tile Lib in order to implement its over-the-air
 * protocol.
 */
struct tile_toa_module 
{
  /**
   * Tile ID -- 64-bit identifier for Tile Nodes.
    *           Example: {0x1a, 0x95, 0xd9, 0x97, 0xf0, 0xf2, 0x66, 0x07}.
   */
  uint8_t*  tile_id;

  /**
   * Auth Key -- 128-bit master key for Tile Nodes.
    *           Example: {0x14, 0x27, 0xe3, 0x03, 0xa2, 0x51, 0xc5, 0xb5, 0x07, 0x2a, 0xa9, 0x81, 0xa9, 0x42, 0x8a, 0x43}.
   */
  uint8_t*  auth_key;
  
  /**
   * Pointer to an array of @ref toa_channel_t structures. It is recommended
   * to use 4 channels, but if memory is a constraint then the number can be
   * decreased.
   */
  toa_channel_t*  channels;
  
  /**
   * Pointer to a buffer for queueing TOA messages.
   */
  uint8_t*  queue;

  /**
   * Size of buffer used for TOA queue. Recommended to be at least size
   * 100 for one channel, and add 40 for each additional channel.
   */
  uint16_t  queue_size;

  /**
   * Number of channels contained in the channels array.
   */
  uint8_t num_channels;

  /**
  * Diagnostic info: counts the mic failures
   */
  uint8_t*  mic_failure_count;
  
  /**
  * Diagnostic info: counts the authentication failures
   */
  uint8_t*  auth_failure_count;
  
  /**
  * Diagnostic info: counts the Number of successfull TOA Channel Open (with a successfull authentication)
   */
  uint32_t* channel_open_count;
  
  /**
  * Diagnostic info: counts the number of TOA Authenticate Commands received
   */
  uint32_t* authenticate_count;
  
  /**
  * Diagnostic info: counts the number of TOA channel close triggered by TKA
   */
  uint16_t* tka_closed_channel_count;

  /**
   * Send a TOA Response.
   * @param[in]  data:    Pointer to the TOA Response.
   * @param[in]  len:     Length of the TOA Response.
   */
  int (*send_response)(uint8_t *data, uint16_t len);

  /**
   * Optional callback called when an association is happenning (can be set to NULL).
   *  It is mostly needed for Commissioning Tiles using an Interim TileID, Key.
   *
   * @param[in]  tile_id: 8-byte unique tile identification code.
   * @param[in]  tile_auth_key: 16-byte authentication key.
   * @param[in]  authorization_type:  Pointer to authorization type.
   *
   * @param[out] authorization_type:  set to the right value if an authorization is required (ie 1 for Button Press).
   *
   * @return See @ref TILE_ERROR_CODES.
   */
  int (*associate)(uint8_t* tile_id, uint8_t* tile_auth_key, uint8_t* authorization_type);
};


/** \ingroup TOA
 * @brief TOA feature error codes. Any feature which uses these error
 * codes will return the error in a standard format. This format is:
 *
 * TOA Response | Error Response | Offending Command | Error Code | Additional Payload
 * -------------|----------------|-------------------|------------|-------------------
 * 1 Byte       | 1 Byte         | 1 Byte            | 1 Byte     | Varies. Up to TOA_MPS - 4 bytes.
 *
 * Example 1: Say a TOFU_CTL_CMD_RESUME command is sent at a bad time. Then, the Tile would
 * respond with
 * TOA_RSP_TOFU_CTL  | TOFU_CTL_RSP_ERROR | TOFU_CTL_CMD_RESUME | TOA_ERROR_INVALID_STATE
 * ------------------|----------------|---------------------|--------------------
 * 1 Byte            | 1 Byte         | 1 Byte              | 1 Byte
 *
 */
enum TOA_FEATURE_ERROR_CODES
{
  TOA_ERROR_OK              = 0x00,
  /**< This code is used when there's no error */
  TOA_ERROR_UNSUPPORTED     = 0x01,
  /**< This code is used when the given command is not supported */
  TOA_ERROR_PARAMETERS      = 0x02,
  /**< This code is used when the parameters to the command are invalid */
  TOA_ERROR_SECURITY        = 0x03,
  /**< This code is used when the app has insufficient security privileges
   * to execute the given command */
  TOA_ERROR_INVALID_STATE   = 0x04,
  /**< This code is used when the given command cannot be executed in
   * the current state of the Tile */
  TOA_ERROR_MEM_READ        = 0x05,
  /**< This code is used when a memory read fail */
  TOA_ERROR_MEM_WRITE       = 0x06,
  /**< This code is used when a memory write fails */
  TOA_ERROR_DATA_LENGTH     = 0x07,
  /**< This code is used when a received data block is not the expected size */
  TOA_ERROR_INVALID_SIZE    = 0x08,
  /**< This code is used when the app requests to write data of inappropriate size */
  TOA_ERROR_SIGNATURE       = 0x09,
  /**< This code is used when a signature check fails */
  TOA_ERROR_CRC             = 0x0A,
  /**< This code is used when a CRC check fails */
  TOA_ERROR_CRC2            = 0x0B,
  /**< This code is used when there are multiple CRC checks */
  TOA_ERROR_HASH            = 0x0C,
  /**< This code is used when a hash check fails */
  TOA_ERROR_PRODUCT_HEADER  = 0x0D,
  /**< This code is used when the product header is invalid. If this happens,
   * the Tile is in a very bad state. */
  TOA_ERROR_IMAGE_HEADER    = 0x0E,
  /**< This code is used when a received image header is invalid */
  TOA_ERROR_SAME_IMAGE      = 0x0F,
  /**< This code is used when the image to send matches the image already on the Tile */
  TOA_ERROR_INVALID_DATA    = 0x10,
  /**< This code is used when the data sent to the Tile is invalid */
  TOA_ERROR_MEM_ERASE       = 0x11,
  /**< This code is used when a memory erase fails */
  TOA_ERROR_RESOURCE_IN_USE = 0x12,
  /**< This code is used when there is an attempt to access a resource in use by someone else */
};

/** \ingroup TOA
 * @brief TOA Error Response Codes
 */
enum TOA_ERROR_CODES
{
  TOA_RSP_ERROR_SECURITY    = 0x01,
  /**< Error Code sent by TOA Server when required security level for the command is not met (like authentication)
  Format:
  @ref TOA_RSP_ERROR_SECURITY Code | The TOA_CMD that failed
  ---------------------------------|-----------------------------
  1 Byte                           |  1 Byte
  */

  TOA_RSP_ERROR_UNSUPPORTED = 0x02,
  /**< Error Code sent by TOA Server when an unsupported TOA Command is received
  Format:
  @ref TOA_RSP_ERROR_UNSUPPORTED Code | The TOA_CMD that failed
  ------------------------------------|-----------------------------
  1 Byte                              |  1 Byte
  */

  TOA_RSP_ERROR_PARAMETERS  = 0x03,
  /**< Error Code sent by TOA Server when a TOA Command with wrong parameters is received
  Format:
  
  @ref TOA_RSP_ERROR_PARAMETERS Code | The TOA_CMD that failed
  -----------------------------------|------------------------------
  1 Byte                             |  1 Byte
  */

  TOA_RSP_ERROR_DROPPED_RSP = 0x04,
  /**< Error Code sent by TOA Server when 1 or more Responses were dropped, most likely due to an overflow.<br>
  The Client should close the connection when this happens.
  Format:
  @ref TOA_RSP_ERROR_DROPPED_RSP Code  | The first TOA_RSP that was dropped
  -------------------------------------|----------------------------------------
  1 Byte                               |  1 Byte
  */

  TOA_RSP_ERROR_NO_CID_AVAILABLE = 0x05,
  /**< Error Code sent by a TOA Server when there are no CIDs available for allocation.

  Format:
  @ref TOA_RSP_ERROR_NO_CID_AVAILABLE | TOA_CMD_OPEN_CHANNEL
  ------------------------------------|--------------------------
  1 Byte                              | 1 Byte
  */
  
  TOA_RSP_ERROR_AUTHORIZATION = 0x06,
  /**< Error Code sent by a TOA Server when the required authorization level for the command is not met
  Format:
  @ref TOA_RSP_ERROR_AUTHORIZATION Code | The TOA_CMD that failed | The required Authorization Type
  --------------------------------------|------------------------------|--------------------------------
  1 Byte                                |  1 Byte                      | 1 Byte (value 1 for Button Press)
  */

  TOA_RSP_SERVICE_UNAVAILABLE = 0x07,
  /**< Error Code sent by a TOA Server when the required service is unavailable (i.e. user trigger)
  Format:
  @ref TOA_RSP_SERVICE_UNAVAILABLE Code | The TOA_CMD that failed
  --------------------------------------|-----------------------------
  1 Byte                                |  1 Byte
  */
};


/**
 * Register TOA module.
 */
int tile_toa_register(struct tile_toa_module *module);

/**
 ****************************************************************************************
 * @brief The underlying TOA transport is ready.
 *  This is the case when TOA_RSP channel was enabled for notifications or indications.
 *
 * @param[in] ready       1 for ready, 0 for not ready.
 *
 ****************************************************************************************
 */
void tile_toa_transport_ready(bool ready);


/**
 ****************************************************************************************
 * @brief A TOA response was successfully sent to the TOA Client (and an other one can be sent).
 *
 ****************************************************************************************
 */
void tile_toa_response_sent_ok(void);


/**
 ****************************************************************************************
 * @brief An TOA Commands was received.
 *
 * @param[in] data       pointer to data.
 * @param[in] datalen    number of bytes of data.
 *
 ****************************************************************************************
 */
void tile_toa_command_received(const uint8_t* data, uint8_t datalen);


/**
 ****************************************************************************************
 * @brief Send an Authorized Notification.
 *
 * @param[in] authorization_type    The type of authorization (ie Button press).
 * @param[in] authorization_time    The time for which the authorization is valid.
 *
 ****************************************************************************************
 */
int tile_toa_authorized(uint8_t authorization_type, uint16_t authorization_time);

/** @} */

#endif
