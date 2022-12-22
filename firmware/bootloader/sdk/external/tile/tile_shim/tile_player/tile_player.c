/**
 * NOTICE
 * 
 * Copyright 2019 Tile Inc.  All Rights Reserved.
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
 * @file tile_player.c
 * @brief Tile player for Nordic Platform
 */

#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)
#include "tile_config.h"
#include "tile_lib.h"

uint8_t g_FindActivate_SongPlayed = 0;

#if TILE_ENABLE_PLAYER

#include <stdint.h>
#include <stdbool.h>

#include "tile_player.h"
#include "tile_storage/tile_storage.h"
#include "tile_service/tile_service.h"
#include "tile_features/tile_features.h"

#include "app_timer.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_ppi.h"
#include "nrfx_ppi.h"
#include "nrf_log.h"

static nrf_ppi_channel_t ppi_channel1;
uint32_t compare_evt_addr1;
uint32_t gpiote_task_addr1;
/* Set PIN_PIEZO for toggle on timer event */
nrf_drv_gpiote_out_config_t config1 = GPIOTE_CONFIG_OUT_TASK_TOGGLE(false);

/* Convert a frequency into number of microseconds for a half-pulse */
#define CONV(n) (1000000 / (n) / 2)
uint8_t g_inbetween = 0;

/* Some defines for accessing the note array properly */
#define NOTE_ARRAY_BASE_NOTE (C3)
#define NOTE_ARRAY_MAX_NOTE  (B8)
#define NOTE_ARRAY_INDEX(n) ((int) (n) - NOTE_ARRAY_BASE_NOTE)

/* Values for setting the PWM to the correct frequency for each note.
 * For our implementation, we only store the notes useful to us, which
 * is the range from C3 to B8 */
