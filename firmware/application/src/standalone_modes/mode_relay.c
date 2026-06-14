/*
 * mode_relay.c
 *
 * Transparent BLE relay between two ChameleonUltra devices.
 *
 *   RELAY_CARD  (lower BLE MAC) — NFCT, faces the real reader
 *   RELAY_READER (higher BLE MAC) — RC522, faces the real card
 *
 * Flow:
 *   1. CU2 (READER) scans real card → sends CARD_IDENTITY to CU1
 *   2. CU1 (CARD) sets up NFCT emulation with real card's identity
 *   3. Reader approaches CU1 → anticollision handled locally (fast)
 *   4. Post-SELECT: every reader frame is forwarded to CU2 via BLE
 *   5. CU2 relays frame to real card via RC522 → gets response
 *   6. CU2 sends response to CU1 via BLE → CU1 sends to reader
 *   7. All frames accumulated in trace buffer → persisted on disarm
 *
 * Result format per session:
 *   [u8  role]       0=CARD, 1=READER
 *   [u8  status]     0=OK, 1=TIMEOUT, 2=DISCONNECT
 *   [u8  uid_len]
 *   [u8[4] uid]      4-byte extracted UID
 *   [u8[2] atqa]
 *   [u8  sak]
 *   [u16 frame_count_le]
 *   [u16 trace_len_le]
 *   [u8[4] reserved]
 *   [trace bytes...]  frames in AuthTrace wire format:
 *                     [u16 hdr: bit15=tag→reader, bits14-0=bit_count]
 *                     [u8[] raw frame bytes]
 */

#include "app_standalone.h"
#include "standalone_led.h"

/* Exported to ble_main.c to suppress spurious battery-shutdown during relay */
bool g_is_standalone_armed = false;
#include "ble_relay.h"

#include <string.h>
#include <stdint.h>
#include "nrf.h"   /* CMSIS core intrinsics: __DMB() memory barrier */

#include "nrf_log.h"
#include "app_timer.h"
#include "app_status.h"

#include "rfid/nfctag/hf/nfc_14a.h"
#include "rfid/nfctag/hf/nfc_relay_tag.h"
#include "rfid_main.h"
#include "bsp/bsp_delay.h"
#include "utils/syssleep.h"
#include "rfid/reader/hf/rc522.h"
#include "tag_emulation.h"

/* -------------------------------------------------------------------------
 * Config
 * ------------------------------------------------------------------------- */
#define RELAY_DEFAULT_WTX_MS     2000u
#define RELAY_LINK_TIMEOUT_MS    120000u
#define RELAY_FRAME_TIMEOUT_MS   3000u    /* max wait for response from real card */

/* ISO14443-4 S(WTX) block */
#define RELAY_WTX_BLOCK_CMD      0xF2
#define RELAY_WTX_MULTIPLIER     1

/* -------------------------------------------------------------------------
 * Result storage
 *
 * Variable-length session records:
 *   16-byte fixed header + variable trace
 *   Max total buffer: 4KB
 * ------------------------------------------------------------------------- */
#define RELAY_RESULT_BUF_BYTES    4096u

/* Session header offsets */
#define RH_ROLE         0
#define RH_STATUS       1
#define RH_UID_LEN      2
#define RH_UID          3   /* 4 bytes */
#define RH_ATQA         7   /* 2 bytes */
#define RH_SAK          9
#define RH_FRAME_COUNT  10  /* u16 LE */
#define RH_TRACE_LEN    12  /* u16 LE */
#define RH_RESERVED     14  /* 2 bytes */
#define RH_HEADER_SIZE  16

#define RELAY_SESSION_OK          0x00
#define RELAY_SESSION_TIMEOUT     0x01
#define RELAY_SESSION_DISCONNECT  0x02

/* Trace buffer per session (accumulates during relay, flushed on save) */
#define RELAY_TRACE_BUF_BYTES  1024u

static uint32_t m_result_words[(RELAY_RESULT_BUF_BYTES + 3) / 4];
#define m_result_buf ((uint8_t *)m_result_words)

static uint8_t  m_trace_buf[RELAY_TRACE_BUF_BYTES];
static uint16_t m_trace_len;
static uint16_t m_trace_frame_count;

/* Append one frame to the in-RAM trace buffer */
static void trace_append(bool tag_to_reader, const uint8_t *data, uint16_t bits) {
    uint16_t bytes = (bits + 7) / 8;
    uint16_t needed = 2 + bytes;
    if (m_trace_len + needed > RELAY_TRACE_BUF_BYTES) return;
    uint16_t hdr = (tag_to_reader ? 0x8000u : 0u) | (bits & 0x7FFFu);
    m_trace_buf[m_trace_len++] = (uint8_t)(hdr >> 8);
    m_trace_buf[m_trace_len++] = (uint8_t)(hdr     );
    if (bytes && data) memcpy(m_trace_buf + m_trace_len, data, bytes);
    m_trace_len += bytes;
    m_trace_frame_count++;
}

/* -------------------------------------------------------------------------
 * Sub-states
 * ------------------------------------------------------------------------- */
typedef enum {
    RS_INIT = 0,
    RS_LINKING,
    RS_CARD_AWAIT_IDENTITY,
    RS_CARD_READY,              /* emulating, waiting for reader */
    RS_CARD_AWAIT_RESPONSE,     /* frame sent to CU2, waiting for response */
    RS_READER_SCAN,
    RS_READER_READY,
    RS_READER_RELAY,
    RS_ERROR,
} relay_sub_state_t;

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */
static struct {
    relay_sub_state_t sub;
    uint8_t           role;
    uint32_t          wtx_ms;

