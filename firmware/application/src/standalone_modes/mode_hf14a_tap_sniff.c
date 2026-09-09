/*
 * mode_hf14a_tap_sniff.c
 *
 * Standalone mode: passive HF14A "tap" sniff. CU stays silent while a REAL
 * card in the field answers a REAL reader; NFCT captures the reader->card
 * downlink and the RC522 captures the card->reader uplink from the shared
 * coil (no antenna flip, no emulated response). Each trigger captures one
 * session; results are persisted to flash for host retrieval later, same
 * as authtrace/emultrace.
 *
 * Thin wrapper around the existing hf14a_sniff_tap_run() engine in
 * app_cmd.c (same code path as CMD 2019, DATA_CMD_HF14A_SNIFF --tap).
 *
 * Use case: walk up to a reader/card pair you don't control a host
 * connection to (or don't want to leave a laptop next to), trigger N
 * captures with the button chord, walk back to the bench, pull all
 * session traces over BLE/USB afterward.
 *
 * Button mapping while armed (chord-only):
 *   BOTH_SHORT  run one tap-sniff capture at the configured timeout
 *   BOTH_LONG   arm/disarm (handled by framework)
 *   BOTH_VLONG  discard all stored sessions
 *
 * Capability flags:
 *   writes_tag  = false  - never touches the target's or CU's tag memory
 *   writes_slot = false  - results go to a private buffer, slots untouched
 * No HOST_OPTED_IN required.
 *
 * Config blob (8 bytes; defaults sensible if absent):
 *   u8  version       schema version, must be CFG_VERSION
 *   u8  reserved0
 *   u16 timeout_ms    per-capture listen duration (100..30000)
 *   u8  reserved1[4]
 *
 * Session record format in the result buffer (little-endian, packed) -
 * identical layout to authtrace/emultrace so host tooling
 * (parse_authtrace_buffer et al.) parses it unchanged:
 *   u8  session_num    0-based sequential session index
 *   u8  status         STATUS_HF_TAG_OK / STATUS_HF_TAG_NO
 *   u16 trace_len      length of the embedded trace bytes
 *   u8  trace[trace_len]   [hdr_be16][data]... records, same wire format
 *                          as CMD 2019's response (ts stripped, chrono order)
 *
 * Build gate: PROJECT_CHAMELEON_ULTRA only. CU Lite has no RC522 / reader
 * hardware so the entire mode is omitted from Lite builds. The mode
 * descriptor (mode_hf14a_tap_sniff_iface) is similarly omitted - see the
 * matching gate around its extern decl in app_standalone.h and the
 * conditional registry entry in app_standalone.c.
 */

#include "app_standalone.h"

#if defined(PROJECT_CHAMELEON_ULTRA)

#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"
#include "app_status.h"
#include "app_cmd.h"        /* hf14a_sniff_tap_run, hf14a_sniff_get_buf */
#include "rfid_main.h"      /* tag_mode_enter, get_device_mode           */

#define CFG_VERSION              1

#define MAX_SESSIONS             8
#define MAX_TRACE_BYTES          256      /* matches authtrace/emultrace sizing */
#define SESSION_HDR_BYTES        4
#define RESULT_BUFFER_BYTES      (MAX_SESSIONS * (SESSION_HDR_BYTES + MAX_TRACE_BYTES))

_Static_assert(RESULT_BUFFER_BYTES <= STANDALONE_RESULT_PERSIST_MAX,
               "hf14a_tap_sniff result buffer exceeds FDS persist limit");

#define DEFAULT_TIMEOUT_MS       5000     /* matches CMD 2019 default */

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  reserved0;
    uint16_t timeout_ms;
    uint8_t  reserved1[4];
} cfg_t;

_Static_assert(sizeof(cfg_t) == 8, "hf14a_tap_sniff cfg_t must be 8 bytes");

/* Result buffer — top-level word-aligned static so it can be passed directly
 * to app_standalone_save_result_buf() without a second staging copy.
 * uint32_t[] guarantees 4-byte BSS alignment required by fds_write_sync. */
static uint32_t m_result_words[(RESULT_BUFFER_BYTES + 3) / 4];
#define m_result_buf ((uint8_t *)m_result_words)

static struct {
    cfg_t    cfg;
    size_t   write_cursor;
    size_t   read_cursor;
    uint8_t  session_count;
    bool     tag_mode_acquired;
    bool     active;
    bool     result_loaded;   /* true once FDS result has been read into RAM */
} m_st;

/* -------------------------------------------------------------------------
 * Config helpers
 * ------------------------------------------------------------------------- */

static void apply_defaults(cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->version    = CFG_VERSION;
    c->timeout_ms = DEFAULT_TIMEOUT_MS;
}

