/* Portable CU framing adapter for the existing generic SL1 core.
 * This is not an NFCT, RATS, slot, persistence, or USB implementation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable AES/CardAuth state, independent of the NFC peripheral. */
#include "sl1_cardauth.h"

#define CU_SL1_KEY_SIZE 16U
#define CU_SL1_MAX_REPLY_SIZE 17U
#define CU_SL1_MAX_RX_SIZE 35U

typedef struct {
    Sl1Core core;
    bool enabled;
} CuSl1Adapter;

typedef struct {
    uint8_t data[CU_SL1_MAX_REPLY_SIZE];
    size_t size;
    size_t bits;
    /* A 17-byte reply needs hardware CRC/parity. A 4-bit NAK needs neither. */
    bool append_crc;
} CuSl1Reply;

/* Call init before use. The context begins disabled without a default key.
 * All functions are synchronous; the owner must serialize access to a context.
 */
void cu_sl1_adapter_init(CuSl1Adapter *adapter);

/* Configuration is explicit and fail-closed: an invalid key disables the old
 * configuration. A valid replacement resets counters and pending state.
 * The key is copied; the caller retains responsibility for its input buffer.
 */
bool cu_sl1_adapter_configure(CuSl1Adapter *adapter, const uint8_t *key, size_t key_size);
bool cu_sl1_adapter_enabled(const CuSl1Adapter *adapter);

/* Field/HALT/deselect/reset boundary: discard pending challenge, keep the key.
 * Slot unload/feature disable: call disable to erase key and session material.
 */
void cu_sl1_adapter_reset(CuSl1Adapter *adapter);
void cu_sl1_adapter_disable(CuSl1Adapter *adapter);

/* RX is CU data after parity removal, with the two CRC-A bytes still present.
 * plaintext_allowed MUST be false while native Classic is pending/authenticated;
 * in that case the adapter does not inspect ciphertext and returns false.
 *
 * true means this adapter consumed the frame; bits==0 means a silent refusal.
 * false leaves the frame to the native handler, with an empty reply. Unrelated
 * plaintext, including another AES key reference, cancels stale AES pending state.
 * Bad CRC/length/bits in a recognized 76/72 family are silently consumed/reset.
 *
 * Fresh 16-byte entropy is supplied by the caller for each 76/9004 start. Missing
 * or incorrectly sized entropy refuses that start. No RNG, blocking, allocation,
 * logging, or FDS operation is performed here. Entropy quality is a caller contract.
 * reply may not alias adapter/rx/entropy; rx and entropy remain unchanged.
 */
bool cu_sl1_adapter_receive(
    CuSl1Adapter *adapter,
    const uint8_t *rx,
    size_t rx_size,
    size_t rx_bits,
    bool plaintext_allowed,
    const uint8_t *entropy,
    size_t entropy_size,
    CuSl1Reply *reply);
