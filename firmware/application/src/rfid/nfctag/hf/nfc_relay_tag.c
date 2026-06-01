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

/* Frame callback (set by mode_relay.c) */
static void (*s_frame_cb)(const uint8_t *, uint16_t) = NULL;

/* Awaiting response flag (set in cb_state, cleared in inject_response) */
static volatile bool s_awaiting_response = false;

/* -------------------------------------------------------------------------
 * coll_res getter — called by nfc_14a.c ISR on every REQA/WUPA
 * ------------------------------------------------------------------------- */
static nfc_tag_14a_coll_res_reference_t s_coll_res_ref;

static nfc_tag_14a_coll_res_reference_t *relay_get_coll_res(void) {
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
    /* Ignore very short frames (Gen1A magic commands etc.) */
    if (szBits <= 8) return;

    /* Forward every frame to mode_relay.c via the registered callback.
     * This is ISR context — callback MUST only copy + set a flag. */
    if (s_frame_cb) {
        s_frame_cb(data, szBits);
        s_awaiting_response = true;
    }
}

/* -------------------------------------------------------------------------
 * Reset handler — field dropped; reset state
 * ------------------------------------------------------------------------- */
static void relay_cb_reset(void) {
    s_awaiting_response = false;
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
    s_frame_cb          = NULL;

    /* Install relay handler — nfc_tag_14a_set_handler just copies ptrs,
     * no teardown, safe to call at any time */
    nfc_tag_14a_set_handler(&s_relay_handler);

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
    s_awaiting_response = false;

    /* Transmit raw bytes as-is. The real card's response already contains
     * CRC bytes as returned by RC522 — do NOT append CRC again. */
    uint16_t bytes = (bit_count + 7) / 8;
    nfc_tag_14a_tx_bytes((uint8_t *)data, bytes, false);

    NRF_LOG_DEBUG("relay_tag: injected %u bits", bit_count);
}

void nfc_relay_tag_no_response(void) {
    s_awaiting_response = false;
}

void nfc_relay_tag_clear(void) {
    s_awaiting_response = false;
    s_frame_cb          = NULL;

    /* Restore zero handler — slot activation will re-register the correct
     * handler the next time the user activates a slot or disarms relay. */
    nfc_tag_14a_handler_t null_handler = {0};
    nfc_tag_14a_set_handler(&null_handler);

    NRF_LOG_INFO("relay_tag: cleared");
}
