/*
 * mode_emultrace.c
 *
 * Standalone mode: CU acts as an emulated card and captures the complete
 * wire exchange when a real reader authenticates to it — REQA, ATQA,
 * anticollision, SELECT, SAK, auth command, NT, NR||AR, AT.
 *
 * This is the card-side counterpart to mode_authtrace (which has CU
 * acting as a reader against a real card). Both produce the same session
 * wire format and both feed mfkey32v2.
 *
 * Works on ChameleonLite as well as Ultra — NFCT only, no RC522.
 *
 * Mechanism:
 *   on_enter() installs RX + TX sniff callbacks onto the already-running
 *   NFCT emulation stack (same technique as CMD 2017 / hf 14a sniff, but
 *   non-blocking). Normal tag emulation continues uninterrupted; CU still
 *   responds to the reader correctly.
 *
 *   Session boundaries are detected by a fresh REQA (7-bit, 0x26) arriving
 *   while the current-session accumulator already has frames. When a new
 *   REQA arrives, the current session is committed to the result buffer and
 *   a fresh accumulator begins.
 *
 *   Sessions are persisted to FDS after each commit so they survive a
 *   reboot (same API as mode_authtrace).
 *
 * Button mapping while armed (chord-only):
 *   BOTH_SHORT   Manually commit whatever is in the current accumulator
 *                as a session (useful if the session boundary detection
 *                didn't fire e.g. reader never sent a second REQA)
 *   BOTH_LONG    Arm / disarm (handled by framework)
 *   BOTH_VLONG   Discard all stored sessions + clear FDS record
 *
 * Config: none — uses the active emulation slot as configured by the
 * user's normal slot settings. No separate config blob needed.
 *
 * Result wire format: identical to mode_authtrace.c
 *   For each session:
 *     u8  session_num
 *     u8  status       (0x00 = ok, 0x01 = partial / manually committed)
 *     u16 trace_len_le
 *     u8[trace_len] trace_bytes
 *   Each trace_bytes frame:
 *     u16 hdr_be       bit15=1: card→reader (TX), bit15=0: reader→card (RX)
 *                      bits14..0: frame size in bits
 *     u8[ceil(bits/8)] frame data
 */

#include "app_standalone.h"
#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"
#include "rfid/nfctag/hf/nfc_14a.h"
#include "tag_emulation.h"

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_SESSIONS             8
#define MAX_TRACE_BYTES          256
#define SESSION_HDR_BYTES        4
#define RESULT_BUFFER_BYTES      (MAX_SESSIONS * (SESSION_HDR_BYTES + MAX_TRACE_BYTES))

/* Current-session accumulator — one ongoing reader interaction */
#define SESSION_ACCUM_BYTES      (MAX_TRACE_BYTES + 32)  /* a bit of headroom */

#define STATUS_OK                0x00
#define STATUS_PARTIAL           0x01   /* manually committed, may be incomplete */

/* REQA: 7 bits, value 0x26 */
#define REQA_BITS                7u
#define REQA_VALUE               0x26u

/* -------------------------------------------------------------------------
 * Result buffer — word-aligned for direct FDS writes
 * ------------------------------------------------------------------------- */

static uint32_t m_result_words[(RESULT_BUFFER_BYTES + 3) / 4];
#define m_result_buf ((uint8_t *)m_result_words)

/* Current-session accumulator (in ISR/callback context — keep simple) */
static uint8_t  m_accum[SESSION_ACCUM_BYTES];
static uint16_t m_accum_len;

static struct {
    size_t  write_cursor;
    size_t  read_cursor;
    uint8_t session_count;
    bool    active;
    bool    result_loaded;
} m_st;

/* -------------------------------------------------------------------------
 * Buffer helpers
 * ------------------------------------------------------------------------- */

static void buffer_reset(void) {
    m_st.write_cursor  = 0;
    m_st.read_cursor   = 0;
    m_st.session_count = 0;
}

static void accum_reset(void) {
    m_accum_len = 0;
}

/* Append one frame to the accumulator (called from sniff callbacks) */
static void accum_frame(const uint8_t *data, uint16_t szBits, bool is_tx) {
    uint16_t szBytes = (szBits + 7) / 8;
    if (m_accum_len + 2 + szBytes > SESSION_ACCUM_BYTES) return;
    uint16_t hdr = szBits | (is_tx ? 0x8000u : 0x0000u);
    m_accum[m_accum_len++] = (hdr >> 8) & 0xFF;
    m_accum[m_accum_len++] =  hdr       & 0xFF;
    memcpy(&m_accum[m_accum_len], data, szBytes);
    m_accum_len += szBytes;
}

