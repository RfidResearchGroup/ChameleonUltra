/*
 * app_standalone.h
 *
 * Host-less workflow framework for ChameleonUltra.
 *
 * Modes are pluggable via the standalone_mode_iface_t descriptor; each mode
 * lives in its own translation unit under standalone_modes/ and exports a
 * const iface that app_standalone.c registers at init.
 *
 * Design rules:
 *   - All interface functions execute in main context. Button events arriving
 *     from ISR or BLE/USB worker must be deferred (e.g. via app_sched).
 *   - Modes that mutate tag data or slot contents MUST set writes_tag /
 *     writes_slot in their descriptor and refuse activation unless the host
 *     has set STANDALONE_FLAG_HOST_OPTED_IN via app_standalone_set_mode().
 *   - Mode IDs are persisted to FDS. Never renumber existing IDs; append.
 *   - State machine: DISARMED <-> ARMED_IDLE driven by chord gestures.
 *     BOTH_LONG (>=1s, <5s held simultaneously) is the universal arm/disarm.
 */

#ifndef APP_STANDALONE_H
#define APP_STANDALONE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */

typedef enum {
    STANDALONE_STATE_DISARMED = 0,   /* subsystem off; normal button cfg applies */
    STANDALONE_STATE_ARMED_IDLE,     /* armed; waiting for mode-select gesture   */
    STANDALONE_STATE_MODE_SELECT,    /* reserved for v2 on-device mode picker    */
    STANDALONE_STATE_MODE_ACTIVE,    /* a mode is running                        */
} standalone_state_t;

/* -------------------------------------------------------------------------
 * Mode identifiers (persisted - DO NOT RENUMBER)
 * ------------------------------------------------------------------------- */

typedef enum {
    STANDALONE_MODE_DISABLED    = 0x00,
    STANDALONE_MODE_AUTOCLONE   = 0x01,  /* writes_tag, writes_slot */
    STANDALONE_MODE_READ_REPLAY = 0x02,  /* writes_slot             */
    STANDALONE_MODE_AUTHTRACE   = 0x03,  /* active reader; logs auth exchanges */
    STANDALONE_MODE_SLOT_CYCLE  = 0x04,
    STANDALONE_MODE_DICT_CHECK  = 0x05,

    STANDALONE_MODE__COUNT              /* sentinel - keep last */
} standalone_mode_t;

/* -------------------------------------------------------------------------
 * Per-mode flags (persisted with mode selection)
 * ------------------------------------------------------------------------- */

#define STANDALONE_FLAG_HOST_OPTED_IN   (1u << 0) /* required for writes_* modes */
#define STANDALONE_FLAG_BUZZER_QUIET    (1u << 1)
#define STANDALONE_FLAG_LED_QUIET       (1u << 2)
/* bits 3..7 reserved */

/* -------------------------------------------------------------------------
 * Button event abstraction
 *
 * NOTE: Single-button events (A/B short or long) are NOT exposed here -
 * they are already claimed by the existing settings_button_function_t
 * config in settings.c (CycleSlot, CloneIcUid, NfcFieldGenerator, etc).
 * Standalone uses only simultaneous-press chords, which the existing
 * button driver does not produce and so cannot conflict with.
 *
 * Chord classification (measured from "both pressed" to "both released"):
 *   BOTH_SHORT  : duration <  1000 ms
 *   BOTH_LONG   : duration >= 1000 ms and < 5000 ms   (arm/disarm)
 *   BOTH_VLONG  : duration >= 5000 ms                 (destructive ops)
 * ------------------------------------------------------------------------- */

typedef enum {
    STANDALONE_BTN_NONE = 0,
    STANDALONE_BTN_BOTH_SHORT,    /* primary action               */
    STANDALONE_BTN_BOTH_LONG,     /* arm/disarm toggle            */
    STANDALONE_BTN_BOTH_VLONG,    /* destructive action (clear)   */
} standalone_button_evt_t;

/* -------------------------------------------------------------------------
 * Return codes (mode-level; translated to data_cmd status by the dispatcher)
 * ------------------------------------------------------------------------- */

typedef enum {
    STANDALONE_RC_OK = 0,
    STANDALONE_RC_BUSY,
    STANDALONE_RC_NO_TAG,
    STANDALONE_RC_NO_FREE_SLOT,
    STANDALONE_RC_WRITE_FAIL,
    STANDALONE_RC_NOT_PERMITTED,     /* mode needs HOST_OPTED_IN flag */
    STANDALONE_RC_INVALID_CFG,
    STANDALONE_RC_INVALID_STATE,
    STANDALONE_RC_BUFFER_FULL,
    STANDALONE_RC_NO_RESULT,
    STANDALONE_RC_INTERNAL,
} standalone_rc_t;

