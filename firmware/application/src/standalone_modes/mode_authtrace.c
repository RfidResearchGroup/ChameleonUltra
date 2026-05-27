/*
 * mode_authtrace.c
 *
 * Standalone mode: each trigger fires one HF14A reader auth attempt and
 * captures the complete wire trace (REQA/ATQA/anticoll/SELECT/SAK/
 * RATS/ATS/auth/NT/NR||AR/AT) into a session-tagged buffer for later
 * host retrieval.
 *
 * Thin wrapper around the existing hf14a_auth_trace_run() machinery in
 * app_cmd.c (same code path as CMD 2017, DATA_CMD_HF14A_AUTH_TRACE).
 *
 * Use case: walk up to a target reader/card pair, trigger N sessions,
 * walk back to the bench, pull all session traces over BLE/USB for
 * offline analysis with Proxmark3-compatible decoders.
 *
 * Button mapping while armed (chord-only):
 *   BOTH_SHORT  run one auth session at the configured (type, block, key)
 *   BOTH_LONG   arm/disarm (handled by framework)
 *   BOTH_VLONG  discard all stored sessions
 *
 * Capability flags:
 *   writes_tag  = false  - we never write target memory
 *   writes_slot = false  - results go to a private buffer, slots untouched
 * No HOST_OPTED_IN required.
 *
 * Config blob (16 bytes; defaults sensible if absent):
 *   u8  version       schema version, must be CFG_VERSION
 *   u8  type          PICC_AUTHENT1A (0x60) or PICC_AUTHENT1B (0x61)
 *   u8  block         target block (e.g. 4 for first non-MAD sector key A)
 *   u8  reserved0
 *   u16 timeout_ms    per-session tag-poll timeout (100..30000)
 *   u8  key[6]        candidate sector key
 *   u8  reserved1[4]
 *
 * Session record format in the result buffer (little-endian, packed):
 *   u8  session_num    0-based sequential session index
 *   u8  status         STATUS_HF_* code from hf14a_auth_trace_run()
 *   u16 trace_len      length of the embedded trace bytes
 *   u8  trace[trace_len]   verbatim wire trace, same format as CMD 2017
 *
 * Build gate: PROJECT_CHAMELEON_ULTRA only. CU Lite has no RC522 / reader
 * hardware so the entire mode is omitted from Lite builds. The mode
 * descriptor (mode_authtrace_iface) is similarly omitted - see the
 * matching gate around its extern decl in app_standalone.h and the
 * conditional registry entry in app_standalone.c.
 */

#include "app_standalone.h"

#if defined(PROJECT_CHAMELEON_ULTRA)

#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"
#include "app_status.h"
#include "rc522.h"          /* PICC_AUTHENT1A/B, pcd_14a_reader_*       */
#include "rfid_main.h"      /* reader_mode_enter, get_device_mode, etc. */
#include "bsp_delay.h"
#include "app_cmd.h"        /* hf14a_auth_trace_run, hf14a_auth_trace_get_buf */

#define CFG_VERSION              1

#define MAX_SESSIONS             8
#define MAX_TRACE_BYTES          256      /* matches HF_AUTH_TRACE_BUF_SIZE */
#define SESSION_HDR_BYTES        4
#define RESULT_BUFFER_BYTES      (MAX_SESSIONS * (SESSION_HDR_BYTES + MAX_TRACE_BYTES))

#define DEFAULT_TYPE             PICC_AUTHENT1A
#define DEFAULT_BLOCK            4         /* first block of sector 1 */
#define DEFAULT_TIMEOUT_MS       3000
static const uint8_t DEFAULT_KEY[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  type;
    uint8_t  block;
    uint8_t  reserved0;
    uint16_t timeout_ms;
    uint8_t  key[6];
    uint8_t  reserved1[4];
} cfg_t;

_Static_assert(sizeof(cfg_t) == 16, "authtrace cfg_t must be 16 bytes");

typedef enum {
    AT_IDLE = 0,
    AT_SCANNING,
} at_state_t;

/* Result buffer — top-level word-aligned static so it can be passed directly
 * to app_standalone_save_result_buf() without a second staging copy.
 * uint32_t[] guarantees 4-byte BSS alignment required by fds_write_sync. */
static uint32_t m_result_words[(RESULT_BUFFER_BYTES + 3) / 4];
#define m_result_buf ((uint8_t *)m_result_words)

static struct {
    cfg_t       cfg;
    at_state_t  state;
    size_t      write_cursor;
    size_t      read_cursor;
    uint8_t     session_count;
    bool        reader_mode_acquired;
    bool        active;
    bool        result_loaded;   /* true once FDS result has been read into RAM */
} m_st;

/* -------------------------------------------------------------------------
 * Config helpers
 * ------------------------------------------------------------------------- */

static void apply_defaults(cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->version    = CFG_VERSION;
    c->type       = DEFAULT_TYPE;
    c->block      = DEFAULT_BLOCK;
    c->timeout_ms = DEFAULT_TIMEOUT_MS;
    memcpy(c->key, DEFAULT_KEY, 6);
}

