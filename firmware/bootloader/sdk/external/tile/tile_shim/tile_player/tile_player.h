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

#ifndef TILE_PLAYER_H
#define TILE_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t g_FindActivate_SongPlayed;

#include "modules/tile_song_module.h"
#include "boards.h"

#define PIN_PIEZO ARDUINO_13_PIN
#define GPIOTE_SOUND_CHANNEL ((nrf_timer_cc_channel_t) 0) /**< PPI channel to use for connecting timer to piezo output */
#define PLAYER_TIMER_ID 1      /**< Timer ID to use with the player */

void InitPlayer(void);
void UninitPlayer(void);
int  PlaySong(uint8_t number, uint8_t strength, uint8_t duration);
int StopSong(void);
bool SongPlaying(void);
void tile_boot_config_player(void);
bool CheckFindSong(void);

typedef struct song_info
{
  const uint8_t *p_firstNote;     // pointer to start of song
  const uint8_t *p_currentNote;   // pointer to current note
  uint8_t        duration; 
}song_t;


#endif

