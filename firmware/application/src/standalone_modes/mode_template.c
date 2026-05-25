/*
 * mode_template.c
 *
 * Copy this file to firmware/application/src/standalone_modes/mode_<name>.c
 * and follow the numbered steps marked TODO to implement your mode.
 *
 * Checklist for a new mode:
 *   [ ] 1.  Copy file, rename to mode_<name>.c
 *   [ ] 2.  Assign a new mode ID in standalone_mode_t (app_standalone.h)
 *   [ ] 3.  Add extern decl to app_standalone.h (optionally gate with
 *             #if defined(PROJECT_CHAMELEON_ULTRA) if reader HW is needed)
 *   [ ] 4.  Add registry entry to app_standalone.c m_modes[]
 *   [ ] 5.  Add CONFIG_STANDALONE_<NAME> default define in app_standalone.c
 *   [ ] 6.  Add SRC_FILES entry in firmware/application/Makefile
 *   [ ] 7.  Add CLI config support in chameleon_cli_unit.py if needed
 *   [ ] 8.  Add mode color to m_palette[] in standalone_led.c
 *
 * Design rules (read before implementing):
 *
 *   ANTENNA / READER MODE
 *     If your mode uses the HF reader (RC522), turn the antenna on ONLY
 *     during the actual operation and off again before returning. Never
 *     leave the antenna powered while idle in ARMED state - it causes
 *     unexpected resets due to interactions with the BLE/USB stack and
 *     power management.
 *     Pattern to follow:
 *       pcd_14a_reader_reset();
 *       pcd_14a_reader_antenna_on();
 *       bsp_delay_ms(8);
 *       ... do the thing ...
 *       pcd_14a_reader_antenna_off();
 *     Call tag_mode_enter() in on_exit() to restore normal emulation.
 *
 *   FDS WRITES
 *     fds_write_sync() requires 4-byte-aligned data. Stack-local buffers
 *     with __attribute__((aligned(4))) are NOT reliable on nRF52 - use a
 *     file-static uint32_t[] staging buffer instead (see persist_config_save
 *     in app_standalone.c). Reads are alignment-safe.
 *
 *   BLOCKING
 *     on_tick() must return quickly (<1ms). Never spin-wait or call
 *     bsp_delay_ms() from the tick path - the tick runs in the main loop
 *     and will starve USB/BLE processing.
 *     on_button() and on_enter()/on_exit() may block briefly (the existing
 *     reader commands do up to ~300ms synchronously) but feed the WDT if
 *     doing anything longer than a second.
 *
 *   DESTRUCTIVE MODES
 *     Any mode that writes to tag memory or modifies slot contents MUST set
 *     writes_tag and/or writes_slot to true. The framework will refuse to
 *     arm the mode unless the host has set STANDALONE_FLAG_HOST_OPTED_IN.
 *     This prevents accidental data loss.
 *
 *   RESULT BUFFER
 *     Keep the result buffer in a file-static array (RAM only). The host
 *     drains it via repeated GET_RESULT calls. The buffer is lost on reboot.
 *     Use a write_cursor / read_cursor pattern (see mode_authtrace.c).
 *     Inform the user via documentation that results must be pulled before
 *     powering off.
 *
 *   HARDWARE GATING
 *     If your mode needs the RC522 (reader hardware, not available on Lite):
 *       - Wrap the entire file body in #if defined(PROJECT_CHAMELEON_ULTRA)
 *       - Gate the extern decl in app_standalone.h the same way
 *       - Gate the registry entry in app_standalone.c the same way
 */

#include "app_standalone.h"
#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"

/* -------------------------------------------------------------------------
 * TODO 1: Define your config struct.
 *
 * Keep it small (max 64 bytes). Every field must be explicitly sized
 * (no enums, no pointers, no padding). Pack the struct.
 * Increment CFG_VERSION any time the layout changes.
 * ------------------------------------------------------------------------- */

#define CFG_VERSION     1
#define MY_MODE_NAME    "my_mode"   /* short, no spaces, matches CLI name */

typedef struct __attribute__((packed)) {
    uint8_t  version;       /* always CFG_VERSION */
    uint8_t  my_param;      /* TODO: replace with your config fields */
    uint16_t my_timeout_ms;
    uint8_t  reserved[12];  /* pad to a round size */
} cfg_t;

_Static_assert(sizeof(cfg_t) <= 64, "config blob must be <= 64 bytes");

/* -------------------------------------------------------------------------
 * TODO 2: Define your result buffer.
 *
 * This is RAM only - lost on reboot. Size it for your worst-case payload.
 * Use a write_cursor / read_cursor pair so the host can drain it in chunks.
 * ------------------------------------------------------------------------- */

#define RESULT_BUFFER_BYTES     512     /* TODO: size for your use case */

static struct {
    cfg_t    cfg;
    bool     active;
    size_t   write_cursor;
    size_t   read_cursor;
    uint8_t  buffer[RESULT_BUFFER_BYTES];
} m_st;

/* -------------------------------------------------------------------------
 * Config helpers
 * ------------------------------------------------------------------------- */

static void apply_defaults(cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->version       = CFG_VERSION;
    c->my_param      = 0;           /* TODO: sensible defaults */
    c->my_timeout_ms = 3000;
}