static bool cfg_valid(const cfg_t *c) {
    if (c->version != CFG_VERSION) return false;
    if (c->type != PICC_AUTHENT1A && c->type != PICC_AUTHENT1B) return false;
    if (c->timeout_ms < 100 || c->timeout_ms > 30000) return false;
    return true;
}

/* -------------------------------------------------------------------------
 * Result buffer
 * ------------------------------------------------------------------------- */

static size_t bytes_free(void) {
    return RESULT_BUFFER_BYTES - m_st.write_cursor;
}

static bool append_session(uint8_t status, const uint8_t *trace, uint16_t trace_len) {
    if (trace_len > MAX_TRACE_BYTES) trace_len = MAX_TRACE_BYTES;
    size_t need = SESSION_HDR_BYTES + trace_len;
    if (bytes_free() < need) return false;

    uint8_t *p = &m_result_buf[m_st.write_cursor];
    p[0] = m_st.session_count;
    p[1] = status;
    p[2] = (uint8_t)(trace_len      );
    p[3] = (uint8_t)(trace_len >>  8);
    if (trace_len) memcpy(p + 4, trace, trace_len);

    m_st.write_cursor += need;
    m_st.session_count++;
    return true;
}

static void buffer_reset(void) {
    m_st.write_cursor  = 0;
    m_st.read_cursor   = 0;
    m_st.session_count = 0;
    /* Keep result_loaded = true after a clear — we know the FDS state
     * matches (empty), so no need to re-load on the next read. */
}

/* Load the persisted result buffer from FDS if not already in RAM.
 * Called lazily from read_result() so GET_RESULT works even before
 * the mode has been armed (on_enter never called after a reboot). */
static void ensure_result_loaded(void) {
    if (m_st.result_loaded) return;
    m_st.result_loaded = true;

    size_t loaded = 0;
    standalone_rc_t rc = app_standalone_load_result_buf(
        STANDALONE_MODE_AUTHTRACE,
        m_result_words, RESULT_BUFFER_BYTES, &loaded);

    if (rc == STANDALONE_RC_OK && loaded > 0) {
        m_st.write_cursor  = loaded;
        m_st.read_cursor   = 0;
        /* Rescan header chain to recount sessions */
        size_t off = 0;
        m_st.session_count = 0;
        while (off + 4 <= m_st.write_cursor) {
            uint16_t tlen = (uint16_t)m_result_buf[off + 2]
                          | ((uint16_t)m_result_buf[off + 3] << 8);
            off += 4 + tlen;
            m_st.session_count++;
        }
        NRF_LOG_INFO("authtrace: lazy-loaded %u session(s) from flash",
                     m_st.session_count);
    }
}

/* -------------------------------------------------------------------------
 * Reader-mode lifecycle
 *
 * We use the SAME pattern as the standard before_hf_reader_run /
 * after_hf_reader_run hooks: antenna is powered ONLY during an actual
 * capture session, never while idle in ARMED state. Sustained reader
 * mode with antenna on causes unexplained device resets - BLE/USB/power
 * management code assumes reader mode is transient.
 * ------------------------------------------------------------------------- */

static bool acquire_reader_mode(void) {
    if (get_device_mode() != DEVICE_MODE_READER) {
        reader_mode_enter();
    }
    /* Antenna stays OFF here - run_session() powers it for the duration
     * of one capture and turns it off again on the way out. */
    return get_device_mode() == DEVICE_MODE_READER;
}

static void release_reader_mode(void) {
    /* Ensure antenna is off (defensive - run_session should have done
     * it already) and step back to tag-emulation mode so the device
     * returns to its normal idle posture after disarming. */
    pcd_14a_reader_antenna_off();
    tag_mode_enter();
}

/* -------------------------------------------------------------------------
 * Session execution
 * ------------------------------------------------------------------------- */

static standalone_rc_t run_session(void) {
    if (bytes_free() < SESSION_HDR_BYTES) {
        standalone_feedback(SL_FB_ERROR);
        return STANDALONE_RC_BUFFER_FULL;
    }
    if (!m_st.reader_mode_acquired) return STANDALONE_RC_INVALID_STATE;

    standalone_feedback(SL_FB_BUSY_START);

    /* Power antenna for this session only (off both before and after,
     * so we never leave the field on while idle - see commentary above
     * acquire_reader_mode). Same shape as before_hf_reader_run +
     * after_hf_reader_run on a normal CMD 2017 invocation. */
    pcd_14a_reader_reset();
    pcd_14a_reader_antenna_on();
    bsp_delay_ms(8);

    uint8_t status = hf14a_auth_trace_run(m_st.cfg.type,
                                          m_st.cfg.block,
                                          m_st.cfg.key,
                                          m_st.cfg.timeout_ms);

    uint16_t trace_len = 0;
    const uint8_t *trace = hf14a_auth_trace_get_buf(&trace_len);

    pcd_14a_reader_antenna_off();   /* mandatory - mirror after_hf_reader_run */

    bool stored = append_session(status, trace, trace_len);
    if (!stored) {
        standalone_feedback(SL_FB_ERROR);
        return STANDALONE_RC_BUFFER_FULL;
    }

    NRF_LOG_INFO("authtrace: session #%u status=0x%02x trace=%u bytes",
                 m_st.session_count - 1, status, trace_len);

    /* Persist the updated buffer to flash so captures survive a reboot. */
    app_standalone_save_result_buf(STANDALONE_MODE_AUTHTRACE,
                                   m_result_words, m_st.write_cursor);

    if (status == STATUS_HF_TAG_OK) {
        standalone_feedback(SL_FB_SUCCESS);
        return STANDALONE_RC_OK;
    }
    standalone_feedback(SL_FB_BUSY_END);
    if (status == STATUS_HF_TAG_NO) return STANDALONE_RC_NO_TAG;
    return STANDALONE_RC_OK;  /* partial trace stored - not a framework error */
}