static const uint16_t notes[] = {
  [NOTE_ARRAY_INDEX(C3)] = CONV(131),
  [NOTE_ARRAY_INDEX(CS3)] = CONV(138),
  [NOTE_ARRAY_INDEX(D3)] = CONV(147),
  [NOTE_ARRAY_INDEX(DS3)] = CONV(156),
  [NOTE_ARRAY_INDEX(E3)] = CONV(165),
  [NOTE_ARRAY_INDEX(F3)] = CONV(175),
  [NOTE_ARRAY_INDEX(FS3)] = CONV(185),
  [NOTE_ARRAY_INDEX(G3)] = CONV(196),
  [NOTE_ARRAY_INDEX(GS3)] = CONV(208),
  [NOTE_ARRAY_INDEX(A3)] = CONV(220),
  [NOTE_ARRAY_INDEX(AS3)] = CONV(233),
  [NOTE_ARRAY_INDEX(B3)] = CONV(247),
  [NOTE_ARRAY_INDEX(C4)] = CONV(262),
  [NOTE_ARRAY_INDEX(CS4)] = CONV(277),
  [NOTE_ARRAY_INDEX(D4)] = CONV(294),
  [NOTE_ARRAY_INDEX(DS4)] = CONV(311),
  [NOTE_ARRAY_INDEX(E4)] = CONV(330),
  [NOTE_ARRAY_INDEX(F4)] = CONV(349),
  [NOTE_ARRAY_INDEX(FS4)] = CONV(370),
  [NOTE_ARRAY_INDEX(G4)] = CONV(392),
  [NOTE_ARRAY_INDEX(GS4)] = CONV(415),
  [NOTE_ARRAY_INDEX(A4)] = CONV(440),
  [NOTE_ARRAY_INDEX(AS4)] = CONV(466),
  [NOTE_ARRAY_INDEX(B4)] = CONV(494),
  [NOTE_ARRAY_INDEX(C5)] = CONV(523),
  [NOTE_ARRAY_INDEX(CS5)] = CONV(554),
  [NOTE_ARRAY_INDEX(D5)] = CONV(587),
  [NOTE_ARRAY_INDEX(DS5)] = CONV(622),
  [NOTE_ARRAY_INDEX(E5)] = CONV(659),
  [NOTE_ARRAY_INDEX(F5)] = CONV(698),
  [NOTE_ARRAY_INDEX(FS5)] = CONV(740),
  [NOTE_ARRAY_INDEX(G5)] = CONV(784),
  [NOTE_ARRAY_INDEX(GS5)] = CONV(831),
  [NOTE_ARRAY_INDEX(A5)] = CONV(880),
  [NOTE_ARRAY_INDEX(AS5)] = CONV(932),
  [NOTE_ARRAY_INDEX(B5)] = CONV(988),
  [NOTE_ARRAY_INDEX(C6)] = CONV(1047),
  [NOTE_ARRAY_INDEX(CS6)] = CONV(1109),
  [NOTE_ARRAY_INDEX(D6)] = CONV(1175),
  [NOTE_ARRAY_INDEX(DS6)] = CONV(1245),
  [NOTE_ARRAY_INDEX(E6)] = CONV(1319),
  [NOTE_ARRAY_INDEX(F6)] = CONV(1397),
  [NOTE_ARRAY_INDEX(FS6)] = CONV(1480),
  [NOTE_ARRAY_INDEX(G6)] = CONV(1568),
  [NOTE_ARRAY_INDEX(GS6)] = CONV(1661),
  [NOTE_ARRAY_INDEX(A6)] = CONV(1760),
  [NOTE_ARRAY_INDEX(AS6)] = CONV(1865),
  [NOTE_ARRAY_INDEX(B6)] = CONV(1976),
  [NOTE_ARRAY_INDEX(C7)] = CONV(2093),
  [NOTE_ARRAY_INDEX(CS7)] = CONV(2217),
  [NOTE_ARRAY_INDEX(D7)] = CONV(2349),
  [NOTE_ARRAY_INDEX(DS7)] = CONV(2489),
  [NOTE_ARRAY_INDEX(E7)] = CONV(2637),
  [NOTE_ARRAY_INDEX(F7)] = CONV(2794),
  [NOTE_ARRAY_INDEX(FS7)] = CONV(2960),
  [NOTE_ARRAY_INDEX(G7)] = CONV(3136),
  [NOTE_ARRAY_INDEX(GS7)] = CONV(3322),
  [NOTE_ARRAY_INDEX(A7)] = CONV(3520),
  [NOTE_ARRAY_INDEX(AS7)] = CONV(3729),
  [NOTE_ARRAY_INDEX(B7)] = CONV(3951),
  [NOTE_ARRAY_INDEX(C8)] = CONV(4186),
  [NOTE_ARRAY_INDEX(CS8)] = CONV(4435),
  [NOTE_ARRAY_INDEX(D8)] = CONV(4699),
  [NOTE_ARRAY_INDEX(DS8)] = CONV(4978),
  [NOTE_ARRAY_INDEX(E8)] = CONV(5274),
  [NOTE_ARRAY_INDEX(F8)] = CONV(5588),
  [NOTE_ARRAY_INDEX(FS8)] = CONV(5920),
  [NOTE_ARRAY_INDEX(G8)] = CONV(6272),
  [NOTE_ARRAY_INDEX(GS8)] = CONV(6645),
  [NOTE_ARRAY_INDEX(A8)] = CONV(7040),
  [NOTE_ARRAY_INDEX(AS8)] = CONV(7459),
  [NOTE_ARRAY_INDEX(B8)] = CONV(7902),
};

const uint8_t FixedSong0[] = {  C3, 1, REST, REST }; // Click Song

const uint8_t FixedSong1[] = {
  D5, 3, FS5, 3, D5, 3, FS5, 3, D5, 3, FS5, 3, D5, 3, FS5, 3, D5, 3, FS5, 6,
  REST, 3, D6, 13, FS5, 13, G5, 13,
  A5, 13, D6, 9, REST, 4, A5, 6,
  REST, 6, A6, 6, REST, 6, A5, 6, REST, 19, FS6, 3,
  A6, 3, FS6, 3, A6, 3, FS6, 3, A6, 3, REST, 6, D6, 3, FS6, 3, D6, 3, FS6, 3,
  D6, 3, FS6, 3, REST, 6, G5, 3, B5, 3, G5, 3, B5, 3, G5, 3, B5, 3, G5, 3,
  B5, 3, G5, 3, B5, 6, REST, 3, G6, 13, B5, 13,
  C6, 13, D6, 13, G6, 9,
  REST, 4, D6, 6, REST, 6, D7, 6, REST, 6, D6, 6,
  REST, 19, B6, 3, D7, 3, B6, 3, D7, 3,
  B6, 3, D7, 3, B6, 3, D7, 6, REST, 22, A5, 3,
  CS6, 3, A5, 3, CS6, 3, A5, 3, CS6, 3, A5, 3, CS6, 3, A5, 3, CS6, 6, REST, 3, A6, 13,
  CS6, 13, D6, 13, E6, 13,
  A6, 9, REST, 4, E6, 6, REST, 6, E7, 6,
  REST, 6, E6, 6, REST, 19, CS7, 3,
  E7, 3, CS7, 3, E7, 3, CS7, 3, E7, 3, REST, 6, A6, 3, CS7, 3, A6, 3, CS7, 3,
  A6, 3, CS7, 3, REST, 6, D6, 3, FS6, 3, D6, 3, FS6, 3, D6, 3, FS6, 3, D6, 3,
  FS6, 3, D6, 3, FS6, 6, REST, 3, D7, 13, FS6, 13,
  G6, 13, A6, 13, D7, 9,
  REST, 4, A6, 6, REST, 6, A7, 6, REST, 6, A6, 6,
  REST, 19, FS7, 3, A7, 3, FS7, 3, A7, 3,
  FS7, 3, A7, 3, FS7, 3, A7, 6, REST, 11,
  REST, REST, REST, REST
};