/* -------------------------------------------------------------------------
 * Mode descriptor
 *
 * One static const instance per mode, in its own translation unit.
 * All callbacks run in main context. Any callback may be NULL if the mode
 * doesn't need it.
 * ------------------------------------------------------------------------- */

typedef struct standalone_mode_iface {
    standalone_mode_t   id;
    const char *        name;          /* short, ASCII, no spaces */

    bool                writes_tag;    /* requires HOST_OPTED_IN */
    bool                writes_slot;   /* requires HOST_OPTED_IN */
    bool                wants_tick;    /* if true, on_tick called ~10 Hz */

    standalone_rc_t   (*on_enter)(const uint8_t *cfg, size_t cfg_len);
    standalone_rc_t   (*on_exit)(void);
    standalone_rc_t   (*on_button)(standalone_button_evt_t evt);
    standalone_rc_t   (*on_tick)(uint32_t now_ms);

    size_t            (*get_result_size)(void);
    standalone_rc_t   (*read_result)(uint8_t *out, size_t out_max, size_t *out_len);
    void              (*clear_result)(void);
} standalone_mode_iface_t;

/* -------------------------------------------------------------------------
 * Mode registry - extern decls. Conditionally compiled via the
 * CONFIG_STANDALONE_<MODE> defines in app_standalone.c.
 * ------------------------------------------------------------------------- */

extern const standalone_mode_iface_t mode_autoclone_iface;
extern const standalone_mode_iface_t mode_read_replay_iface;
#if defined(PROJECT_CHAMELEON_ULTRA)
extern const standalone_mode_iface_t mode_authtrace_iface;     /* needs reader hw */
#endif
extern const standalone_mode_iface_t mode_slot_cycle_iface;
extern const standalone_mode_iface_t mode_dict_check_iface;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void                app_standalone_init(void);

standalone_state_t  app_standalone_get_state(void);
standalone_mode_t   app_standalone_get_mode(void);
uint8_t             app_standalone_get_flags(void);

standalone_rc_t     app_standalone_set_mode(standalone_mode_t mode, uint8_t flags);

standalone_rc_t     app_standalone_set_config(standalone_mode_t mode,
                                              const uint8_t *cfg, size_t cfg_len);
standalone_rc_t     app_standalone_get_config(standalone_mode_t mode,
                                              uint8_t *cfg, size_t cfg_max,
                                              size_t *cfg_len);

/* -------------------------------------------------------------------------
 * Result-buffer persistence.
 *
 * Modes that produce results (authtrace etc.) should call
 * app_standalone_save_result_buf() after each successful append so captures
 * survive a reboot.  The framework loads the persisted buffer back in
 * on_enter so previously captured sessions are immediately available.
 *
 * FDS key layout (file FDS_STANDALONE_FILE_ID = 0x1010):
 *   0x0001              state (mode + flags)
 *   0x0100 + mode_id    per-mode config blob
 *   0x0200 + mode_id    per-mode result buffer
 *
 * buf_words must be a word-aligned (uint32_t[]) backing array — the caller
 * owns the buffer, the framework only writes to/from flash.  byte_len is
 * the number of valid bytes; 0 clears the persisted record.
 * ------------------------------------------------------------------------- */
#define FDS_KEY_STANDALONE_RESULT_BASE  0x0200   /* + mode_id */

/* Maximum result bytes the framework will persist in a single FDS record. */
#define STANDALONE_RESULT_PERSIST_MAX   2084u    /* 4-byte header + 2080 data */

standalone_rc_t app_standalone_save_result_buf(standalone_mode_t mode,
                                               const uint32_t *buf_words,
                                               size_t byte_len);
standalone_rc_t app_standalone_load_result_buf(standalone_mode_t mode,
                                               uint32_t *buf_words,
                                               size_t word_buf_bytes,
                                               size_t *out_byte_len);

/* Returns true if event was consumed; false to let normal button cfg handle it. */
bool                app_standalone_on_button(standalone_button_evt_t evt);

/* Main-loop tick. now_ms is monotonic ticks since boot (app_timer_cnt_get()). */
void                app_standalone_tick(uint32_t now_ms);

standalone_rc_t     app_standalone_read_result(uint8_t *out, size_t out_max,
                                               size_t *out_len);
standalone_rc_t     app_standalone_clear_result(void);

/* Manual trigger - host-side equivalent of BOTH_SHORT. */
standalone_rc_t     app_standalone_trigger(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STANDALONE_H */