/* -------------------------------------------------------------------------
 * Lifecycle callbacks
 * ------------------------------------------------------------------------- */

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    apply_defaults(&m_st.cfg);

    if (cfg != NULL && cfg_len == sizeof(cfg_t)) {
        cfg_t parsed;
        memcpy(&parsed, cfg, sizeof(parsed));
        if (cfg_valid(&parsed)) m_st.cfg = parsed;
        else NRF_LOG_WARNING("authtrace: invalid cfg, using defaults");
    } else if (cfg_len != 0) {
        NRF_LOG_WARNING("authtrace: cfg size %u != %u, using defaults",
                        (unsigned)cfg_len, (unsigned)sizeof(cfg_t));
    }

    m_st.state  = AT_IDLE;
    m_st.active = true;

    /* Load any persisted sessions from flash (if not already in RAM from a
     * prior GET_RESULT call since boot). Resets read_cursor so the host
     * sees the full session list from the start on each new arm. */
    ensure_result_loaded();
    m_st.read_cursor = 0;

    m_st.reader_mode_acquired = acquire_reader_mode();
    if (!m_st.reader_mode_acquired) {
        NRF_LOG_WARNING("authtrace: failed to enter reader mode");
        return STANDALONE_RC_INVALID_STATE;
    }

    NRF_LOG_INFO("authtrace: armed type=0x%02x block=%u timeout=%ums",
                 m_st.cfg.type, m_st.cfg.block, m_st.cfg.timeout_ms);
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_exit(void) {
    if (m_st.reader_mode_acquired) {
        release_reader_mode();
        m_st.reader_mode_acquired = false;
    }
    m_st.active = false;
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_tick(uint32_t now_ms) {
    (void)now_ms;
    /* hf14a_auth_trace_run is synchronous (~50ms worst case incl polling),
     * so nothing to advance on tick. Hook stays for future async variants. */
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:        /* primary: run one session */
            return run_session();

        case STANDALONE_BTN_BOTH_VLONG:        /* destructive: discard all */
            buffer_reset();
            app_standalone_save_result_buf(STANDALONE_MODE_AUTHTRACE, NULL, 0);
            NRF_LOG_INFO("authtrace: sessions cleared");
            standalone_feedback(SL_FB_SUCCESS);
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_LONG:
            /* arm/disarm handled by framework */
        default:
            return STANDALONE_RC_OK;
    }
}

/* -------------------------------------------------------------------------
 * Result retrieval
 * ------------------------------------------------------------------------- */

static size_t get_result_size(void) {
    return m_st.write_cursor;
}

static standalone_rc_t read_result(uint8_t *out, size_t out_max, size_t *out_len) {
    if (out == NULL || out_len == NULL) return STANDALONE_RC_INVALID_CFG;

    /* Lazy-load from FDS so GET_RESULT works even before the mode has
     * been armed (i.e. on_enter never called after a reboot). */
    ensure_result_loaded();

    if (m_st.read_cursor >= m_st.write_cursor) {
        /* Auto-reset so the next drain starts from the beginning.
         * Data is only truly gone when clear_result() is called. */
        m_st.read_cursor = 0;
        *out_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }

    size_t remaining = m_st.write_cursor - m_st.read_cursor;
    size_t take      = (remaining < out_max) ? remaining : out_max;

    memcpy(out, &m_result_buf[m_st.read_cursor], take);
    m_st.read_cursor += take;
    *out_len = take;
    return STANDALONE_RC_OK;
}

static void clear_result(void) {
    buffer_reset();
    m_st.result_loaded = true;   /* RAM now matches FDS (both empty) */
    app_standalone_save_result_buf(STANDALONE_MODE_AUTHTRACE, NULL, 0);
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */

const standalone_mode_iface_t mode_authtrace_iface = {
    .id              = STANDALONE_MODE_AUTHTRACE,
    .name            = "authtrace",
    .writes_tag      = false,
    .writes_slot     = false,
    .wants_tick      = false,
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = on_tick,
    .get_result_size = get_result_size,
    .read_result     = read_result,
    .clear_result    = clear_result,
    .ensure_loaded   = ensure_result_loaded,
};

#endif /* PROJECT_CHAMELEON_ULTRA */