static bool cfg_valid(const cfg_t *c) {
    if (c->version != CFG_VERSION) return false;
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
        STANDALONE_MODE_HF14A_TAP_SNIFF,
        m_result_words, RESULT_BUFFER_BYTES, &loaded);

    if (rc == STANDALONE_RC_OK && loaded > 0) {
        m_st.write_cursor = loaded;
        m_st.read_cursor  = 0;
        /* Rescan header chain to recount sessions */
        size_t off = 0;
        m_st.session_count = 0;
        while (off + 4 <= m_st.write_cursor) {
            uint16_t tlen = (uint16_t)m_result_buf[off + 2]
                          | ((uint16_t)m_result_buf[off + 3] << 8);
            off += 4 + tlen;
            m_st.session_count++;
        }
        NRF_LOG_INFO("hf14a_tap_sniff: lazy-loaded %u session(s) from flash",
                     m_st.session_count);
    }
}

/* -------------------------------------------------------------------------
 * Emulator-mode lifecycle
 *
 * hf14a_sniff_tap_run() requires the device already in emulator mode with
 * an active slot (its capture callback hangs off the NFCT tag-emulation
 * stack). Antenna handling is owned entirely by hf14a_sniff_tap_run() -
 * we only need to make sure NFCT/tag mode is the active front-end.
 * ------------------------------------------------------------------------- */

static bool acquire_tag_mode(void) {
    if (get_device_mode() != DEVICE_MODE_TAG) {
        tag_mode_enter();
    }
    return get_device_mode() == DEVICE_MODE_TAG;
}

static void release_tag_mode(void) {
    /* Restore normal emulation posture on the way out, mirroring the
     * template's guidance for modes that touch reader/tag mode state. */
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
    if (!m_st.tag_mode_acquired) return STANDALONE_RC_INVALID_STATE;

    standalone_feedback(SL_FB_BUSY_START);

    uint16_t cb_count = 0;
    uint16_t cap_len  = hf14a_sniff_tap_run(m_st.cfg.timeout_ms, &cb_count);

    uint16_t trace_len = 0;
    const uint8_t *trace = hf14a_sniff_get_buf(&trace_len);
    (void)cap_len;  /* trace_len from the getter is authoritative */

    uint8_t status = (trace_len > 0) ? STATUS_HF_TAG_OK : STATUS_HF_TAG_NO;

    bool stored = append_session(status, trace, trace_len);
    if (!stored) {
        standalone_feedback(SL_FB_ERROR);
        return STANDALONE_RC_BUFFER_FULL;
    }

    NRF_LOG_INFO("hf14a_tap_sniff: session #%u status=0x%02x trace=%u bytes cb=%u",
                 m_st.session_count - 1, status, trace_len, cb_count);

    /* Persist the updated buffer to flash so captures survive a reboot. */
    app_standalone_save_result_buf(STANDALONE_MODE_HF14A_TAP_SNIFF,
                                   m_result_words, m_st.write_cursor);

    if (status == STATUS_HF_TAG_OK) {
        standalone_feedback(SL_FB_SUCCESS);
        return STANDALONE_RC_OK;
    }
    standalone_feedback(SL_FB_BUSY_END);
    return STANDALONE_RC_NO_TAG;
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
        else NRF_LOG_WARNING("hf14a_tap_sniff: invalid cfg, using defaults");
    } else if (cfg_len != 0) {
        NRF_LOG_WARNING("hf14a_tap_sniff: cfg size %u != %u, using defaults",
                        (unsigned)cfg_len, (unsigned)sizeof(cfg_t));
    }

    m_st.active = true;

    /* Load any persisted sessions from flash (if not already in RAM from a
     * prior GET_RESULT call since boot). Resets read_cursor so the host
     * sees the full session list from the start on each new arm. */
    ensure_result_loaded();
    m_st.read_cursor = 0;

    m_st.tag_mode_acquired = acquire_tag_mode();
    if (!m_st.tag_mode_acquired) {
        NRF_LOG_WARNING("hf14a_tap_sniff: failed to enter tag mode");
        return STANDALONE_RC_INVALID_STATE;
    }

    NRF_LOG_INFO("hf14a_tap_sniff: armed timeout=%ums", m_st.cfg.timeout_ms);
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_exit(void) {
    if (m_st.tag_mode_acquired) {
        release_tag_mode();
        m_st.tag_mode_acquired = false;
    }
    m_st.active = false;
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:        /* primary: run one capture */
            return run_session();

        case STANDALONE_BTN_BOTH_VLONG:        /* destructive: discard all */
            buffer_reset();
            app_standalone_save_result_buf(STANDALONE_MODE_HF14A_TAP_SNIFF, NULL, 0);
            NRF_LOG_INFO("hf14a_tap_sniff: sessions cleared");
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
    app_standalone_save_result_buf(STANDALONE_MODE_HF14A_TAP_SNIFF, NULL, 0);
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */

const standalone_mode_iface_t mode_hf14a_tap_sniff_iface = {
    .id              = STANDALONE_MODE_HF14A_TAP_SNIFF,
    .name            = "hf14a_tap_sniff",
    .writes_tag      = false,
    .writes_slot     = false,
    .wants_tick      = false,
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = NULL,
    .get_result_size = get_result_size,
    .read_result     = read_result,
    .clear_result    = clear_result,
    .ensure_loaded   = ensure_result_loaded,
};

#endif /* PROJECT_CHAMELEON_ULTRA */
