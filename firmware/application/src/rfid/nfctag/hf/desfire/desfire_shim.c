/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * Firmware-side implementation of the DESFire engine's platform interface:
 * assertions, object allocation, response byte buffers, and randomness.
 *
 * The engine runs inside the NFC interrupt handler, so nothing here may block,
 * allocate from a heap, or call into the SoftDevice.
 */

#include "desfire_shim.h"

#include <stdlib.h>
#include <string.h>

#include "app_error.h"
#include "app_util_platform.h"
#include "nrf_drv_rng.h"

#define NRF_LOG_MODULE_NAME desfire
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
NRF_LOG_MODULE_REGISTER();

#include "dfc_bytebuf.h"
#include "dfc_emulator.h"
#include "dfc_port.h"
#include "dfc_secure_messaging.h"
#include "dfc_virtual_picc.h"

/* ---------------------------------------------------------- assertions ---- */

void dfc_assert_fail(const char* file, int line) {
    NRF_LOG_ERROR("DESFire assertion failed at %s:%d", file, line);
    /* APP_ERROR_HANDLER resets the device, so the message has to be pushed out
     * of the deferred log buffer first or it is lost -- which would defeat the
     * point of logging it at all. */
    NRF_LOG_FINAL_FLUSH();
    APP_ERROR_HANDLER(NRF_ERROR_INTERNAL);
}

/* ---------------------------------------------------------- allocation ---- */

/* One static slot per object kind. The engine never holds two of any kind at
 * once, and dfc_virtual_picc_field_off releases and reacquires the emulator
 * from the NFC interrupt on every field drop, which a heap could not serve
 * safely or in bounded time. */
static DfcEmulator m_emulator;
static DfcSecureMessaging m_secure_messaging;
static DfcVirtualPiccSession m_session;
static bool m_emulator_used;
static bool m_secure_messaging_used;
static bool m_session_used;

void* dfc_platform_alloc(size_t size, DfcAllocTag tag) {
    switch(tag) {
    case DfcAllocEmulator:
        if(m_emulator_used || size > sizeof(m_emulator)) return NULL;
        m_emulator_used = true;
        return &m_emulator;
    case DfcAllocSecureMessaging:
        if(m_secure_messaging_used || size > sizeof(m_secure_messaging)) return NULL;
        m_secure_messaging_used = true;
        return &m_secure_messaging;
    case DfcAllocSession:
        if(m_session_used || size > sizeof(m_session)) return NULL;
        m_session_used = true;
        return &m_session;
    }
    return NULL;
}

void dfc_platform_free(void* ptr) {
    if(ptr == &m_emulator) {
        m_emulator_used = false;
    } else if(ptr == &m_secure_messaging) {
        m_secure_messaging_used = false;
    } else if(ptr == &m_session) {
        m_session_used = false;
    }
}

/* ------------------------------------------------------------ bytebuf ---- */

/* Two are reachable at once: the emulator's own response buffer plus the
 * temporary the virtual PICC layer allocates per native exchange. The third is
 * slack so a leak shows up as a NULL rather than silent reuse. */
#define DESFIRE_BYTEBUF_SLOTS 3

static DfcByteBuf m_bytebufs[DESFIRE_BYTEBUF_SLOTS];
static bool m_bytebuf_used[DESFIRE_BYTEBUF_SLOTS];

DfcByteBuf* dfc_bytebuf_alloc(size_t max_size) {
    if(max_size > DFC_BYTEBUF_MAX) return NULL;
    for(size_t i = 0; i < DESFIRE_BYTEBUF_SLOTS; i++) {
        if(!m_bytebuf_used[i]) {
            m_bytebuf_used[i] = true;
            m_bytebufs[i].size_bytes = 0;
            return &m_bytebufs[i];
        }
    }
    return NULL;
}

void dfc_bytebuf_free(DfcByteBuf* b) {
    for(size_t i = 0; i < DESFIRE_BYTEBUF_SLOTS; i++) {
        if(b == &m_bytebufs[i]) {
            m_bytebuf_used[i] = false;
            return;
        }
    }
}

void dfc_bytebuf_reset(DfcByteBuf* b) {
    b->size_bytes = 0;
}

void dfc_bytebuf_append_bytes(DfcByteBuf* b, const uint8_t* data, size_t len) {
    if(b->size_bytes + len > DFC_BYTEBUF_MAX) return;
    memcpy(b->data + b->size_bytes, data, len);
    b->size_bytes += len;
}