    relay_card_identity_t identity;
    bool identity_received;
    bool reader_sent_ready;


    /* Generic frame relay: set in frame_cb ISR, consumed in on_tick */
    volatile bool  frame_pending;
    uint8_t        frame_buf[256]; /* large enough for any ISO14443-4 APDU */
    uint16_t       frame_bits;

    /* Response from real card (set in on_response CB, consumed in on_tick) */
    volatile bool  response_ready;
    bool           no_response;
    uint8_t        response_buf[256];
    uint16_t       response_bits;

    /* READER: pending frame from CU1 (set in on_frame CB) */
    volatile bool  reader_frame_pending;
    uint8_t        reader_frame_buf[256];
    uint16_t       reader_frame_bits;

    /* Timing */
    uint32_t link_start_ticks;
    uint32_t frame_sent_ticks;

    picc_14a_tag_t real_card;
    bool           real_card_found;
    bool           rescan_pending;  /* set by on_rescan_req, handled in on_tick */
    bool           needs_reselect;   /* true after RATS — re-select before first I-block */

    /* Result tracking */
    size_t  result_write;
    size_t  result_read;
    uint8_t session_count;
    bool    result_loaded;

    bool was_connected;  /* set on_connected, never cleared — used by on_exit */
    bool active;
} m_st;

/* nfc_relay_tag.h functions are included above */

/* -------------------------------------------------------------------------
 * WTX helper (ISO14443-4 only)
 * ------------------------------------------------------------------------- */
static void send_wtx(uint8_t cid) {
    /* S(WTX) PCB = 0xF2, set CID bit (bit 3) and CID byte if reader uses CID */
    uint8_t wtx[5];
    uint8_t pcb = RELAY_WTX_BLOCK_CMD;
    uint8_t len = 2;
    if (cid != 0xFF) {
        pcb |= 0x08;           /* CID present */
        wtx[0] = pcb;
        wtx[1] = cid & 0x0F;  /* CID byte */
        wtx[2] = RELAY_WTX_MULTIPLIER;
        len = 3;
    } else {
        wtx[0] = pcb;
        wtx[1] = RELAY_WTX_MULTIPLIER;
    }
    nfc_tag_14a_append_crc(wtx, len);
    nfc_tag_14a_tx_bytes(wtx, len + 2, false);
}

/* -------------------------------------------------------------------------
 * Frame ISR callback (NFCT ISR context — ISR-safe: copy and flag only)
 * ------------------------------------------------------------------------- */
static void on_frame_isr(const uint8_t *data, uint16_t bits) {
    if (m_st.frame_pending) return;  /* previous frame not yet processed */
    uint16_t bytes = (bits + 7) / 8;
    if (bytes > sizeof(m_st.frame_buf)) bytes = sizeof(m_st.frame_buf);
    memcpy(m_st.frame_buf, data, bytes);
    m_st.frame_bits    = bits;
    __DMB();  /* publish buffer writes before the flag the consumer polls */
    m_st.frame_pending = true;
}

/* -------------------------------------------------------------------------
 * Result helpers
 * ------------------------------------------------------------------------- */
static void result_ensure_loaded(void) {
    if (m_st.result_loaded) return;
    m_st.result_loaded = true;
    size_t loaded = 0;
    standalone_rc_t rc = app_standalone_load_result_buf(
        STANDALONE_MODE_RELAY, m_result_words, RELAY_RESULT_BUF_BYTES, &loaded);
    if (rc == STANDALONE_RC_OK && loaded > 0) {
        m_st.result_write = loaded;
        m_st.result_read  = 0;
    }
}

/* Forward declaration */
static void result_save_session(uint8_t status);

/* Flush current session state to FDS, overwriting any previous save.
 * Used for autosave — keeps exactly one in-progress session record. */
static void result_flush_session(uint8_t status) {
    /* Reset write cursor to overwrite the last session only */
    result_ensure_loaded();
    m_st.result_write  = 0;
    m_st.result_read   = 0;
    m_st.session_count = 0;
    result_save_session(status);
}

