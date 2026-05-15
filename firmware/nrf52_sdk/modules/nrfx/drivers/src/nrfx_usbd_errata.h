/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef NRFX_USBD_ERRATA_H__
#define NRFX_USBD_ERRATA_H__

#include <nrfx.h>
#include <nrf_erratas.h>

#ifndef NRFX_USBD_ERRATA_ENABLE
/**
 * @brief The constant that informs if errata should be enabled at all.
 *
 * If this constant is set to 0, all the Errata bug fixes will be automatically disabled.
 */
#define NRFX_USBD_ERRATA_ENABLE 1
#endif


/* Errata: ISO double buffering not functional. */
static inline bool nrfx_usbd_errata_166(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_166();
}

/* Errata: USBD might not reach its active state. */
static inline bool nrfx_usbd_errata_171(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_171();
}

/* Errata: USB cannot be enabled. */
static inline bool nrfx_usbd_errata_187(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_187();
}

/* Errata: USBD cannot receive tasks during DMA. */
static inline bool nrfx_usbd_errata_199(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_199();
}

/* Errata: Device remains in SUSPEND too long. */
static inline bool nrfx_usbd_errata_211(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_211();
}

/* Errata: Unexpected behavior after reset. **/
static inline bool nrfx_usbd_errata_223(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrf52_errata_223();
}

#endif // NRFX_USBD_ERRATA_H__
