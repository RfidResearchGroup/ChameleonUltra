/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * Firmware-side platform layer for the DESFire engine.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Top up the entropy reserve the engine draws on during authentication.
 *
 * Must be called from the main loop, never from an interrupt handler: it may
 * reach the SoftDevice, and the nRF52 RNG yields only about a byte per 120 us,
 * so filling on demand inside the NFC interrupt would overrun the frame delay
 * budget on its own. Cheap and non-blocking when the reserve is already full. */
void desfire_random_pump(void);

/* True while the reserve holds enough entropy for an authentication. Exposed
 * for diagnostics; the engine does not consult it. */
bool desfire_random_ready(void);

/* Count of times the engine asked for entropy the reserve could not supply and
 * the PRNG fallback was used. Should stay at zero; a non-zero value means the
 * main loop is not pumping often enough relative to reader traffic. */
unsigned desfire_random_starvations(void);