void dfc_bytebuf_append_byte(DfcByteBuf* b, uint8_t byte) {
    if(b->size_bytes + 1 > DFC_BYTEBUF_MAX) return;
    b->data[b->size_bytes++] = byte;
}

size_t dfc_bytebuf_get_size_bytes(const DfcByteBuf* b) {
    return b->size_bytes;
}

const uint8_t* dfc_bytebuf_get_data(const DfcByteBuf* b) {
    return b->data;
}

uint8_t dfc_bytebuf_get_byte(const DfcByteBuf* b, size_t index) {
    return b->data[index];
}

/* ------------------------------------------------------------- random ---- */

/* Reserve drained by the interrupt handler and refilled by the main loop. An
 * authentication consumes at most 16 bytes, so this covers four back-to-back
 * authentications between pumps. */
#define DESFIRE_ENTROPY_SIZE     64
#define DESFIRE_ENTROPY_LOW_MARK 32

static uint8_t m_entropy[DESFIRE_ENTROPY_SIZE];
static size_t m_entropy_len;
static unsigned m_starvations;

void desfire_random_pump(void) {
    uint8_t wanted;
    CRITICAL_REGION_ENTER();
    wanted = (m_entropy_len < DESFIRE_ENTROPY_LOW_MARK) ?
                 (uint8_t)(DESFIRE_ENTROPY_SIZE - m_entropy_len) :
                 0;
    CRITICAL_REGION_EXIT();
    if(wanted == 0) return;

    uint8_t available = 0;
    nrf_drv_rng_bytes_available(&available);
    if(available == 0) return;
    if(available < wanted) wanted = available;

    uint8_t staging[DESFIRE_ENTROPY_SIZE];
    if(nrf_drv_rng_rand(staging, wanted) != NRF_SUCCESS) return;

    CRITICAL_REGION_ENTER();
    if(m_entropy_len + wanted > DESFIRE_ENTROPY_SIZE) {
        wanted = (uint8_t)(DESFIRE_ENTROPY_SIZE - m_entropy_len);
    }
    memcpy(m_entropy + m_entropy_len, staging, wanted);
    m_entropy_len += wanted;
    CRITICAL_REGION_EXIT();
}

bool desfire_random_ready(void) {
    bool ready;
    CRITICAL_REGION_ENTER();
    ready = m_entropy_len >= 16;
    CRITICAL_REGION_EXIT();
    return ready;
}

unsigned desfire_random_starvations(void) {
    return m_starvations;
}

void dfc_random_fill(uint8_t* buf, size_t len) {
    size_t taken = 0;

    CRITICAL_REGION_ENTER();
    if(m_entropy_len > 0) {
        taken = (len < m_entropy_len) ? len : m_entropy_len;
        /* Draw from the tail so the remainder stays contiguous at the front. */
        memcpy(buf, m_entropy + m_entropy_len - taken, taken);
        m_entropy_len -= taken;
    }
    CRITICAL_REGION_EXIT();

    if(taken == len) return;

    /* Reserve ran dry mid-authentication. Fall back to the C PRNG, which
     * app_main seeds from the hardware RNG at boot: weaker than the reserve but
     * never constant or all-zero, which is what matters -- DESFire mutual
     * authentication rests on RndB being unpredictable to the reader. The
     * counter makes the shortfall visible rather than silent. */
    m_starvations++;
    for(size_t i = taken; i < len; i++) {
        buf[i] = (uint8_t)(rand() & 0xFF);
    }
}

/* ------------------------------------------------- configuration guards ---- */

/* The static slots above must match what the engine actually asks for; a
 * capacity change that outgrew them would otherwise fail at runtime by
 * returning NULL from an interrupt handler. */
_Static_assert(sizeof(m_emulator) == sizeof(DfcEmulator), "emulator slot size drift");
_Static_assert(
    sizeof(m_secure_messaging) == sizeof(DfcSecureMessaging),
    "secure messaging slot size drift");
_Static_assert(sizeof(m_session) == sizeof(DfcVirtualPiccSession), "session slot size drift");
_Static_assert(
    DFC_BYTEBUF_MAX >= DFC_WORKER_MAX_BUFFER_SIZE,
    "byte buffer smaller than the engine largest response");

/* The firmware has no user interface, so progress events go nowhere. The engine
 * reports them and carries on regardless. */
void dfc_port_notify(void* context, DfcEvent event) {
    DFC_UNUSED(context);
    DFC_UNUSED(event);
}
