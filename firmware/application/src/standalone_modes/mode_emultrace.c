/*
 * mode_emultrace.c
 *
 * Standalone mode: CU acts as an emulated card and captures the complete
 * wire exchange when a real reader authenticates to it.
 *
 * Card-side counterpart to mode_authtrace. Both produce the same session
 * wire format and both feed mfkey32v2. Works on Lite and Ultra.
 *
 * Mechanism:
 *   on_enter() installs RX + TX sniff callbacks on the running NFCT
 *   emulation stack. Normal tag emulation continues — CU still responds
 *   to the reader correctly.
 *
 *   IMPORTANT: The sniff callbacks fire from the NFCT interrupt handler.
 *   All they do is copy bytes into the accumulator and set a volatile flag.
 *   All heavy work (LED feedback, FDS writes) happens in on_tick(), which
 *   runs in main-loop context where blocking is safe.
 *
 * Session boundary:
 *   A new REQA (7 bits, 0x26) arriving while the accumulator is non-empty
 *   sets m_session_ready. on_tick() detects this and commits the buffered
 *   session. Additionally, if no new frame arrives for IDLE_COMMIT_TICKS
 *   and the accumulator is non-empty, on_tick() commits automatically.
 *
 * Button mapping while armed:
 *   BOTH_SHORT   Force-commit whatever is in the accumulator now
 *   BOTH_LONG    Arm / disarm (framework)
 *   BOTH_VLONG   Discard all sessions + clear FDS
 *
 * Config: none — uses the active emulation slot as configured.
 *
 * Result wire format: identical to mode_authtrace.c (session header +
 * direction-tagged frames).
 */

#include "app_standalone.h"
#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"
#include "rfid/nfctag/hf/nfc_14a.h"
#include "tag_emulation.h"
#include "app_timer.h"      /* app_timer_cnt_get / app_timer_cnt_diff_compute */

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_SESSIONS             8
#define MAX_TRACE_BYTES          256
#define SESSION_HDR_BYTES        4
#define RESULT_BUFFER_BYTES      (MAX_SESSIONS * (SESSION_HDR_BYTES + MAX_TRACE_BYTES))
#define SESSION_ACCUM_BYTES      (MAX_TRACE_BYTES + 32)

/* Auto-commit after this many ticks of no frames (~2s at 32768 Hz) */
#define IDLE_COMMIT_TICKS        APP_TIMER_TICKS(2000)

#define STATUS_OK                0x00
#define STATUS_PARTIAL           0x01

#define REQA_BITS                7u
#define REQA_VALUE               0x26u

/* -------------------------------------------------------------------------
 * Result buffer — word-aligned for direct FDS writes
 * ------------------------------------------------------------------------- */

static uint32_t m_result_words[(RESULT_BUFFER_BYTES + 3) / 4];
#define m_result_buf ((uint8_t *)m_result_words)

/* Current-session accumulator */
static uint8_t  m_accum[SESSION_ACCUM_BYTES];
static uint16_t m_accum_len;

/* Flags written from ISR, read from main-loop on_tick */
static volatile bool    m_session_ready;     /* REQA boundary detected       */
static volatile uint32_t m_last_frame_ticks; /* tick of most recent frame    */

static struct {
    size_t   write_cursor;
    size_t   read_cursor;
    uint8_t  session_count;
    bool     active;
    bool     result_loaded;
} m_st;

/* -------------------------------------------------------------------------
 * Buffer helpers (main-loop context only)
 * ------------------------------------------------------------------------- */

static void buffer_reset(void) {
    m_st.write_cursor  = 0;
    m_st.read_cursor   = 0;
    m_st.session_count = 0;
}

static void accum_reset(void) {
    m_accum_len = 0;
}

/* Commit the current accumulator as a session — MAIN LOOP ONLY */
static bool commit_session(uint8_t status) {
    if (m_accum_len == 0) return false;

    uint16_t trace_len = m_accum_len;
    if (trace_len > MAX_TRACE_BYTES) trace_len = MAX_TRACE_BYTES;
    size_t need = SESSION_HDR_BYTES + trace_len;
    if (RESULT_BUFFER_BYTES - m_st.write_cursor < need) {
        standalone_feedback(SL_FB_ERROR);
        return false;
    }

    uint8_t *p = &m_result_buf[m_st.write_cursor];
    p[0] = m_st.session_count;
    p[1] = status;
    p[2] = (uint8_t)(trace_len      );
    p[3] = (uint8_t)(trace_len >>  8);
    memcpy(p + 4, m_accum, trace_len);
    m_st.write_cursor += need;
    m_st.session_count++;

    NRF_LOG_INFO("emultrace: session #%u status=0x%02x trace=%u bytes",
                 m_st.session_count - 1, status, trace_len);

    accum_reset();

    /* Persist and give feedback — safe here in main-loop context */
    app_standalone_save_result_buf(STANDALONE_MODE_EMUL_TRACE,
                                   m_result_words, m_st.write_cursor);
    standalone_feedback(SL_FB_SUCCESS);
    return true;
}

