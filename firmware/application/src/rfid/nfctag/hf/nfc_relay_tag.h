/*
 * nfc_relay_tag.h
 *
 * Slot-independent 14A relay tag handler.
 *
 * Registers a minimal nfc_tag_14a_handler_t that:
 *   - Presents the real card's UID/ATQA/SAK for anticollision/SELECT
 *   - Forwards every post-SELECT frame to a callback (mode_relay.c)
 *   - Allows arbitrary response injection via nfc_relay_tag_inject_response()
 *
 * Works for ALL HF tag types — the relay is completely transparent to
 * the underlying protocol. No active slot required.
 */

#ifndef NFC_RELAY_TAG_H
#define NFC_RELAY_TAG_H

#include <stdint.h>
#include <stdbool.h>
#include "nfc_14a.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install the relay handler and set the card identity to present.
 *  Safe to call at any time; replaces whatever handler is currently active.
 *  Saves the previous handler so it can be restored by nfc_relay_tag_clear(). */
void nfc_relay_tag_install(const uint8_t *uid, uint8_t uid_len,
                           const uint8_t atqa[2], uint8_t sak,
                           const uint8_t *ats, uint8_t ats_len);

/** Register the frame callback (ISR-safe: copy + flag only).
 *  Called once per frame received from the reader after SELECT. */
void nfc_relay_tag_set_frame_cb(void (*cb)(const uint8_t *data, uint16_t bits));
/* Returns true once after a new RATS (new T=CL session) has reset the relay
 * tag state; clears the flag on read. mode_relay uses this to force its own
 * sub-state back to READY between auth-trace runs. */
bool nfc_relay_tag_take_session_reset(void);

/** Inject a raw response to be transmitted to the reader.
 *  Call from main-loop context after receiving the response from RELAY_READER.
 *  bit_count is the exact bit length; data is raw bytes. */
void nfc_relay_tag_inject_response(const uint8_t *data, uint16_t bit_count);

/** Signal that no response is coming (RELAY_READER reported no card response). */
void nfc_relay_tag_no_response(void);

/** Remove the relay handler and restore the previous tag handler.
 *  Call on disarm. */
void nfc_relay_tag_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_RELAY_TAG_H */