static bool cfg_valid(const cfg_t *c) {
    if (c->version != CFG_VERSION) return false;
    /* TODO: add range checks for your fields */
    return true;
}

/* -------------------------------------------------------------------------
 * Lifecycle callbacks
 * ------------------------------------------------------------------------- */

/*
 * on_enter: called when the framework arms this mode.
 *
 * cfg / cfg_len is the persisted config blob (NULL / 0 if never set).
 * Return STANDALONE_RC_OK on success.
 * Return STANDALONE_RC_INVALID_STATE if hardware isn't available.
 * Do NOT power on the HF antenna here - do it per-operation in on_button.
 */
static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    apply_defaults(&m_st.cfg);

    if (cfg != NULL && cfg_len == sizeof(cfg_t)) {
        cfg_t parsed;
        memcpy(&parsed, cfg, sizeof(parsed));
        if (cfg_valid(&parsed)) {
            m_st.cfg = parsed;
        } else {
            NRF_LOG_WARNING("%s: invalid cfg, using defaults", MY_MODE_NAME);
        }
    }

    m_st.active = true;
    /* TODO: any one-time setup (switch to reader mode, etc.) */

    NRF_LOG_INFO("%s: armed", MY_MODE_NAME);
    return STANDALONE_RC_OK;
}

/*
 * on_exit: called when the framework disarms this mode.
 *
 * Clean up hardware state. If you entered reader mode in on_enter,
 * call tag_mode_enter() here to restore normal emulation.
 */
static standalone_rc_t on_exit(void) {
    m_st.active = false;
    /* TODO: pcd_14a_reader_antenna_off() + tag_mode_enter() if you used reader */
    NRF_LOG_INFO("%s: disarmed", MY_MODE_NAME);
    return STANDALONE_RC_OK;
}

/*
 * on_tick: called at ~10 Hz while armed (only if wants_tick = true).
 *
 * MUST return quickly. No blocking, no bsp_delay_ms(), no FDS writes.
 * Use for: timeout checks, state machine advances, periodic LED updates.
 */
static standalone_rc_t on_tick(uint32_t now_ms) {
    (void)now_ms;
    /* TODO: implement tick logic, or set wants_tick = false and remove */
    return STANDALONE_RC_OK;
}

/*
 * on_button: called when the user completes a chord gesture.
 *
 * BOTH_LONG is handled by the framework (arm/disarm) and never reaches here.
 * Handle BOTH_SHORT (primary action) and BOTH_VLONG (destructive/clear).
 *
 * May block for the duration of one RF operation. Feed the WDT if doing
 * anything that takes more than ~1 second.
 */
static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:
            /* TODO: implement primary action */
            NRF_LOG_INFO("%s: trigger", MY_MODE_NAME);
            standalone_feedback(SL_FB_SUCCESS);
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_VLONG:
            /* TODO: destructive action - clear buffer, reset state */
            m_st.write_cursor = 0;
            m_st.read_cursor  = 0;
            NRF_LOG_INFO("%s: cleared", MY_MODE_NAME);
            standalone_feedback(SL_FB_SUCCESS);
            return STANDALONE_RC_OK;

        default:
            return STANDALONE_RC_OK;
    }
}

/* -------------------------------------------------------------------------
 * Result retrieval
 *
 * The host calls GET_RESULT repeatedly until the chunk returned is empty.
 * read_result advances read_cursor; clear_result resets both cursors.
 * Return STANDALONE_RC_NO_RESULT when nothing is available (maps to an
 * empty 4-byte response on the wire, signalling end-of-data to the host).
 * ------------------------------------------------------------------------- */

static size_t get_result_size(void) {
    return m_st.write_cursor;
}

static standalone_rc_t read_result(uint8_t *out, size_t out_max, size_t *out_len) {
    if (out == NULL || out_len == NULL) return STANDALONE_RC_INVALID_CFG;

    if (m_st.read_cursor >= m_st.write_cursor) {
        *out_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }

    size_t remaining = m_st.write_cursor - m_st.read_cursor;
    size_t take      = (remaining < out_max) ? remaining : out_max;

    memcpy(out, &m_st.buffer[m_st.read_cursor], take);
    m_st.read_cursor += take;
    *out_len = take;
    return STANDALONE_RC_OK;
}

static void clear_result(void) {
    m_st.write_cursor = 0;
    m_st.read_cursor  = 0;
}

/* -------------------------------------------------------------------------
 * Descriptor
 *
 * TODO 3: Fill in the correct mode ID and set flags appropriately.
 *         Set writes_tag / writes_slot if your mode mutates card data.
 *         Set wants_tick = false if you don't need the ~10Hz tick.
 *         Set get_result_size / read_result / clear_result to NULL if
 *         your mode produces no results (e.g. slot_cycle).
 * ------------------------------------------------------------------------- */

const standalone_mode_iface_t mode_template_iface = {
    .id              = STANDALONE_MODE_DISABLED,  /* TODO: set your mode ID */
    .name            = MY_MODE_NAME,
    .writes_tag      = false,    /* TODO: true if you write to tag memory  */
    .writes_slot     = false,    /* TODO: true if you write to slot config */
    .wants_tick      = false,    /* TODO: true if you need on_tick         */
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = on_tick,  /* TODO: set to NULL if wants_tick=false  */
    .get_result_size = get_result_size,
    .read_result     = read_result,
    .clear_result    = clear_result,
};