const uint8_t FixedSong2[] = { // Active Song
    A5, 5, REST, 7, A6, 2, REST, 11, A5, 2, REST, 23, A5, 2, REST, 11, A6, 2, REST, 11, A5, 2, REST, 23, D6, 13, FS5, 13, G5, 13, A5, 13, D5, 26, D6, 14, REST, REST
};

const uint8_t FixedSong3[] = { // Sleep Song
     A6, 38, D6, 13, G6, 13, FS6, 13, D6, 13, A5, 10, REST, 3, D5, 5, REST, 7, D6, 2, REST, 11, D5, 2, REST, 23, D5, 2, REST, 11, D6, 2, REST, 11, D3, 2, REST, 1, REST, REST
};

const uint8_t FixedSong4[] = { // Wake Song
D5,38,
A5,13,
FS5,13,
G5,13,
A5,13,
D6,10,

REST,3,     A5,5,
REST,7,     A6,2,
REST,11,    A5,2,

REST,23,    A5,2,
REST,11,    A6,2,
REST,11,    A5,2,
REST, REST };

const uint8_t FixedSong5[]  = { FS7,250, FS7,250, FS7,250, FS7,250, REST, REST };   /* Factory Song: For factory test song - 10 seconds of F#7 at 2960Hz */
const uint8_t FixedSong6[]  = { C8,1, CS8,1, D8,1, DS8,1, E8,1, F8,1, REST, REST };
const uint8_t FixedSong7[]  = { REST, REST };                                                                                /* Silent Song */
const uint8_t FixedSong8[]  = { REST, REST };                                                                                /* Button Song: currently silent */
const uint8_t FixedSong9[]  = { A5, 2, REST,11, A6, 2, REST, 11, A5, 2, REST,REST };   /* WakePart Song */
const uint8_t FixedSong10[] = { FS4, 3, REST,10, D5, 11, REST, 2, A5, 3, REST, 10, D6, 12, REST,REST };   /* double tap success Song */
const uint8_t FixedSong11[] = { GS4, 3, REST,10, GS5, 11, REST, 2, D4, 3, REST, 10, GS3, 12, REST,1, REST,REST };   /* double tap failure Song */
const uint8_t FixedSong12[] = { /*C3, 1, REST, 10, C3, 1,*/ REST, REST };               /* 2 clicks song */
const uint8_t FixedSong13[] = { D5, 30, REST, REST };   /* 1 bip Song */
const uint8_t FixedSong14[] = { D5, 30, REST,11,    D5, 30, REST, REST };   /* 2 bip Song */
const uint8_t FixedSong15[] = { D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST, REST };   /* 3 bip Song */
const uint8_t FixedSong16[] = { D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST, REST };   /* 4 bip Song */
const uint8_t FixedSong17[] = { D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST, REST };   /* 5 bip Song */
const uint8_t FixedSong18[] = { D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST, REST };   /* 6 bip Song */
const uint8_t FixedSong19[] = { D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST,11,    D5, 30, REST, REST };   /* 7 bip Song */

