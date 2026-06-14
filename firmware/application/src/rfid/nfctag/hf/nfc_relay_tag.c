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

    /* Consume S(WTX) ACK locally — do NOT relay to real card.
     * WTX is a local handshake between CARD CU and the real reader;
     * forwarding WTX ACK to the real card confuses its session state. */
    if ((data[0] & 0xF7) == 0xF2) return;

    /* Forward all other frames (I-blocks, S(DESELECT), etc.) via callback. */
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
    s_awaiting_response = false;

    /* Extend the NFCT frame-delay window to its maximum (0xFFFFF ticks ≈ 77ms
     * at 13.56 MHz) so this single relayed response can be transmitted even
     * though the BLE round-trip took far longer than the default ~4.8ms
     * window. The window is restored to default immediately afterward by the
     * TX-done path / next anticollision so rapid UID-only polling is not
     * affected. */
    nfc_tag_14a_set_frame_delay_max(0xFFFFFUL);

    /* Transmit raw bytes as-is. The real card's response already contains
     * CRC bytes as returned by RC522 — do NOT append CRC again. */
    uint16_t bytes = (bit_count + 7) / 8;
    nfc_tag_14a_tx_bytes((uint8_t *)data, bytes, false);

    /* Restore the default response window so the next command (e.g. a fresh
     * WUPA/anticollision from a UID-only reader doing rapid polling) is
     * answered with normal fast timing rather than the wide relay window. */
    nfc_tag_14a_set_frame_delay_max(65535UL);

    NRF_LOG_DEBUG("relay_tag: injected %u bits", bit_count);
}

void nfc_relay_tag_no_response(void) {
    s_awaiting_response = false;
}

void nfc_relay_tag_clear(void) {
    /* Disable callbacks first */
    s_active            = false;
    s_awaiting_response = false;
    s_frame_cb          = NULL;

    /* Force the 14A state machine back to IDLE so the READY→SELECT path
     * never fires with a NULL auto_coll_res (nfc_14a.c dereferences
     * auto_coll_res->size without a NULL check when in NFC_TAG_STATE_14A_READY). */
    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);

    NRF_LOG_INFO("relay_tag: cleared");
}