static void result_save_session(uint8_t status) {
    result_ensure_loaded();

    /* Make sure trace fits; if not, drop oldest session */
    uint16_t needed = RH_HEADER_SIZE + m_trace_len;
    while (m_st.result_write + needed > RELAY_RESULT_BUF_BYTES
           && m_st.result_write >= RH_HEADER_SIZE) {
        /* Drop oldest session: read its trace_len from header */
        uint16_t oldest_trace = (uint16_t)m_result_buf[RH_TRACE_LEN]
                              | ((uint16_t)m_result_buf[RH_TRACE_LEN+1] << 8);
        uint16_t oldest_total = RH_HEADER_SIZE + oldest_trace;
        if (oldest_total > m_st.result_write) break;
        memmove(m_result_buf, m_result_buf + oldest_total,
                m_st.result_write - oldest_total);
        m_st.result_write -= oldest_total;
        if (m_st.session_count > 0) m_st.session_count--;
    }

    if (m_st.result_write + needed > RELAY_RESULT_BUF_BYTES) {
        NRF_LOG_WARNING("relay: result buffer full, session dropped");
        return;
    }

    uint8_t *h = &m_result_buf[m_st.result_write];
    memset(h, 0, RH_HEADER_SIZE);

    uint8_t own[6] = {0}, peer[6] = {0};
    ble_relay_get_my_addr(own);
    ble_relay_get_peer_addr(peer);
    (void)own; (void)peer;

    h[RH_ROLE]   = m_st.role;
    h[RH_STATUS] = status;

    /* On READER: record the real card found by RC522.
     * On CARD:   record the identity received from READER CU. */
    if (m_st.role == BLE_RELAY_ROLE_READER && m_st.real_card_found) {
        h[RH_UID_LEN] = m_st.real_card.uid_len;
        memcpy(&h[RH_UID],  m_st.real_card.uid,  m_st.real_card.uid_len > 4 ? 4 : m_st.real_card.uid_len);
        memcpy(&h[RH_ATQA], m_st.real_card.atqa, 2);
        h[RH_SAK] = m_st.real_card.sak;
    } else if (m_st.identity.uid_len > 0) {
        h[RH_UID_LEN] = m_st.identity.uid_len > 4 ? 4 : m_st.identity.uid_len;
        memcpy(&h[RH_UID],  m_st.identity.uid,  4);
        memcpy(&h[RH_ATQA], m_st.identity.atqa, 2);
        h[RH_SAK] = m_st.identity.sak;
    }

    h[RH_FRAME_COUNT    ] = (uint8_t)(m_trace_frame_count     );
    h[RH_FRAME_COUNT + 1] = (uint8_t)(m_trace_frame_count >> 8);
    h[RH_TRACE_LEN      ] = (uint8_t)(m_trace_len             );
    h[RH_TRACE_LEN   + 1] = (uint8_t)(m_trace_len          >> 8);

    m_st.result_write += RH_HEADER_SIZE;
    if (m_trace_len > 0) {
        memcpy(&m_result_buf[m_st.result_write], m_trace_buf, m_trace_len);
        m_st.result_write += m_trace_len;
    }
    m_st.session_count++;
    m_st.result_read = 0;

    /* FDS write can block for seconds on GC with fragmented flash.
     * Cancel sleep timer before blocking so it can't fire mid-write. */
    sleep_timer_stop();
    standalone_rc_t save_rc = app_standalone_save_result_buf(STANDALONE_MODE_RELAY,
                                   m_result_words, m_st.result_write);
    if (save_rc != STANDALONE_RC_OK) {
        NRF_LOG_WARNING("relay: FDS save failed rc=%d write=%u", save_rc, m_st.result_write);
        standalone_feedback(SL_FB_ERROR);
    } else {
        NRF_LOG_INFO("relay: session saved frames=%u trace=%u status=%u",
                     m_trace_frame_count, m_trace_len, status);
        standalone_feedback(SL_FB_SUCCESS);   /* brief green = saved OK */
    }

    /* Clear trace for next session */
    m_trace_len         = 0;
    m_trace_frame_count = 0;
}

/* -------------------------------------------------------------------------
 * BLE relay callbacks (main-loop context via ble_relay_process())
 * ------------------------------------------------------------------------- */

static void on_connected(uint8_t my_role) {
    m_st.role = my_role;
    m_st.was_connected = true;
    NRF_LOG_INFO("relay: connected role=%s",
                 my_role == BLE_RELAY_ROLE_CARD ? "CARD" : "READER");

    if (my_role == BLE_RELAY_ROLE_CARD) {
        standalone_feedback(SL_FB_SUCCESS);
        standalone_led_set_mode_color(STANDALONE_MODE_RELAY, RGB_BLUE);
    } else {
        standalone_feedback(SL_FB_SUCCESS);
        standalone_led_set_mode_color(STANDALONE_MODE_RELAY, RGB_GREEN);
    }
    standalone_led_solid();

    if (my_role == BLE_RELAY_ROLE_CARD) {
        m_st.sub = RS_CARD_AWAIT_IDENTITY;
    } else {
        reader_mode_enter();
        m_st.sub = RS_READER_SCAN;
    }
}

/* Forward declaration */
static void card_setup_emulation(void);

static void on_field_on(void)  { /* reserved for future use */ }
static void on_field_off(void) {
    /* Real reader's session ended (CARD CU signalled field drop via BLE).
     * Mark real card session as stale — the next relay frame will re-select
     * the real card via WUPA+SELECT+RATS to establish a fresh T=CL session. */
    if (m_st.role == BLE_RELAY_ROLE_READER && m_st.real_card_found)
        m_st.needs_reselect = true;
}

static void on_rescan_req(void) {
    /* Set flag — actual RC522 scan deferred to on_tick so it runs
     * in main-loop context, not inside BLE dispatch chain. */
    if (m_st.role != BLE_RELAY_ROLE_READER) return;
    if (m_st.sub != RS_READER_READY) return;
    m_st.rescan_pending = true;
    NRF_LOG_INFO("relay: RESCAN_REQ queued");
}

