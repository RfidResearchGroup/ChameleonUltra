/**
 * Copyright (c) 2020 - 2021, Nordic Semiconductor ASA
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
#ifndef NRF21540_MACRO_H_
#define NRF21540_MACRO_H_

#include "nrf21540_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Macro for retrieving the state of the nRF21540 radio event. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_EVENT_CHECK(event) \
        nrf_radio_event_check(event)
#else
#define NRF21540_RADIO_EVENT_CHECK(event) \
        nrf_egu_event_check(NRF21540_EGU, event)
#endif

/**@brief Macro for clearing the nRF21540 radio event. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_EVENT_CLEAR(event) \
        nrf_radio_event_clear(event)
#else
#define NRF21540_RADIO_EVENT_CLEAR(event) \
        nrf_egu_event_clear(NRF21540_EGU, event)
#endif

/**@brief Macro for triggering the nRF21540 radio task. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_TASK_TRIGGER(task) \
        nrf_radio_task_trigger(event)
#else
#define NRF21540_RADIO_TASK_TRIGGER(task) \
        nrf_egu_task_trigger(NRF21540_EGU, event)
#endif

/**@brief Macro for disabling the nRF21540 interrupts. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_INT_DISABLE(mask) \
        nrf_radio_int_disable(mask)
#else
#define NRF21540_RADIO_INT_DISABLE(mask) \
        nrf_egu_int_disable(NRF21540_EGU, mask)
#endif

/**@brief Macro for enabling the nRF21540 interrupts. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_INT_ENABLE(mask) \
        nrf_radio_int_enable(mask)
#else
#define NRF21540_RADIO_INT_ENABLE(mask) \
        nrf_egu_int_enable(NRF21540_EGU, mask)
#endif

/**@brief Macro for enabling the nRF21540 shorts. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_SHORTS_ENABLE(shorts_mask) \
        nrf_radio_shorts_enable(shorts_mask)
#else
#define NRF21540_RADIO_SHORTS_ENABLE(shorts_mask) \
        (m_nrf21540_data.shorts |= shorts_mask)
#endif

/**@brief Macro for disabling the nRF21540 shorts. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_SHORTS_DISABLE(shorts_mask) \
        nrf_radio_shorts_disable(shorts_mask)
#else
#define NRF21540_RADIO_SHORTS_DISABLE(shorts_mask) \
     (m_nrf21540_data.shorts &= ~shorts_mask)
#endif

/**@brief Macro for disabling the nRF21540 shorts. */
#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_SHORTS_ENABLE_CHECK(shorts_mask) \
        (nrf_radio_shorts_get() && shorts_mask)
#else
#define NRF21540_RADIO_SHORTS_ENABLE_CHECK(shorts_mask) \
        (m_nrf21540_data.shorts && shorts_mask)
#endif

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_MACRO_H_