const uint8_t *tile_song_array[] = {
FixedSong0,
FixedSong1,
FixedSong2,
FixedSong3,
FixedSong4,
FixedSong5,
FixedSong6,
FixedSong7,
FixedSong8,
FixedSong9,
FixedSong10,
FixedSong11,
FixedSong12,
FixedSong13,
FixedSong14,
FixedSong15,
FixedSong16,
FixedSong17,
FixedSong18,
FixedSong19,
};

static uint8_t startup_sequence = 0;
static nrf_drv_timer_t player_timer = NRF_DRV_TIMER_INSTANCE(PLAYER_TIMER_ID);
APP_TIMER_DEF(song_timer_id);
APP_TIMER_DEF(song_timer_loop);


static void NextNote(void);
static void StartupPlayer(void);

static song_t current_song          = {0};
static song_t next_song             = {0};
static uint8_t song_loop_flag       = 1; 

static void song_timer_handler(void *p_context)
{
  /* TODO: guard against calling NextNote when song is not playing */
  if(startup_sequence)
  {
    StartupPlayer();
  }
  else
  {
    NextNote();
  }
}

/**
* @brief song_duration_timeout_handler
* This is the interrupt handler for the song duration timer. 
* The idea is that the variable song_loop_flag is set to 1 to ensure
* normal song_done() operation at all times. It is only set to 0 if 
* The song is requested to play forever. If it is requested to play for a 
* fixed duration, the timer allows the song_loop_flag to stay 0 until it times out.
*/

static void song_duration_timeout_handler(void *p_context)
{
  song_loop_flag = 1;
}

/**
 * @brief Player Boot Config
 * Allocate ppi Channels for Player once at Boot:
 *   Call once at Boot, so we don't keep on assigning it again and again:
 *     There are limited number of channels, and every channel alloc from same place does not alloc a new channel
 *     It returns error: channel already allocated and moves on without apparent immediate problem, but assigned channels can go out of context, 
 *     allocate more than needed channels, and then stop playing song/advertise in some scenarios
 *     So do not call alloc again and again
 */
void tile_boot_config_player(void)
{
  nrfx_err_t err_code;

  /* Initialize the gpiote driver if it isn't already */
  if(!nrf_drv_gpiote_is_init())
  {
    err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);
  }
  err_code = nrf_drv_ppi_channel_alloc(&ppi_channel1);
  NRF_LOG_INFO("ppi channel 1 alloc gave %u, &ppi_channel1: 0x%08x",err_code,&ppi_channel1);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_drv_gpiote_out_init(PIN_PIEZO, &config1);
  APP_ERROR_CHECK(err_code);
}

/**
 * @brief Configure PPI/GPIOTE for the layer
 */
static void ConfigureBuzzer()
{
    uint32_t compare_evt_addr;
    uint32_t gpiote_task_addr;
    nrf_ppi_channel_t ppi_channel;

    (void) nrf_drv_ppi_channel_alloc(&ppi_channel);

    /* Set PIN_PIEZO for toggle on timer event */
    nrf_drv_gpiote_out_config_t config = GPIOTE_CONFIG_OUT_TASK_TOGGLE(false);
    (void) nrf_drv_gpiote_out_init(PIN_PIEZO, &config);

    /* Tie timer events to piezo toggle */
    compare_evt_addr = nrf_drv_timer_event_address_get(&player_timer, NRF_TIMER_EVENT_COMPARE0);
    gpiote_task_addr = nrf_drv_gpiote_out_task_addr_get(PIN_PIEZO);
    (void) nrf_drv_ppi_channel_assign(ppi_channel, compare_evt_addr, gpiote_task_addr);
    (void) nrf_drv_ppi_channel_enable(ppi_channel);
}

/**
 * @brief Set the GPIO Player PIN as an Out PIN and Start Song after delay
 */
static void StartupPlayer()
{
    startup_sequence = 0;

    /* This is the start of the Find Default Song*/
    if (current_song.p_firstNote == tile_song_array[TILE_SONG_FIND])
    {
      g_FindActivate_SongPlayed = 1;
    }
    
    /* find length of the song */
    if (current_song.duration == TILE_SONG_DURATION_ONCE)
    {
      song_loop_flag = 1;
    }
    else if (current_song.duration == TILE_SONG_DURATION_FOREVER)
    {
      song_loop_flag = 0;
    }
    else
    {
      /* Create the timer for Song Loop */
      NRF_LOG_INFO("Song Request received for duration - %d", current_song.duration);
      song_loop_flag = 0;
      (void) app_timer_start(song_timer_loop, APP_TIMER_TICKS(current_song.duration * 1000), NULL);
    }

    nrf_gpio_pin_clear(PIN_PIEZO);
    nrf_drv_gpiote_out_task_enable(PIN_PIEZO);
    (void) app_timer_start(song_timer_id, APP_TIMER_TICKS(10), NULL);
}