/* -------------------------------------------------------------------------
 * Lazy FDS load
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
 * NFCT sniff callbacks — ISR CONTEXT
 *
 * Rules: no blocking, no FDS, no LED. Only memcpy and flag writes.
 * -------------------------------------------------------------------------
 */

static void accum_frame_isr(const uint8_t *data, uint16_t szBits, bool is_tx) {
    uint16_t szBytes = (szBits + 7) / 8;
    if (m_accum_len + 2 + szBytes > SESSION_ACCUM_BYTES) return;
    uint16_t hdr = szBits | (is_tx ? 0x8000u : 0x0000u);
    m_accum[m_accum_len++] = (hdr >> 8) & 0xFF;
    m_accum[m_accum_len++] =  hdr       & 0xFF;
    memcpy(&m_accum[m_accum_len], data, szBytes);
    m_accum_len += szBytes;
    m_last_frame_ticks = app_timer_cnt_get();
}

static void emultrace_rx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;

    /* New REQA while we have frames — signal a completed session.
     * Set the flag; on_tick() does the actual commit. */
    if (szBits == REQA_BITS && data[0] == REQA_VALUE && m_accum_len > 0) {
        m_session_ready = true;
    }

    accum_frame_isr(data, szBits, false);   /* reader→card */
}

static void emultrace_tx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;
    accum_frame_isr(data, szBits, true);    /* card→reader */
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    (void)cfg; (void)cfg_len;

    accum_reset();
    m_session_ready    = false;
    m_last_frame_ticks = 0;
    m_st.active        = true;

    ensure_result_loaded();

    tag_emulation_load_data();

    nfc_tag_14a_set_sniff_cb(emultrace_rx_cb);
    nfc_tag_14a_set_tx_sniff_cb(emultrace_tx_cb);

    NRF_LOG_INFO("emultrace: armed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_exit(void) {
    nfc_tag_14a_clear_sniff_cb();
    nfc_tag_14a_clear_tx_sniff_cb();

    m_st.active     = false;
    m_session_ready = false;

    /* Commit any partial accumulator */
    if (m_accum_len > 0) {
        commit_session(STATUS_PARTIAL);
    }

    NRF_LOG_INFO("emultrace: disarmed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_tick(uint32_t now_ticks) {
    if (!m_st.active) return STANDALONE_RC_OK;

    /* Case 1: REQA boundary detected by RX callback */
    if (m_session_ready) {
        m_session_ready = false;
        /* The REQA itself is already in the accumulator. We want to commit
         * everything UP TO the REQA (i.e., the previous session). Trim the
         * last frame (REQA = 2 hdr bytes + 1 data byte = 3 bytes) before
         * committing, then re-add it to the fresh accumulator. */
        if (m_accum_len >= 3) {
            uint16_t save_len = m_accum_len - 3;
            uint8_t  reqa_frame[3];
            memcpy(reqa_frame, &m_accum[save_len], 3);
            m_accum_len = save_len;
            commit_session(STATUS_OK);
            /* Seed the new session with the REQA */
            memcpy(m_accum, reqa_frame, 3);
            m_accum_len = 3;
        } else {
            commit_session(STATUS_OK);
        }
        return STANDALONE_RC_OK;
    }

    /* Case 2: Idle timeout — reader walked away */
    if (m_accum_len > 0 && m_last_frame_ticks != 0) {
        uint32_t elapsed = app_timer_cnt_diff_compute(now_ticks,
                                                      m_last_frame_ticks);
        if (elapsed >= IDLE_COMMIT_TICKS) {
            m_last_frame_ticks = 0;
            commit_session(STATUS_OK);
        }
    }

    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:
            if (m_accum_len > 0) {
                m_session_ready    = false;
                m_last_frame_ticks = 0;
                commit_session(STATUS_PARTIAL);
            } else {
                standalone_feedback(SL_FB_ERROR);
            }
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_VLONG:
            accum_reset();
            buffer_reset();
            m_session_ready    = false;
            m_last_frame_ticks = 0;
            m_st.result_loaded = true;
            app_standalone_save_result_buf(STANDALONE_MODE_EMUL_TRACE, NULL, 0);
            NRF_LOG_INFO("emultrace: cleared");
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
    m_session_ready    = false;
    m_last_frame_ticks = 0;
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
    .wants_tick      = true,    /* needed for idle-timeout and session commit */
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = on_tick,
    .get_result_size = get_result_size,
    .read_result     = read_result,
    .clear_result    = clear_result,
};