static void on_card_identity(const relay_card_identity_t *id) {
    /* Accept updates when already CARD_READY — card may have changed on
     * the READER side (periodic re-scan). Reinstall relay handler with
     * the new identity so CU1 presents the correct card to the reader. */
    bool already_ready = (m_st.identity_received && m_st.sub == RS_CARD_READY);
    if (m_st.identity_received && !already_ready) return;

    bool changed = already_ready &&
                   (memcmp(&m_st.identity, id, sizeof(*id)) != 0);
    memcpy(&m_st.identity, id, sizeof(*id));
    if (m_st.identity.uid_len != 4 && m_st.identity.uid_len != 7)
        m_st.identity.uid_len = 4;
    m_st.identity_received = true;
    m_st.reader_sent_ready = true;

    /* If card changed while already relaying, reinstall handler immediately */
    if (changed) {
        card_setup_emulation();
        NRF_LOG_INFO("relay card: identity updated UID=%02X%02X%02X%02X",
                     id->uid[0], id->uid[1], id->uid[2], id->uid[3]);
    } else {
        NRF_LOG_INFO("relay: identity UID=%02X%02X%02X%02X",
                     id->uid[0], id->uid[1], id->uid[2], id->uid[3]);
    }
}

static void on_ready(void) {
    if (m_st.role == BLE_RELAY_ROLE_CARD) {
        m_st.reader_sent_ready = true;
        if (m_st.identity_received) {
            m_st.sub = RS_CARD_READY;
            standalone_feedback(SL_FB_ARMED);
        }
    }
}

/* Response from real card (CU2 → CU1 via BLE) */
static void on_response(const uint8_t *data, uint16_t bits) {
    if (m_st.sub != RS_CARD_AWAIT_RESPONSE) return;
    uint16_t bytes = (bits + 7) / 8;
    if (bytes > sizeof(m_st.response_buf)) bytes = sizeof(m_st.response_buf);
    memcpy(m_st.response_buf, data, bytes);
    m_st.response_bits  = bits;
    m_st.no_response    = false;
    __DMB();
    m_st.response_ready = true;
}

static void on_no_response(void) {
    if (m_st.sub != RS_CARD_AWAIT_RESPONSE) return;
    m_st.no_response    = true;
    m_st.response_bits  = 0;
    m_st.response_ready = true;
}

/* Frame from CU1 to forward to real card (CU2 side) */
static void on_frame(const uint8_t *data, uint16_t bits) {
    if (m_st.reader_frame_pending) return;
    uint16_t bytes = (bits + 7) / 8;
    if (bytes > sizeof(m_st.reader_frame_buf))
        bytes = sizeof(m_st.reader_frame_buf);
    memcpy(m_st.reader_frame_buf, data, bytes);
    m_st.reader_frame_bits    = bits;
    __DMB();
    m_st.reader_frame_pending = true;
}

static void on_disconnected(void) {
    NRF_LOG_INFO("relay: peer disconnected");
    pcd_14a_reader_antenna_off();   /* de-power real card on disconnect */
    result_save_session(RELAY_SESSION_DISCONNECT);
    m_trace_len         = 0;   /* prevent on_exit double-save */
    m_trace_frame_count = 0;
    m_st.sub               = RS_LINKING;
    m_st.identity_received = false;
    m_st.reader_sent_ready = false;
    m_st.frame_pending     = false;
    m_st.response_ready    = false;
    m_st.reader_frame_pending = false;
    m_st.real_card_found   = false;  /* allow fresh card scan on reconnect */
    m_st.rescan_pending    = false;
    m_st.needs_reselect    = false;
    memset(&m_st.real_card, 0, sizeof(m_st.real_card));
    m_st.link_start_ticks  = app_timer_cnt_get();
    sleep_timer_stop();
    standalone_feedback(SL_FB_ERROR);
    ble_relay_start();
}

/* -------------------------------------------------------------------------
 * RELAY_READER: scan real card and send identity to CU1
 * ------------------------------------------------------------------------- */
static void reader_setup_card(void) {
    /* Keep sleep timer reset during scanning — BLE_GAP_EVT_DISCONNECTED
     * (NUS/phone disconnect) starts a 4s sleep timer. Each RC522 scan
     * blocks up to 500ms; multiple retries can exceed 4s total without
     * on_tick running, firing the sleep timer and putting us to sleep. */
    sleep_timer_stop();
    if (!m_st.real_card_found) {
        pcd_14a_reader_reset();
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
        uint8_t status = pcd_14a_reader_scan_auto(&m_st.real_card);
        if (status != STATUS_HF_TAG_OK) {
            pcd_14a_reader_antenna_off();
            return;
        }
        m_st.real_card_found = true;
        NRF_LOG_INFO("relay reader: card UID=%02X%02X%02X%02X",
                     m_st.real_card.uid[0], m_st.real_card.uid[1],
                     m_st.real_card.uid[2], m_st.real_card.uid[3]);
    }

    relay_card_identity_t id;
    memset(&id, 0, sizeof(id));
    id.atqa[0] = m_st.real_card.atqa[0];
    id.atqa[1] = m_st.real_card.atqa[1];
    id.sak     = m_st.real_card.sak;
    uint8_t cascade = m_st.real_card.cascade ? m_st.real_card.cascade : 1;
    id.uid_len = cascade == 1 ? 4 : 7;
    if (id.uid_len == 4) {
#ifndef PROJECT_CHAMELEON_LITE
        get_4byte_tag_uid(&m_st.real_card, id.uid);
#else
        memcpy(id.uid, m_st.real_card.uid, 4);
#endif
    } else {
        memcpy(id.uid, m_st.real_card.uid, 7);
    }

    /* ATS for ISO14443-4 cards — scan_auto already sent RATS and stored
     * the result in m_st.real_card.ats / m_st.real_card.ats_len.
     * Card is now in T=CL ACTIVE and ready for I-blocks — do NOT HALT. */
    if (m_st.real_card.sak & 0x20) {
        id.ats_len = m_st.real_card.ats_len < sizeof(id.ats)
                     ? m_st.real_card.ats_len : sizeof(id.ats);
        memcpy(id.ats, m_st.real_card.ats, id.ats_len);
    }

    memcpy(&m_st.identity, &id, sizeof(id));
    /* Keep antenna ON — card must remain powered throughout the relay session.
     * Antenna will be turned off only on disconnect or disarm. */
    ble_relay_send_card_identity(&id);
    /* Cache in m_st.identity so the 1s re-broadcast in RS_READER_READY
     * keeps sending it — CARD CU may miss the first packet. */
    memcpy(&m_st.identity, &id, sizeof(id));
    m_st.identity_received = true;

    m_st.sub = RS_READER_READY;
    /* READER stays in fast scan — must remain responsive to relay frames
     * and RESCAN_REQ from CARD. Only CARD CU uses slow mode. */
    standalone_led_set_mode_color(STANDALONE_MODE_RELAY, RGB_GREEN);
    standalone_led_solid();
    standalone_feedback(SL_FB_ARMED);
    NRF_LOG_INFO("relay reader: identity sent");
}

