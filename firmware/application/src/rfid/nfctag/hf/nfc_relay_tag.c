/*
 * nfc_relay_tag.c
 *
 * Slot-independent 14A relay tag handler.
 *
 * Registers directly with nfc_tag_14a_set_handler(), bypassing the slot
 * system entirely.  Works for any HF tag type — the relay is a transparent
 * pipe; protocol details are handled end-to-end by the real reader and
 * real card.
 */

#include "nfc_relay_tag.h"
#include "nfc_14a.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define NRF_LOG_MODULE_NAME nfc_relay_tag
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

/* -------------------------------------------------------------------------
 * Relay identity buffers (static — valid for the duration of the session)
 * ------------------------------------------------------------------------- */
static uint8_t               s_uid[7]     = {0};
static uint8_t               s_atqa[2]    = {0};
static uint8_t               s_sak[1]     = {0};
static nfc_tag_14a_uid_size  s_uid_size   = NFC_TAG_14A_UID_SINGLE_SIZE;
static nfc_14a_ats_t         s_ats        = {0};

/* Active flag — set by install, cleared by clear; makes callbacks no-ops safely */
static bool s_active = false;

/* Frame callback (set by mode_relay.c) */
static void (*s_frame_cb)(const uint8_t *, uint16_t) = NULL;

/* Awaiting response flag (set in cb_state, cleared in inject_response) */
static volatile bool s_awaiting_response = false;

/* WTX choreography state.
 *
 * The relay round-trip (~35-80ms) can exceed the NFCT FRAMEDELAYMAX hardware
 * ceiling (~77ms). ISO 14443-4 WTX is the correct remedy: the moment we
 * receive an I-block we send S(WTX) to the reader (within the first window),
 * which resets BOTH the reader's FWT timer and gives us a fresh NFCT window
 * when the reader's S(WTX) ACK arrives. We keep sending WTX on each ACK until
 * the relayed response is ready, then transmit it against the ACK frame.
 *
 * s_response_pending : a relayed response has arrived and is buffered, ready
 *                      to send on the next reader contact (WTX ACK).
 * s_response_buf/len : the buffered response (raw bytes incl. CRC).
 * s_wtx_active       : we are mid-WTX-wait (sent WTX, awaiting ACK). */
static volatile bool s_response_pending = false;
static uint8_t       s_response_buf[256];
static uint16_t      s_response_len = 0;   /* bytes */
static volatile bool s_wtx_active   = false;

/* S(WTX) request frame: PCB 0xF2, WTXM=1 (request minimal extension; the
 * actual extension is the reader's FWT × WTXM). CRC appended by tx path. */
static void relay_send_wtx(void) {
    uint8_t wtx[2] = { 0xF2, 0x01 };
    /* Keep the response window wide so the WTX itself transmits late if the
     * ISR is delayed; reset to default happens in TX_FRAMEEND. */
    nfc_tag_14a_set_frame_delay_max(0xFFFFFUL);
    nfc_tag_14a_tx_bytes(wtx, sizeof(wtx), true);  /* appendCrc=true */
    s_wtx_active = true;
}

static void relay_tx_buffered_response(void) {
    if (s_response_len == 0) return;
    uint16_t bytes = s_response_len;
    if (bytes > MAX_NFC_TX_BUFFER_SIZE) bytes = MAX_NFC_TX_BUFFER_SIZE;
    nfc_tag_14a_set_frame_delay_max(0xFFFFFUL);
    nfc_tag_14a_tx_bytes(s_response_buf, bytes, false);  /* CRC already present */
    s_response_pending  = false;
    s_response_len      = 0;
    s_wtx_active        = false;
    s_awaiting_response = false;
}

/* -------------------------------------------------------------------------
 * coll_res getter — called by nfc_14a.c ISR on every REQA/WUPA
 * ------------------------------------------------------------------------- */
static nfc_tag_14a_coll_res_reference_t s_coll_res_ref;

static nfc_tag_14a_coll_res_reference_t *relay_get_coll_res(void) {
    if (!s_active) return NULL;
    s_coll_res_ref.size = &s_uid_size;
    s_coll_res_ref.uid  = s_uid;
    s_coll_res_ref.atqa = s_atqa;
    s_coll_res_ref.sak  = s_sak;
    s_coll_res_ref.ats  = &s_ats;
    return &s_coll_res_ref;
}

/* -------------------------------------------------------------------------
 * State handler — called by nfc_14a.c ISR for every post-SELECT frame
 * ------------------------------------------------------------------------- */
static void relay_cb_state(uint8_t *data, uint16_t szBits) {
    if (!s_active) return;
    if (szBits <= 8) return;

    /* S(WTX) ACK from the reader (PCB 0xF2, optionally CID/0x08 bit set).
     * This is our cue: the reader granted more time and reset our NFCT window.
     * If the relayed response has arrived, transmit it now against this ACK.
     * Otherwise send another S(WTX) to buy a further window. Never relay the
     * WTX ACK to the real card. */
    if ((data[0] & 0xF7) == 0xF2) {
        if (s_response_pending) {
            relay_tx_buffered_response();
        } else if (s_awaiting_response) {
            relay_send_wtx();   /* still waiting — extend again */
        }
        return;
    }

    /* Normal I-block / S-block from the reader: forward to the real card and
     * immediately request a waiting-time extension so the reader (and our own
     * NFCT) tolerate the relay latency. */
    if (s_frame_cb) {
        s_awaiting_response = true;
        s_response_pending  = false;
        s_response_len      = 0;
        s_frame_cb(data, szBits);
        /* Send S(WTX) right now, within the first NFCT window. The reader will
         * ACK it; by the time the ACK returns the BLE response may be ready. */
        relay_send_wtx();
    }
}

