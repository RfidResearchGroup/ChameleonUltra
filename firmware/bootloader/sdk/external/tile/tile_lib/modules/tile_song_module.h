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

/** @file tile_song_module.h
 ** @addtogroup TOA
 ** @{
 ** @brief Tile Song Module
 */

#ifndef TILE_SONG_MODULE_H_
#define TILE_SONG_MODULE_H_

#include <stdint.h>
#include "crypto/hmac_sha256.h"

/**
 * @brief Tile Song numbers
 */
enum TILE_SONG
{
  TILE_SONG_1_CLICK      = 0x00,
  TILE_SONG_FIND         = 0x01,
  TILE_SONG_ACTIVE       = 0x02,
  TILE_SONG_SLEEP        = 0x03,
  TILE_SONG_WAKEUP       = 0x04,
  TILE_SONG_FACTORY_TEST = 0x05,
  TILE_SONG_MYSTERY      = 0x06,
  TILE_SONG_SILENT       = 0x07,
  TILE_SONG_BUTTON       = 0x08,
  TILE_SONG_WAKEUP_PART  = 0x09,
  TILE_SONG_DT_SUCCESS   = 0x0a,
  TILE_SONG_DT_FAILURE   = 0x0b,
  TILE_SONG_2_CLICK      = 0x0c,
  TILE_SONG_1_BIP        = 0x0d,
  TILE_SONG_2_BIP        = 0x0e,
  TILE_SONG_3_BIP        = 0x0f,
  TILE_SONG_4_BIP        = 0x10,
  TILE_SONG_5_BIP        = 0x11,
  TILE_SONG_6_BIP        = 0x12,
  TILE_SONG_7_BIP        = 0x13,
  TILE_SONG_DT_HB        = 0x14,
  TILE_SONG_MAX          = 0x15,

  TILE_SONG_STOP         = 0xFF
};


/**
 * Enumerate notes from C0 to B9
 *
 * Tile songs are created as a sequence of pairs,
 * (note, duration). Each value in the pair is one byte.
 * A song ends with the pair (REST, REST).
 */
enum NOTES {
  REST = 0x00,
  C0, CS0, D0, DS0, E0, F0, FS0, G0, GS0, A0, AS0, B0,
  C1, CS1, D1, DS1, E1, F1, FS1, G1, GS1, A1, AS1, B1,
  C2, CS2, D2, DS2, E2, F2, FS2, G2, GS2, A2, AS2, B2,
  C3, CS3, D3, DS3, E3, F3, FS3, G3, GS3, A3, AS3, B3,
  C4, CS4, D4, DS4, E4, F4, FS4, G4, GS4, A4, AS4, B4,
  C5, CS5, D5, DS5, E5, F5, FS5, G5, GS5, A5, AS5, B5,
  C6, CS6, D6, DS6, E6, F6, FS6, G6, GS6, A6, AS6, B6,
  C7, CS7, D7, DS7, E7, F7, FS7, G7, GS7, A7, AS7, B7,
  C8, CS8, D8, DS8, E8, F8, FS8, G8, GS8, A8, AS8, B8,
  C9, CS9, D9, DS9, E9, F9, FS9, G9, GS9, A9, AS9, B9,
};

/**
 *  @brief TILE_SONG_DURATION
 *          Duration to play the Tile Song for.
 *          The duration is in seconds and here are special values.
 */
enum TILE_SONG_DURATION
{
  TILE_SONG_DURATION_NOPLAY   = 0x00, /**< Do not play anything */
  TILE_SONG_DURATION_ONCE     = 0xFE, /**< Play the Song just once */
  TILE_SONG_DURATION_FOREVER  = 0xFF  /**< Play the song forever, till someone stops it */
};


#define SONG_METADATA_SIZE (sizeof(struct song_metadata_t))
#define SONG_INFO_SIZE (sizeof(struct song_hdr_info_t))
#define SONG_SECURITY_SIZE (sizeof(struct song_hdr_sec_t))
#define SONG_HEADER_SIZE (SONG_INFO_SIZE + SONG_SECURITY_SIZE)

#define SONG_HASH_SIZE 32 /**< Size of the song hash */
#define SONG_SIG_SIZE  64 /**< Size of the song signature */
#define SONG_CRC16_SIZE 2 /**< Size of the Block CRC */


#define TILE_PROGRAMMABLE_SONG_LENGTH 1024 /**< Maximum length of the programmable song section in flash */
#define TILE_SONG_BLOCK_SIZE  128          /**< Size of data in a data block */
#define TILE_SONG_BUFFER_SIZE TILE_SONG_BLOCK_SIZE + SONG_CRC16_SIZE /**< Size of intermediate buffer for programming  */
#define TILE_SONG_VERSION 1                /**< Version field, to allow future format changes to take place */
#define TILE_SONG_VALID   0xAA             /**< Flag indicating a song is valid */