/* -------------------------------------------------------------------------
 * RELAY_READER: forward frame to real card, return response to CU1
 * ------------------------------------------------------------------------- */
static void reader_relay_frame(const uint8_t *data, uint16_t bits) {
    /* Re-select the real card if the previous T=CL session ended
     * (field drop, S(DESELECT), or first frame after rescan).
     * scan_auto does WUPA+SELECT+RATS in one call — use its result directly.
     * Do NOT call pcd_14a_reader_ats_request after scan_auto: card is already
     * in T=CL ACTIVE and a second RATS will NAK, aborting the session. */
    if (m_st.needs_reselect && (m_st.real_card.sak & 0x20)) {
        m_st.needs_reselect = false;
        picc_14a_tag_t fresh;
        pcd_14a_reader_reset();
        bsp_delay_ms(5);
        if (pcd_14a_reader_scan_auto(&fresh) == STATUS_HF_TAG_OK) {
            /* Update cached ATS if it changed (rare, but safe) */
            uint8_t al = fresh.ats_len < sizeof(m_st.identity.ats)
                         ? fresh.ats_len : sizeof(m_st.identity.ats);
            if (al > 0 && (al != m_st.identity.ats_len ||
                memcmp(fresh.ats, m_st.identity.ats, al) != 0)) {
                m_st.identity.ats_len = al;
                memcpy(m_st.identity.ats, fresh.ats, al);
                ble_relay_send_card_identity(&m_st.identity);
            }
            /* Card is now in T=CL ACTIVE — fall through to forward I-block */
        } else {
            ble_relay_send_no_response();
            return;
        }
    }

    uint8_t  rx_buf[256];
    uint16_t rx_bits = 0;
    uint8_t  tx_buf[256];
    uint16_t tx_bytes = (bits + 7) / 8;
    if (tx_bytes > sizeof(tx_buf)) tx_bytes = sizeof(tx_buf);
    memcpy(tx_buf, data, tx_bytes);

    /* Antenna stays on — card is kept powered for the duration of the session */
    uint8_t status = pcd_14a_reader_bytes_transfer(
        PCD_TRANSCEIVE, tx_buf, tx_bytes, rx_buf, &rx_bits,
        sizeof(rx_buf) * 8);

    if (status == STATUS_HF_TAG_OK && rx_bits > 0) {
        /* Propagate S(DESELECT) response — card returns to HALT state,
         * set needs_reselect so the next frame triggers a fresh re-select. */
        if (tx_bytes >= 1 && (tx_buf[0] & 0xF7) == 0xC2) {
            m_st.needs_reselect = true;
        }
        ble_relay_send_response(rx_buf, rx_bits);
    } else {
        ble_relay_send_no_response();
    }
}

/* -------------------------------------------------------------------------
 * RELAY_CARD NFCT setup
 * ------------------------------------------------------------------------- */