/* Set true by relay_cb_reset (fires on RATS = new T=CL session). mode_relay
 * polls/clears it via nfc_relay_tag_take_session_reset() to force its own
 * sub-state back to READY, so a second auth-trace run isn't blocked by stale
 * RS_CARD_AWAIT_RESPONSE state left from the previous run. */
static volatile bool s_session_reset = false;

/* -------------------------------------------------------------------------
 * Reset handler — field dropped / new RATS; reset state
 * ------------------------------------------------------------------------- */
static void relay_cb_reset(void) {
    s_awaiting_response = false;
    s_response_pending  = false;
    s_response_len      = 0;
    s_wtx_active        = false;
    s_session_reset     = true;
}

bool nfc_relay_tag_take_session_reset(void) {
    bool v = s_session_reset;
    s_session_reset = false;
    return v;
}

/* -------------------------------------------------------------------------
 * Relay handler descriptor
 * ------------------------------------------------------------------------- */
static nfc_tag_14a_handler_t s_relay_handler = {
    .cb_reset    = relay_cb_reset,
    .cb_state    = relay_cb_state,
    .get_coll_res= relay_get_coll_res,
};

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void nfc_relay_tag_install(const uint8_t *uid, uint8_t uid_len,
                           const uint8_t atqa[2], uint8_t sak,
                           const uint8_t *ats, uint8_t ats_len) {
    /* Populate identity buffers */
    uint8_t ulen = uid_len > 7 ? 7 : uid_len;
    memset(s_uid, 0, sizeof(s_uid));
    memcpy(s_uid, uid, ulen);
    memcpy(s_atqa, atqa, 2);
    s_sak[0]  = sak;
    s_uid_size = (uid_len <= 4)
        ? NFC_TAG_14A_UID_SINGLE_SIZE : NFC_TAG_14A_UID_DOUBLE_SIZE;

    memset(&s_ats, 0, sizeof(s_ats));
    if (ats && ats_len > 0) {
        uint8_t al = ats_len > sizeof(s_ats.data) ? sizeof(s_ats.data) : ats_len;
        memcpy(s_ats.data, ats, al);
        s_ats.length = al;
    }

    s_awaiting_response = false;
    s_response_pending  = false;
    s_response_len      = 0;
    s_wtx_active        = false;
    s_frame_cb          = NULL;

    /* Install relay handler — nfc_tag_14a_set_handler just copies ptrs,
     * no teardown, safe to call at any time */
    nfc_tag_14a_set_handler(&s_relay_handler);

    s_active = true;  /* enable callbacks now handler is registered */

    /* Ensure NFCT hardware is running. If the active slot has HF type
     * undefined, tag_emulation_sense_switch_all() calls sense_switch(false)
     * at boot leaving NFCT uninitialised — relay would be silent to readers.
     * sense_switch(false) resets state; sense_switch(true) always calls
     * nrfx_nfct_init() when state is NONE or DISABLE. */
    nfc_tag_14a_sense_switch(false);
    nfc_tag_14a_sense_switch(true);
    NRF_LOG_INFO("relay_tag: installed UID=%02X%02X%02X%02X",
                 s_uid[0], s_uid[1], s_uid[2], s_uid[3]);
    NRF_LOG_INFO("relay_tag: ATQA=%02X%02X SAK=%02X",
                 s_atqa[0], s_atqa[1], s_sak[0]);
}

void nfc_relay_tag_set_frame_cb(void (*cb)(const uint8_t *, uint16_t)) {
    s_frame_cb = cb;
}


void nfc_relay_tag_inject_response(const uint8_t *data, uint16_t bit_count) {
    if (!s_awaiting_response || !data) return;

    uint16_t bytes = (bit_count + 7) / 8;
    if (bytes == 0) return;
    if (bytes > sizeof(s_response_buf)) bytes = sizeof(s_response_buf);

    /* Buffer the relayed response. It is NOT transmitted here — the original
     * command's NFCT window closed when we sent S(WTX). Instead we hold the
     * bytes and transmit them in relay_cb_state() when the reader's next
     * S(WTX) ACK arrives (which opens a fresh window). This is the ISO 14443-4
     * WTX mechanism: WTX request → reader ACK → real response.
     *
     * If a WTX ACK has not yet been seen, s_response_pending tells the ACK
     * handler to transmit immediately on arrival. */
    memcpy(s_response_buf, data, bytes);
    s_response_len     = bytes;
    s_response_pending = true;

    NRF_LOG_DEBUG("relay_tag: response buffered %u bytes (awaiting WTX ACK)", bytes);
}

void nfc_relay_tag_no_response(void) {
    s_awaiting_response = false;
    s_response_pending  = false;
    s_response_len      = 0;
    s_wtx_active        = false;
}

void nfc_relay_tag_clear(void) {
    /* Disable callbacks first */
    s_active            = false;
    s_awaiting_response = false;
    s_response_pending  = false;
    s_response_len      = 0;
    s_wtx_active        = false;
    s_frame_cb          = NULL;

    /* Force the 14A state machine back to IDLE so the READY→SELECT path
     * never fires with a NULL auto_coll_res (nfc_14a.c dereferences
     * auto_coll_res->size without a NULL check when in NFC_TAG_STATE_14A_READY). */
    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);

    NRF_LOG_INFO("relay_tag: cleared");
}