/**
 * @brief Set the GPIO Player PIN to default state
 */
static void ShutdownPlayer()
{
  nrf_drv_gpiote_out_task_disable(PIN_PIEZO);
  nrf_gpio_pin_clear(PIN_PIEZO);
}

/**
 * @brief Nordic SDK requires an interrupt handler for Timer1, even though we do not need one 
 */
void timer_dummy_handler(nrf_timer_event_t event_type, void * p_context){}

/**
 * @brief Function called at the end of a song.
 * Will start enqueued song if any, otherwise shut everything Off and notify application.
 */
static void SongDone(void)
{
  NRF_LOG_INFO("Song Done");
  ShutdownPlayer();
  (void) app_timer_stop(song_timer_loop);
  g_inbetween = 0;
  if(NULL != next_song.p_firstNote)
  {
    /* Start enqueued Song */
    current_song = next_song;
    memset(&next_song,0,sizeof(song_t));
    startup_sequence = 1;
    /* Give 200 ms between songs */
    (void) app_timer_start(song_timer_id, APP_TIMER_TICKS(200), NULL);
  }
  else
  {
    /* Shut everything Off */
    nrf_drv_timer_disable(&player_timer);
    (void) app_timer_stop(song_timer_id);

    UninitPlayer();

    /********************************************************/
    memset(&current_song,0,sizeof(song_t));
    
    if (g_FindActivate_SongPlayed == 1)
    {
      if( (nrf_fstorage_is_busy(&app_data_bank0) == false) && (nrf_fstorage_is_busy(&app_data_bank1) == false) ) // If flash activity on-going, wait
      {
        if (BLE_CONN_HANDLE_INVALID   == tile_ble_env.conn_handle) // Disconnected
        {
          /* These will auto clear to 0 because of reboot, but still add it for logical purposes */
          g_FindActivate_SongPlayed = 0;
        }
      }
    }
  }
}

/**
 * @brief Play the next Note
 */
static void NextNote(void)
{
  uint8_t note             = *current_song.p_currentNote++;
  uint8_t duration         = *current_song.p_currentNote++;

  tile_unchecked->piezoMs += duration;
  
  nrf_drv_timer_disable(&player_timer);
  if(REST == note && REST == duration)
  {
    NRF_LOG_INFO("Song Duration: %d\r\n",tile_unchecked->piezoMs);

    if(song_loop_flag == 1) 
    {
      /* End of song reached */
      SongDone();
      return;
    }
    else
    {
      if (g_inbetween == 0)
      {
        NRF_LOG_INFO("song loop len is Non0, g_inbetween is 0");
        /* Give 200 ms between songs*/
        (void) app_timer_start(song_timer_id, APP_TIMER_TICKS(200), NULL);
        current_song.p_currentNote -=2;
        g_inbetween = 1;
      }
      else
      {
        NRF_LOG_INFO("song loop len is Non0, g_inbetween is 1");
        /* Start one more loop */
        current_song.p_currentNote = current_song.p_firstNote;
        note                       = *current_song.p_currentNote++;
        duration                   = *current_song.p_currentNote++;
        tile_unchecked->piezoMs   += duration;
        g_inbetween = 0;
      }
    }
  }

  /* We come here if we are in the middle of a song, or we are starting a new loop */
  if(REST == note)
  {
    /* reached a rest, disable the piezo pin and put it down */
    nrf_drv_gpiote_out_task_disable(PIN_PIEZO);
    nrf_gpio_pin_clear(PIN_PIEZO);
  }
  else
  {
    /* reached a note, set the Piezo Pin to toggle at the proper frequency */
    nrf_drv_timer_clear(&player_timer);
    nrf_drv_timer_extended_compare(&player_timer, GPIOTE_SOUND_CHANNEL, nrf_drv_timer_us_to_ticks(&player_timer, notes[NOTE_ARRAY_INDEX(note)]), NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);
    nrf_drv_gpiote_out_task_enable(PIN_PIEZO);

    if (nrf_drv_timer_is_enabled(&player_timer) == 0)
    nrf_drv_timer_enable(&player_timer);
  }

  if (g_inbetween == 0)
  {
    (void) app_timer_start(song_timer_id, APP_TIMER_TICKS(duration*10), NULL);
  }
}

