/*
 * mode_emultrace.c
 *
 * Standalone mode: CU acts as an emulated card and captures the complete
 * wire exchange when a real reader authenticates to it.
 *
 * Card-side counterpart to mode_authtrace. Both produce the same session
 * wire format and both feed mfkey32v2. Works on Lite and Ultra.
 *
 * Session boundary:
 *   A new REQA (7 bits, 0x26) arriving while the accumulator is non-empty
 *   marks a session boundary. The ISR saves the cut-point (accumulator
 *   length at the moment REQA arrives, before it is added). on_tick()
 *   commits everything before that cut-point as one session and moves
 *   everything from the cut-point onward (REQA + any subsequent frames
 *   that arrived before on_tick ran) into a fresh accumulator.
 *
 * Minimum session size:
 *   Only sessions with >= MIN_SESSION_BYTES of trace data are committed.
 *   This filters out spurious sessions caused by isolated REQAs, back-to-
 *   back REQAs, or idle-timeout firing on an accumulator that holds only
 *   a REQA/WUPA frame.
 *
 * Idle timeout:
 *   If no new frame arrives for IDLE_COMMIT_MS and the accumulator has
 *   enough content, it is auto-committed.
 */

#include "app_standalone.h"
#include "standalone_led.h"

#include <string.h>

#include "nrf_log.h"
#include "rfid/nfctag/hf/nfc_14a.h"
#include "tag_emulation.h"
#include "app_timer.h"

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_SESSIONS             8
#define MAX_TRACE_BYTES          256
#define SESSION_HDR_BYTES        4
#define RESULT_BUFFER_BYTES      (MAX_SESSIONS * (SESSION_HDR_BYTES + MAX_TRACE_BYTES))
#define SESSION_ACCUM_BYTES      (MAX_TRACE_BYTES + 64)

/* Commit after this many ms of silence (enough for a reader to finish) */
#define IDLE_COMMIT_MS           3000
#define IDLE_COMMIT_TICKS        APP_TIMER_TICKS(IDLE_COMMIT_MS)

/* Minimum trace bytes for a session to be worth committing.
 * REQA(3) + ATQA(4) + ANTICOLL_CMD(4) + UID_RESP(7) = 18 bytes minimum
 * for a session that got as far as anticollision.  Anything shorter is
 * a spurious REQA/WUPA or back-to-back REQA noise. */
#define MIN_SESSION_BYTES        16

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

/* ISR → on_tick communication:
 * m_session_cut is set by the RX ISR to m_accum_len AT THE MOMENT
 * the boundary REQA arrives (before the REQA itself is added to the
 * accumulator).  on_tick() uses it to split old session / new session. */
static volatile bool     m_session_ready;
static volatile uint16_t m_session_cut;
static volatile uint32_t m_last_frame_ticks;

static struct {
    size_t   write_cursor;
    size_t   read_cursor;
    uint8_t  session_count;
    bool     active;
    bool     result_loaded;
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
    m_accum_len        = 0;
    m_session_ready    = false;
    m_last_frame_ticks = 0;
}

/* Commit the current accumulator as a session — MAIN LOOP ONLY.
 * Skips sessions shorter than MIN_SESSION_BYTES (noise / spurious REQAs). */
static bool commit_session(uint8_t status) {
    if (m_accum_len == 0) return false;

    /* Filter out noise sessions */
    if (m_accum_len < MIN_SESSION_BYTES) {
        NRF_LOG_DEBUG("emultrace: skip tiny session (%u bytes)", m_accum_len);
        accum_reset();
        return false;
    }

    uint16_t trace_len = m_accum_len;
    if (trace_len > MAX_TRACE_BYTES) trace_len = MAX_TRACE_BYTES;
    size_t need = SESSION_HDR_BYTES + trace_len;
    if (RESULT_BUFFER_BYTES - m_st.write_cursor < need) {
        standalone_feedback(SL_FB_ERROR);
        accum_reset();
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

    NRF_LOG_INFO("emultrace: session #%u  %u bytes", m_st.session_count - 1, trace_len);

    accum_reset();

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
                                       m_result_words, RESULT_BUFFER_BYTES,
                                       &loaded) == STANDALONE_RC_OK
            && loaded > 0) {
        m_st.write_cursor = loaded;
        size_t off = 0; m_st.session_count = 0;
        while (off + 4 <= m_st.write_cursor) {
            uint16_t tlen = (uint16_t)m_result_buf[off + 2]
                          | ((uint16_t)m_result_buf[off + 3] << 8);
            off += 4 + tlen;
            m_st.session_count++;
        }
        NRF_LOG_INFO("emultrace: loaded %u session(s)", m_st.session_count);
    }
}