static void card_setup_emulation(void) {
    if (!m_st.identity_received) return;
    /* Install slot-independent relay handler — works for any HF tag type */
    nfc_relay_tag_install(m_st.identity.uid, m_st.identity.uid_len,
                          m_st.identity.atqa, m_st.identity.sak,
                          m_st.identity.ats, m_st.identity.ats_len);
    nfc_relay_tag_set_frame_cb(on_frame_isr);
    NRF_LOG_INFO("relay card: relay handler installed");
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */
static const ble_relay_callbacks_t k_relay_cbs = {
    .on_connected     = on_connected,
    .on_card_identity = on_card_identity,
    .on_rescan_req    = on_rescan_req,
    .on_field_on      = on_field_on,
    .on_field_off     = on_field_off,
    .on_preauth       = NULL,
    .on_ready         = on_ready,
    .on_frame         = on_frame,
    .on_response      = on_response,
    .on_no_response   = on_no_response,
    .on_disconnected  = on_disconnected,
};

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    size_t  saved_write   = m_st.result_write;
    size_t  saved_read    = m_st.result_read;
    uint8_t saved_count   = m_st.session_count;
    bool    saved_loaded  = m_st.result_loaded;

    memset(&m_st, 0, sizeof(m_st));
    m_trace_len         = 0;
    m_trace_frame_count = 0;

    m_st.result_write  = saved_write;
    m_st.result_read   = saved_read;
    m_st.session_count = saved_count;
    m_st.result_loaded = saved_loaded;

    m_st.active           = true;
    g_is_standalone_armed = true;
    memset(&m_st.identity, 0, sizeof(m_st.identity));  /* clear stale identity from prev arm */
    m_st.wtx_ms  = RELAY_DEFAULT_WTX_MS;
    m_st.sub     = RS_LINKING;

    if (cfg != NULL && cfg_len >= 4) {
        m_st.wtx_ms = (uint32_t)cfg[0] | ((uint32_t)cfg[1] << 8)
                    | ((uint32_t)cfg[2] << 16) | ((uint32_t)cfg[3] << 24);
    }

    m_st.link_start_ticks = app_timer_cnt_get();
    sleep_timer_stop();

    static bool s_relay_initialized = false;
    if (!s_relay_initialized) {
        ble_relay_init(&k_relay_cbs);
        s_relay_initialized = true;
    }
    ble_relay_start();
    NRF_LOG_INFO("relay: armed WTX=%ums", m_st.wtx_ms);
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_exit(void) {
    /* Sanity: flash LEDs immediately to confirm on_exit was called */
    standalone_feedback(SL_FB_SUCCESS);

    /* Stop relay hardware */
    m_st.active           = false;
    g_is_standalone_armed = false;
    nfc_relay_tag_clear();
    ble_relay_stop();
    pcd_14a_reader_antenna_off();

    /* Save result — must happen before tag_mode_enter() which
     * re-initialises NFCT and can briefly interrupt USB on Windows */
    result_save_session(RELAY_SESSION_OK);

    /* NOTE: tag_mode_enter() is intentionally omitted here.
     * nfc_relay_tag_clear() already installed a null handler so the
     * NFCT hardware is idle. The slot handler will be re-registered
     * naturally on next mode arm or boot. */
    sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
    NRF_LOG_INFO("relay: disarmed");
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_tick(uint32_t now_ticks) {
    if (!m_st.active) return STANDALONE_RC_OK;

    ble_relay_process();
    sleep_timer_stop();

    /* Keep scan + HELLO alive every 2s */
    static uint32_t s_last_scan_restart = 0;
    if (ble_relay_get_state() != BLE_RELAY_STATE_IDLE &&
        app_timer_cnt_diff_compute(now_ticks, s_last_scan_restart)
            >= APP_TIMER_TICKS(2000)) {
        s_last_scan_restart = now_ticks;
        ble_relay_restart_scan();
        if (m_st.sub == RS_LINKING) ble_relay_broadcast_hello();
    }

    switch (m_st.sub) {

    case RS_LINKING: {
        static uint32_t s_last_pulse = 0;
        if (app_timer_cnt_diff_compute(now_ticks, s_last_pulse)
                >= APP_TIMER_TICKS(1000)) {
            s_last_pulse = now_ticks;
            standalone_feedback(SL_FB_BUSY_START);
        }
        if (app_timer_cnt_diff_compute(now_ticks, m_st.link_start_ticks)
                >= APP_TIMER_TICKS(RELAY_LINK_TIMEOUT_MS)) {
            m_st.sub = RS_ERROR;
            standalone_feedback(SL_FB_ERROR);
        }
        break;
    }

    case RS_CARD_AWAIT_IDENTITY:
        if (m_st.identity_received) {
            card_setup_emulation();
            if (m_st.reader_sent_ready) {
                m_st.sub = RS_CARD_READY;
                ble_relay_set_fast_mode(true);  /* max scan rate for relay latency */
                standalone_feedback(SL_FB_ARMED);
            }
        }
        break;

    case RS_CARD_READY:
        /* Periodic autosave — guard s_last_autosave==0 so it doesn't
         * fire immediately on first tick (diff from 0 >> 15s at boot). */
        {
            static uint32_t s_last_autosave = 0;
            if (s_last_autosave == 0) s_last_autosave = now_ticks;
            if (m_st.was_connected &&
                app_timer_cnt_diff_compute(now_ticks, s_last_autosave)
                    >= APP_TIMER_TICKS(15000)) {
                s_last_autosave = now_ticks;
                result_flush_session(RELAY_SESSION_OK);
            }
        }
        /* Frame ISR set frame_pending when reader sends a command */
        if (m_st.frame_pending) {
            __DMB();  /* ensure frame_buf/frame_bits writes (ISR) are visible
                       * before we read them — pairs with the ISR's flag set */
            m_st.frame_pending = false;

            /* Log reader→tag frame in trace */
            trace_append(false, m_st.frame_buf, m_st.frame_bits);

            /* NOTE: We deliberately do NOT send S(WTX) here.
             *
             * Transmitting WTX via nfc_tag_14a_tx_bytes() closes the NFCT
             * response slot for the current command — once WTX goes out the
             * hardware returns to RX and the real relayed response can no
             * longer be transmitted against the original command frame.
             *
             * Instead, nfc_relay_tag_inject_response() widens FRAMEDELAYMAX to
             * its maximum (~77ms) so the relayed response can be transmitted
             * late, after the BLE round-trip completes, against the original
             * command. This covers the relay latency without WTX choreography.
             *
             * A real reader with a tight FWT that genuinely requires WTX would
             * need the WTX-ACK handshake handled in the NFCT ISR (respond to
             * the reader's S(WTX) ACK frame), which is a future enhancement. */

            /* Forward frame to RELAY_READER via BLE */
            ble_relay_send_frame(m_st.frame_buf, m_st.frame_bits);
            m_st.sub              = RS_CARD_AWAIT_RESPONSE;
            m_st.frame_sent_ticks = now_ticks;
        }
        /* Detect reader field drop: 2s after last frame, signal CU2
         * to scan for a new card so CU1 is ready for next presentation. */
        {
            static uint32_t s_last_frame_tick = 0;
            static bool     s_had_frames      = false;
            static bool     s_rescan_sent     = false;
            if (m_st.frame_pending || m_st.sub == RS_CARD_AWAIT_RESPONSE) {
                s_last_frame_tick = now_ticks;
                s_had_frames      = true;
                s_rescan_sent     = false;
            }
            if (s_had_frames && !s_rescan_sent &&
                app_timer_cnt_diff_compute(now_ticks, s_last_frame_tick)
                    >= APP_TIMER_TICKS(2000)) {
                s_rescan_sent = true;
                ble_relay_send_rescan_req();
                NRF_LOG_INFO("relay card: reader left, RESCAN_REQ sent");
            }
        }

        break;

    case RS_CARD_AWAIT_RESPONSE:
        if (m_st.response_ready) {
            __DMB();  /* response_buf/bits written in BLE callback — order after flag */
            m_st.response_ready = false;
            m_st.sub            = RS_CARD_READY;

            if (!m_st.no_response && m_st.response_bits > 0) {
                /* Log tag→reader response in trace */
                trace_append(true, m_st.response_buf, m_st.response_bits);
                /* Inject response into NFCT for transmission to reader */
                nfc_relay_tag_inject_response(m_st.response_buf,
                                             m_st.response_bits);
            } else {
                nfc_relay_tag_no_response();
                NRF_LOG_INFO("relay card: no response from real card");
            }
        } else {
            /* Timeout */
            uint32_t wait = app_timer_cnt_diff_compute(now_ticks,
                                                        m_st.frame_sent_ticks);
            if (wait >= APP_TIMER_TICKS(RELAY_FRAME_TIMEOUT_MS)) {
                NRF_LOG_WARNING("relay card: response timeout");
                nfc_relay_tag_no_response();
                m_st.sub = RS_CARD_READY;
            }
        }
        break;

    case RS_READER_SCAN: {
        static uint32_t s_last_card_scan = 0;
        if (app_timer_cnt_diff_compute(now_ticks, s_last_card_scan)
                >= APP_TIMER_TICKS(500)) {
            s_last_card_scan = now_ticks;
            sleep_timer_stop();
            reader_setup_card();
            sleep_timer_stop();
        }
        break;
    }

    case RS_READER_READY: {
        /* Periodic autosave — guard s_last_autosave_r==0 so it doesn't
         * fire immediately on first tick (diff from 0 >> 15s at boot). */
        {
            static uint32_t s_last_autosave_r = 0;
            if (s_last_autosave_r == 0) s_last_autosave_r = now_ticks;
            if (m_st.was_connected &&
                app_timer_cnt_diff_compute(now_ticks, s_last_autosave_r)
                    >= APP_TIMER_TICKS(15000)) {
                s_last_autosave_r = now_ticks;
                result_flush_session(RELAY_SESSION_OK);
            }
        }
        /* Re-broadcast identity every 1s */
        static uint32_t s_last_bcast = 0;
        if (app_timer_cnt_diff_compute(now_ticks, s_last_bcast)
                >= APP_TIMER_TICKS(1000)) {
            s_last_bcast = now_ticks;
            ble_relay_send_card_identity(&m_st.identity);
        }
        /* Card change detection: scan when RESCAN_REQ received OR every 5s
         * when idle (no relay frames for 3s). Single RESCAN_REQ scan misses
         * the new card if it isn't in the RC522 field at that exact moment;
         * the periodic scan catches it on the next 5s window. */
        {
            static uint32_t s_last_frame_seen  = 0;
            static uint32_t s_last_card_check  = 0;
            if (s_last_card_check == 0) s_last_card_check = now_ticks;

            if (m_st.reader_frame_pending) s_last_frame_seen = now_ticks;

            bool reader_idle = (s_last_frame_seen == 0 ||
                app_timer_cnt_diff_compute(now_ticks, s_last_frame_seen)
                    >= APP_TIMER_TICKS(3000));

            bool do_scan = m_st.rescan_pending ||
                (reader_idle &&
                 app_timer_cnt_diff_compute(now_ticks, s_last_card_check)
                     >= APP_TIMER_TICKS(5000));

            if (do_scan) {
                m_st.rescan_pending = false;
                s_last_card_check   = now_ticks;
                sleep_timer_stop();
                set_scan_tag_timeout(200);
                picc_14a_tag_t new_card;
                memset(&new_card, 0, sizeof(new_card));
                if (pcd_14a_reader_scan_auto(&new_card) == STATUS_HF_TAG_OK) {
                    /* scan_auto already does RATS internally for SAK & 0x20 cards
                     * and stores the result in new_card.ats / new_card.ats_len.
                     * Do NOT call pcd_14a_reader_ats_request again — card is already
                     * in T=CL ACTIVE state and a second RATS request will NAK. */

                    bool changed;
                    if (new_card.sak & 0x20) {
                        /* DeSFire/ISO14443-4: compare ATQA+SAK only — UID is random */
                        changed = (new_card.atqa[0] != m_st.real_card.atqa[0] ||
                                   new_card.atqa[1] != m_st.real_card.atqa[1] ||
                                   new_card.sak     != m_st.real_card.sak);
                    } else {
                        changed = (memcmp(new_card.uid, m_st.real_card.uid,
                                          sizeof(new_card.uid)) != 0);
                    }

                    if (changed) {
                        m_st.real_card       = new_card;
                        m_st.real_card_found = true;
                        relay_card_identity_t id;
                        memset(&id, 0, sizeof(id));
                        id.atqa[0] = new_card.atqa[0];
                        id.atqa[1] = new_card.atqa[1];
                        id.sak     = new_card.sak;
                        id.uid_len = new_card.uid_len;
                        memcpy(id.uid, new_card.uid, new_card.uid_len);
                        id.ats_len = new_card.ats_len < sizeof(id.ats)
                                     ? new_card.ats_len : sizeof(id.ats);
                        memcpy(id.ats, new_card.ats, id.ats_len);
                        ble_relay_send_card_identity(&id);
                        memcpy(&m_st.identity, &id, sizeof(id));
                        m_st.identity_received = true;
                        m_st.needs_reselect    = false;
                        NRF_LOG_INFO("relay reader: card changed, new identity sent");
                    }
                    /* Unchanged card: keep m_st.identity as-is — preserves ATS so
                     * CARD CU continues responding to RATS correctly. */
                    if (new_card.sak & 0x20) {
                        m_st.needs_reselect = true; /* scan_auto did RATS; re-select before next relay frame */
                    }
                }
            }
        }
        /* Forward frames from CU1 to real card */
        if (m_st.reader_frame_pending) {
            __DMB();  /* reader_frame_buf/bits written in BLE callback — order after flag */
            m_st.reader_frame_pending = false;
            m_st.sub = RS_READER_RELAY;
            reader_relay_frame(m_st.reader_frame_buf, m_st.reader_frame_bits);
            m_st.sub = RS_READER_READY;
        }
        break;
    }

    case RS_READER_RELAY:
        /* handled synchronously in reader_relay_frame */
        break;

    case RS_ERROR:
        break;

    default:
        break;
    }

    return STANDALONE_RC_OK;
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (evt == STANDALONE_BTN_BOTH_VLONG) {
        memset(m_result_words, 0, sizeof(m_result_words));
        m_st.result_write  = 0;
        m_st.result_read   = 0;
        m_st.session_count = 0;
        m_st.result_loaded = true;
        m_trace_len         = 0;
        m_trace_frame_count = 0;
        app_standalone_save_result_buf(STANDALONE_MODE_RELAY, NULL, 0);
        ble_relay_stop();
        m_st.sub               = RS_LINKING;
            m_st.identity_received = false;
        m_st.link_start_ticks  = app_timer_cnt_get();
        ble_relay_start();
        standalone_feedback(SL_FB_SUCCESS);
    }
    return STANDALONE_RC_OK;
}

/* -------------------------------------------------------------------------
 * Result interface
 * ------------------------------------------------------------------------- */
static size_t get_result_size(void) { return m_st.result_write; }

static standalone_rc_t read_result(uint8_t *out, size_t out_max, size_t *out_len) {
    if (!out || !out_len) return STANDALONE_RC_INVALID_CFG;
    result_ensure_loaded();
    if (m_st.result_read >= m_st.result_write) {
        m_st.result_read = 0;
        *out_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }
    size_t remaining = m_st.result_write - m_st.result_read;
    size_t take      = (remaining < out_max) ? remaining : out_max;
    memcpy(out, &m_result_buf[m_st.result_read], take);
    m_st.result_read += take;
    *out_len = take;
    return STANDALONE_RC_OK;
}

static void clear_result(void) {
    memset(m_result_words, 0, sizeof(m_result_words));
    m_st.result_write   = 0;
    m_st.result_read    = 0;
    m_st.session_count  = 0;
    m_st.result_loaded  = true;
    m_trace_len         = 0;
    m_trace_frame_count = 0;
    app_standalone_save_result_buf(STANDALONE_MODE_RELAY, NULL, 0);
}

static void ensure_loaded(void) { result_ensure_loaded(); }

/* -------------------------------------------------------------------------
 * Diagnostic accessor (CMD 7008)
 * ------------------------------------------------------------------------- */
void mode_relay_get_diag(uint8_t *out_sub, uint8_t *out_card_found,
                         uint8_t *out_identity_rx,
                         uint8_t *out_uid, uint8_t *out_uid_len) {
    if (out_sub)         *out_sub         = (uint8_t)m_st.sub;
    if (out_card_found)  *out_card_found  = m_st.real_card_found  ? 1 : 0;
    if (out_identity_rx) *out_identity_rx = m_st.identity_received ? 1 : 0;
    if (out_uid && out_uid_len) {
        if (m_st.real_card_found) {
#ifndef PROJECT_CHAMELEON_LITE
            get_4byte_tag_uid(&m_st.real_card, out_uid);
#else
            memcpy(out_uid, m_st.real_card.uid, 4);
#endif
            *out_uid_len = 4;
        } else if (m_st.identity_received) {
            *out_uid_len = m_st.identity.uid_len;
            memcpy(out_uid, m_st.identity.uid, m_st.identity.uid_len);
        } else {
            *out_uid_len = 0;
        }
    }
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */
const standalone_mode_iface_t mode_relay_iface = {
    .id              = STANDALONE_MODE_RELAY,
    .name            = "relay",
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
    .ensure_loaded   = ensure_loaded,
};