/**
 * @brief Initialize the Tile player
 */
void InitPlayer(void)
{
  /* The Tile player uses GPIOTE to toggle the piezo pin, and
   * Timer1 triggers the toggle using PPI */
  nrf_drv_timer_config_t timer_config = NRF_DRV_TIMER_DEFAULT_CONFIG;

  /* Configure timer */
  (void) nrf_drv_timer_init(&player_timer, &timer_config, timer_dummy_handler);

  ConfigureBuzzer();

  /* Create the timer for switching frequencies */
  (void) app_timer_create(&song_timer_id, APP_TIMER_MODE_SINGLE_SHOT, song_timer_handler);

  /* Create the timer for Song Loop */
  (void) app_timer_create(&song_timer_loop, APP_TIMER_MODE_SINGLE_SHOT, song_duration_timeout_handler);

}

/**
 * @brief Uninitialize the Tile player, for Power Save reasons
 *        nrfx timer consumes ~0.5 mA current on Average
 */
void UninitPlayer(void)
{
  ret_code_t err_code;
  NRF_LOG_INFO("UninitPlayer");
  
  /* Do not call nrf_drv_ppi_uninit() or nrf_drv_gpiote_uninit() as they are used by other modules */
  nrf_drv_timer_uninit(&player_timer);

  err_code = nrf_drv_ppi_channel_disable(ppi_channel1);
  if (err_code != 0)
    NRF_LOG_INFO("ppi channel 1 disable gave error %u",err_code);
  // No Need to assert if Disable failed
}

/**
 * @brief Play a song. Queue song if necessary
 */
int PlaySong(uint8_t number, uint8_t strength, uint8_t duration)
{

  NRF_LOG_INFO("Play Song Request received");

  if(strength == 0 || duration == 0)
  {
    return TILE_ERROR_SUCCESS;
  }

  if(number < ARRAY_SIZE(tile_song_array))
  {
    if((NULL != current_song.p_firstNote) && (NULL == next_song.p_firstNote))
    {
      /* A song is currently playing but there is NO enqueued song */
      /* SO enqueue the song */

      NRF_LOG_INFO("Enqueue Default song");
      next_song   = (song_t) { tile_song_array[number], tile_song_array[number], duration};
    }
    else if(NULL == current_song.p_firstNote)
    {
      /* no song is currently playing, start it right away */
      InitPlayer();

      song_loop_flag = 1;

      NRF_LOG_INFO("Play Default song");
      current_song = (song_t) { tile_song_array[number], tile_song_array[number], duration};

      startup_sequence = 1;
      StartupPlayer();
    }
    else
    {
      /* If queue is full, ignore */
    }
  }
  return TILE_ERROR_SUCCESS;
}

/**
 * @brief Stop currently playing song and remove enqueued songs
 */
int StopSong(void)
{
  /* Destroy the queue */
  if(next_song.p_firstNote != NULL)
  {
    memset(&next_song,0,sizeof(song_t));
  }
  /* Turn off the songs */
  if(current_song.p_firstNote != NULL)
  {
    song_loop_flag = 1;
    SongDone();
  }
  return TILE_ERROR_SUCCESS;
}

/**
 * @brief Return whether a song is playing or not
 */
bool SongPlaying(void)
{
  return current_song.p_firstNote != NULL;
}

/**
 * @brief Return whether a Find song is playing or not
 */
bool CheckFindSong(void)
{
  /*Return true if the current song note matches to the TILE_SONG_FIND note*/
  if ( (current_song.p_firstNote != NULL) && 
      (current_song.p_firstNote == tile_song_array[TILE_SONG_FIND]) )
  {	
    return true; 
  } 

  return false; 
}


#else

void InitPlayer(void){}
void UninitPlayer(void){}
int PlaySong(uint8_t number, uint8_t strength, uint8_t duration){return TILE_ERROR_SUCCESS;}
int StopSong(void){return TILE_ERROR_SUCCESS;}
bool SongPlaying(void){return false;}
void tile_boot_config_player(void){}

#endif // TILE_ENABLE_PLAYER

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