/* -------------------------------------------------------------------------
 * NFCT sniff callbacks — ISR CONTEXT: no blocking, no FDS, no LED
 * ------------------------------------------------------------------------- */

static void accum_frame_isr(const uint8_t *data, uint16_t szBits, bool is_tx) {
    uint16_t szBytes = (szBits + 7) / 8;
    if (m_accum_len + 2 + szBytes > SESSION_ACCUM_BYTES) return;
    uint16_t hdr = szBits | (is_tx ? 0x8000u : 0x0000u);
    m_accum[m_accum_len++] = (hdr >> 8) & 0xFF;
    m_accum[m_accum_len++] =  hdr       & 0xFF;
    memcpy(&m_accum[m_accum_len], data, szBytes);
    m_accum_len     += szBytes;
    m_last_frame_ticks = app_timer_cnt_get();
}

static void emultrace_rx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;

    /* Session boundary: REQA while accumulator has content.
     * Save the cut-point BEFORE adding the REQA. */
    if (szBits == REQA_BITS && data[0] == REQA_VALUE && m_accum_len > 0) {
        if (!m_session_ready) {
            m_session_cut   = m_accum_len;
            m_session_ready = true;
        }
        /* If already pending, leave cut-point as-is — on_tick will process. */
    }

    accum_frame_isr(data, szBits, false);
}

static void emultrace_tx_cb(const uint8_t *data, uint16_t szBits) {
    if (!m_st.active) return;
    accum_frame_isr(data, szBits, true);
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    (void)cfg; (void)cfg_len;
    accum_reset();
    m_session_cut  = 0;
    m_st.active    = true;
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
    m_st.active = false;
    if (m_accum_len >= MIN_SESSION_BYTES) {
        commit_session(STATUS_PARTIAL);
    } else {
        accum_reset();
    }
    NRF_LOG_INFO("emultrace: disarmed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_tick(uint32_t now_ticks) {
    if (!m_st.active) return STANDALONE_RC_OK;

    /* --- Case 1: REQA boundary detected by RX callback --- */
    if (m_session_ready) {
        uint16_t cut = m_session_cut;
        m_session_ready = false;
        m_session_cut   = 0;

        if (cut > 0) {
            /* Save everything from cut onward (REQA + any subsequent frames) */
            uint16_t tail_len = m_accum_len - cut;
            uint8_t  tail[SESSION_ACCUM_BYTES];
            if (tail_len > 0) memcpy(tail, &m_accum[cut], tail_len);

            /* Commit the session up to cut */
            m_accum_len = cut;
            commit_session(STATUS_OK);

            /* Seed new session with the tail */
            if (tail_len > 0) memcpy(m_accum, tail, tail_len);
            m_accum_len = tail_len;
        }
        return STANDALONE_RC_OK;
    }

    /* --- Case 2: Idle timeout --- */
    if (m_accum_len >= MIN_SESSION_BYTES && m_last_frame_ticks != 0) {
        uint32_t elapsed = app_timer_cnt_diff_compute(now_ticks, m_last_frame_ticks);
        if (elapsed >= IDLE_COMMIT_TICKS) {
            m_last_frame_ticks = 0;
            commit_session(STATUS_OK);
        }
    } else if (m_accum_len > 0 && m_accum_len < MIN_SESSION_BYTES && m_last_frame_ticks != 0) {
        /* Noise accumulator — discard after timeout */
        uint32_t elapsed = app_timer_cnt_diff_compute(now_ticks, m_last_frame_ticks);
        if (elapsed >= IDLE_COMMIT_TICKS) {
            NRF_LOG_DEBUG("emultrace: discard noise accum (%u bytes)", m_accum_len);
            accum_reset();
        }
    }

    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_st.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT:
            if (m_accum_len >= MIN_SESSION_BYTES) {
                m_session_ready    = false;
                m_last_frame_ticks = 0;
                commit_session(STATUS_PARTIAL);
            } else {
                accum_reset();
                standalone_feedback(SL_FB_ERROR);
            }
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_VLONG:
            accum_reset();
            buffer_reset();
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
    .wants_tick      = true,
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = on_tick,
    .get_result_size = get_result_size,
    .read_result     = read_result,
    .clear_result    = clear_result,
    .ensure_loaded   = ensure_result_loaded,
};