/** @brief Song file header info portion */
struct song_hdr_info_t
{
  uint8_t  song_format;     ///< song_format is a Version Number that would describe the Tsong Format Variations
  uint8_t  song_number;     ///< song_number describes what the Type of Song being Programmed and what song_number to use for playing the Song using TOA_CMD_SONG command.
  uint16_t song_id;         ///< song_id is the Tile Assigned ID Number of this Song. See @ref TILE_SONG
  uint16_t song_size;       ///< song_size represents the Song Payload Size, excluding any Security or Info Header.
};

/** @brief Song file header security info */
struct song_hdr_sec_t
{
  uint8_t  hash[SONG_HASH_SIZE];  ///< A SHA-256 Hash Calculated using the Info Header and Song Payload.
  uint8_t  sign[SONG_SIG_SIZE];   ///< Signature of the Song, calculated using the hash as input and Song Private Key with ECC Secp256k1 Curve.
};

/** @brief State of song programming */
struct song_program_state_t
{
  uint16_t pos;                 ///< accumulated number of bytes written for current Song
  uint8_t  buf_pos;             ///< accumulated number of bytes received for current block
  uint8_t  state;               ///< Song Programming State
  uint16_t file_size;           ///< Total File Size of the current song being programmed
  uint32_t bank;                ///< Memory Bank currently being used to program the Song
  sha256_ctx hash_ctx;          ///< Current programmed Song Hash Calculation Context
  struct song_hdr_info_t info;  ///< Current programmed Song Info Header
  struct song_hdr_sec_t sec;    ///< Current programmed Song Security Header
  uint8_t  cached_cid;          ///< TOA CID of the current TPS session
  uint8_t  block_dataSize;      ///< datasize of the received block
};

/** @brief Metadata info stored in flash */
struct song_metadata_t
{
  uint8_t  valid;
  uint8_t  id;
};

/** @brief Cache for information related to the currently loaded song */
struct song_info_cache_t
{
  struct song_metadata_t curMeta;
  struct song_hdr_info_t curInfo;
  uint32_t curBank;
};


/**
 * Tile Programmable Songs module.
 *
 * Tile Lib supports the ability to update the find song over the air.
 */
struct tile_song_tps_module_t {
  /**
   * Public key used for ECC signature verification
   * Generating ECC keys:
   * $ openssl ecparam -genkey -name secp256k1 -out k.perm
   * $ openssl ec -outform DER -in k.perm -noout -text
   */
  uint8_t *pub_key;

  /**
   * If set to 0, it means the programmable song is currently not playable
   */
  uint8_t useProgrammableSong;

  /**
   * Buffer used to receive TPS Song data
   */
  uint8_t tileSongBuffer[TILE_SONG_BUFFER_SIZE];

   /**
   * Cache for information related to the currently loaded song
   */
  struct song_info_cache_t song_info_cache;

  /**
   * Internal state used by TPS
   */
  struct song_program_state_t state;

  /**
   * Song Programming is starting.
   *
   */
  int (*begin)(void);

  /**
   * A TPS block has been received. Write to nonvolatile storage.
   * After writting, it is recommended to read back the data from flash in the tileSongBuffer.
   * The reason is TileLib will check the CRC again after this call returns (song_block_done is called).
   *
   */
  int (*block_ready)(void);

  /**
   * TPS has completed successfully
   */
  int (*complete)(void);
};


/**
 * Tile Song module.
 *
 * This module is used to allow the Tile app to play a song on the device.
 */
struct tile_song_module {
  /**
   * Play song with given index number with strength from 0-3.
   */
  int (*play)(uint8_t number, uint8_t strength, uint8_t duration);

  /**
   * Stop all songs.
   */
  int (*stop)(void);

   /**
   * Optional TPS Module (set to NULL if not supported).
   */
  struct tile_song_tps_module_t* tps_module;
};


/**
 * Register the song module.
 */
int tile_song_register(struct tile_song_module *module);

/**
 * Call when the song programming begin command has completed.
 *
 * NOTE: Only required if TPS is supported.
 */
void song_begin_done(uint8_t error);

/**
 * Call when the song programming block ready command has completed.
 * tileSongBuffer is expected to contain the valid song block data when this function is called.
 * The reason is TileLib will check the CRC again in this function.
 * This allows the application to implement a read back of the data from flash to verify integrity.
 *
 * NOTE: Only required if TPS is supported.
 */
void song_block_done(uint8_t error);

/**
 * Call when the song programming complete command has completed.
 *
 * NOTE: Only required if TPS is supported.
 */
void song_complete_done(uint8_t error);


 /** @} */

#endif