/* Commit the current accumulator as a new session in the result buffer */
static bool commit_session(uint8_t status) {
    if (m_accum_len == 0) return false;

    uint16_t trace_len = m_accum_len;
    if (trace_len > MAX_TRACE_BYTES) trace_len = MAX_TRACE_BYTES;
    size_t need = SESSION_HDR_BYTES + trace_len;
    if (RESULT_BUFFER_BYTES - m_st.write_cursor < need) return false;

    uint8_t *p = &m_result_buf[m_st.write_cursor];
    p[0] = m_st.session_count;
    p[1] = status;
    p[2] = (uint8_t)(trace_len      );
    p[3] = (uint8_t)(trace_len >>  8);
    memcpy(p + 4, m_accum, trace_len);
    m_st.write_cursor += need;
    m_st.session_count++;

    NRF_LOG_INFO("emultrace: committed session #%u status=0x%02x trace=%u bytes",
                 m_st.session_count - 1, status, trace_len);

    accum_reset();

    /* Persist to flash after each commit */
    app_standalone_save_result_buf(STANDALONE_MODE_EMUL_TRACE,
                                   m_result_words, m_st.write_cursor);
    return true;
}

/* -------------------------------------------------------------------------
 * Lazy FDS load (same pattern as mode_authtrace)
 * ------------------------------------------------------------------------- */

static void ensure_result_loaded(void) {
    if (m_st.result_loaded) return;
    m_st.result_loaded = true;

    size_t loaded = 0;
    if (app_standalone_load_result_buf(STANDALONE_MODE_EMUL_TRACE,
                                       m_result_words,
                                       RESULT_BUFFER_BYTES,
                                       &loaded) == STANDALONE_RC_OK
            && loaded > 0) {
        m_st.write_cursor = loaded;
        size_t off = 0;
        m_st.session_count = 0;
        while (off + 4 <= m_st.write_cursor) {
            uint16_t tlen = (uint16_t)m_result_buf[off + 2]
                          | ((uint16_t)m_result_buf[off + 3] << 8);
            off += 4 + tlen;
            m_st.session_count++;
        }
        NRF_LOG_INFO("emultrace: loaded %u session(s) from flash",
                     m_st.session_count);
    }
}

/* -------------------------------------------------------------------------
 * NFCT sniff callbacks
 *
 * Called from the NFCT event handler while tag emulation is running.
 * Keep these fast and non-blocking.
 * ------------------------------------------------------------------------- */

static void emultrace_rx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;

    /* REQA = start of a new reader interaction.
     * If we already have frames in the accumulator, commit the previous
     * session before starting fresh. */
    if (szBits == REQA_BITS && data[0] == REQA_VALUE && m_accum_len > 0) {
        commit_session(STATUS_OK);
        standalone_feedback(SL_FB_SUCCESS);
    }

    accum_frame(data, szBits, false);   /* reader→card */
}

static void emultrace_tx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;
    accum_frame(data, szBits, true);    /* card→reader (CU's own response) */
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    (void)cfg; (void)cfg_len;   /* no config for this mode */

    accum_reset();
    m_st.active = true;

    ensure_result_loaded();

    /* Install sniff callbacks onto the running NFCT emulation stack.
     * Do NOT call tag_mode_enter() here — that would reinitialise NFCT
     * and wipe the anti-collision data. The device must already be in
     * emulator mode (normal default). */
    tag_emulation_load_data();   /* ensure the active slot's UID is loaded */

    nfc_tag_14a_set_sniff_cb(emultrace_rx_cb);
    nfc_tag_14a_set_tx_sniff_cb(emultrace_tx_cb);

    NRF_LOG_INFO("emultrace: armed — sniff callbacks installed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_exit(void) {
    /* Remove callbacks first, then commit any partial session */
    nfc_tag_14a_clear_sniff_cb();
    nfc_tag_14a_clear_tx_sniff_cb();

    if (m_accum_len > 0) {
        commit_session(STATUS_PARTIAL);
    }

    m_st.active = false;
    NRF_LOG_INFO("emultrace: disarmed — sniff callbacks removed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:
            /* Manually commit the current accumulator */
            if (m_accum_len > 0) {
                commit_session(STATUS_PARTIAL);
                standalone_feedback(SL_FB_SUCCESS);
                NRF_LOG_INFO("emultrace: manual commit");
            } else {
                standalone_feedback(SL_FB_ERROR);
            }
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_VLONG:
            accum_reset();
            buffer_reset();
            m_st.result_loaded = true;   /* RAM is authoritative now */
            app_standalone_save_result_buf(STANDALONE_MODE_EMUL_TRACE, NULL, 0);
            NRF_LOG_INFO("emultrace: sessions cleared");
            standalone_feedback(SL_FB_SUCCESS);
            return STANDALONE_RC_OK;

        default:
            return STANDALONE_RC_OK;
    }
}

/* -------------------------------------------------------------------------
 * Result retrieval
 * ------------------------------------------------------------------------- */

static size_t get_result_size(void) {
    ensure_result_loaded();
    return m_st.write_cursor;
}

static standalone_rc_t read_result(uint8_t *out, size_t out_max, size_t *out_len) {
    if (out == NULL || out_len == NULL) return STANDALONE_RC_INVALID_CFG;

    ensure_result_loaded();

    if (m_st.read_cursor >= m_st.write_cursor) {
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
    accum_reset();
    buffer_reset();
    m_st.result_loaded = true;
    app_standalone_save_result_buf(STANDALONE_MODE_EMUL_TRACE, NULL, 0);
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */

const standalone_mode_iface_t mode_emultrace_iface = {
    .id              = STANDALONE_MODE_EMUL_TRACE,
    .name            = "emul_trace",
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
};
