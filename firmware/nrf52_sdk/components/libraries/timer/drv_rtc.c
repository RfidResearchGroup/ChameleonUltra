/**
 * Copyright (c) 2018 - 2021, Nordic Semiconductor ASA
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

#include <nrfx.h>
#include <nrf_delay.h>
#include <drv_rtc.h>

/* Module is integral part of app_timer implementation. */
#define NRF_LOG_MODULE_NAME app_timer
#include <nrf_log.h>

#define EVT_TO_STR(event)                                           \
    (event == NRF_RTC_EVENT_TICK      ? "NRF_RTC_EVENT_TICK"      : \
    (event == NRF_RTC_EVENT_OVERFLOW  ? "NRF_RTC_EVENT_OVERFLOW"  : \
    (event == NRF_RTC_EVENT_COMPARE_0 ? "NRF_RTC_EVENT_COMPARE_0" : \
    (event == NRF_RTC_EVENT_COMPARE_1 ? "NRF_RTC_EVENT_COMPARE_1" : \
    (event == NRF_RTC_EVENT_COMPARE_2 ? "NRF_RTC_EVENT_COMPARE_2" : \
    (event == NRF_RTC_EVENT_COMPARE_3 ? "NRF_RTC_EVENT_COMPARE_3" : \
                                        "UNKNOWN EVENT"))))))
#if defined ( __ICCARM__ )
/* IAR gives warning for offsetof with non-constant expression.*/
#define CC_IDX_TO_CC_EVENT(_cc) \
              ((nrf_rtc_event_t)(offsetof(NRF_RTC_Type, EVENTS_COMPARE[0]) + sizeof(uint32_t)*_cc))
#else
#define CC_IDX_TO_CC_EVENT(_cc) \
             ((nrf_rtc_event_t)(offsetof(NRF_RTC_Type, EVENTS_COMPARE[_cc])))
#endif

/**@brief RTC driver instance control block structure. */
typedef struct
{
    drv_rtc_t const * p_instance;
    nrfx_drv_state_t  state;        /**< Instance state. */
} drv_rtc_cb_t;

// User callbacks local storage.
static drv_rtc_handler_t m_handlers[DRV_RTC_ENABLED_COUNT];
static drv_rtc_cb_t      m_cb[DRV_RTC_ENABLED_COUNT];

// According to Produce Specification RTC may not trigger COMPARE event if CC value set is equal to
// COUNTER value or COUNTER+1.
#define COUNTER_TO_CC_MIN_DISTANCE 2

ret_code_t drv_rtc_init(drv_rtc_t const * const  p_instance,
                        drv_rtc_config_t const * p_config,
                        drv_rtc_handler_t        handler)
{
    ASSERT(p_instance);
    ASSERT(p_config);
    ASSERT(handler);

    ret_code_t err_code;

    m_handlers[p_instance->instance_id] = handler;

    if (m_cb[p_instance->instance_id].state != NRFX_DRV_STATE_UNINITIALIZED)
    {
        err_code = NRF_ERROR_INVALID_STATE;
        NRF_LOG_WARNING("RTC instance already initialized.");
        return err_code;
    }

    nrf_rtc_prescaler_set(p_instance->p_reg, p_config->prescaler);
    NRFX_IRQ_PRIORITY_SET(p_instance->irq, p_config->interrupt_priority);
    NRFX_IRQ_ENABLE(p_instance->irq);

    m_cb[p_instance->instance_id].state = NRFX_DRV_STATE_INITIALIZED;
    m_cb[p_instance->instance_id].p_instance = p_instance;

    err_code = NRF_SUCCESS;
    NRF_LOG_INFO("RTC: initialized.");
    return err_code;
}

void drv_rtc_uninit(drv_rtc_t const * const p_instance)
{
    ASSERT(p_instance);
    uint32_t mask = NRF_RTC_INT_TICK_MASK     |
                    NRF_RTC_INT_OVERFLOW_MASK |
                    NRF_RTC_INT_COMPARE0_MASK |
                    NRF_RTC_INT_COMPARE1_MASK |
                    NRF_RTC_INT_COMPARE2_MASK |
                    NRF_RTC_INT_COMPARE3_MASK;
    ASSERT(m_cb[p_instance->instance_id].state != NRFX_DRV_STATE_UNINITIALIZED);

    NRFX_IRQ_DISABLE(p_instance->irq);

    drv_rtc_stop(p_instance);
    nrf_rtc_event_disable(p_instance->p_reg, mask);
    nrf_rtc_int_disable(p_instance->p_reg, mask);

    m_cb[p_instance->instance_id].state = NRFX_DRV_STATE_UNINITIALIZED;
    NRF_LOG_INFO("RTC: Uninitialized.");
}

void drv_rtc_start(drv_rtc_t const * const p_instance)
{
    ASSERT(p_instance);
    nrf_rtc_task_trigger(p_instance->p_reg, NRF_RTC_TASK_START);
}

void drv_rtc_stop(drv_rtc_t const * const p_instance)
{
    ASSERT(p_instance);
    nrf_rtc_task_trigger(p_instance->p_reg, NRF_RTC_TASK_STOP);
}

void drv_rtc_compare_set(drv_rtc_t const * const p_instance,
                         uint32_t                cc,
                         uint32_t                abs_value,
                         bool                    irq_enable)
{
    ASSERT(p_instance);
    nrf_rtc_int_t   cc_int_mask = (nrf_rtc_int_t)(NRF_RTC_INT_COMPARE0_MASK << cc);
    nrf_rtc_event_t cc_evt      = CC_IDX_TO_CC_EVENT(cc);
    abs_value &= RTC_COUNTER_COUNTER_Msk;

    nrf_rtc_int_disable(p_instance->p_reg, cc_int_mask);
    nrf_rtc_event_disable(p_instance->p_reg, cc_int_mask);
    nrf_rtc_event_clear(p_instance->p_reg, cc_evt);
    nrf_rtc_cc_set(p_instance->p_reg, cc,abs_value);
    nrf_rtc_event_enable(p_instance->p_reg, cc_int_mask);

    if (irq_enable)
    {
        nrf_rtc_int_enable(p_instance->p_reg, cc_int_mask);
    }
}

static void evt_enable(drv_rtc_t const * const p_instance, uint32_t mask, bool irq_enable)
{
    ASSERT(p_instance);
    nrf_rtc_event_enable(p_instance->p_reg, mask);
    if (irq_enable)
    {
        nrf_rtc_int_enable(p_instance->p_reg, mask);
    }
}

static void evt_disable(drv_rtc_t const * const p_instance, uint32_t mask)
{
    ASSERT(p_instance);
    nrf_rtc_event_disable(p_instance->p_reg, mask);
    nrf_rtc_int_disable(p_instance->p_reg, mask);
}

static bool evt_pending(drv_rtc_t const * const p_instance, nrf_rtc_event_t event)
{
    ASSERT(p_instance);
    if (nrf_rtc_event_pending(p_instance->p_reg, event))
    {
        nrf_rtc_event_clear(p_instance->p_reg, event);
        return true;
    }
    return false;
}

static uint32_t ticks_sub(uint32_t a, uint32_t b)
{
    return (a - b) & RTC_COUNTER_COUNTER_Msk;
}

ret_code_t drv_rtc_windowed_compare_set(drv_rtc_t const * const p_instance,
                                        uint32_t                cc,
                                        uint32_t                abs_value,
                                        uint32_t                safe_window)
{
    ASSERT(p_instance);
    uint32_t        prev_cc_set;
    uint32_t        now;
    uint32_t        diff;
    nrf_rtc_int_t   cc_int_mask = (nrf_rtc_int_t)(NRF_RTC_INT_COMPARE0_MASK << cc);
    nrf_rtc_event_t cc_evt      = CC_IDX_TO_CC_EVENT(cc);;
    abs_value &=RTC_COUNTER_COUNTER_Msk;

    evt_disable(p_instance, cc_int_mask);

    /* First handle potential prefiring caused by CC being set to next tick. Even if CC is
     * overwritten it may happen that event will be generated for previous CC in next tick.
     * Following algorith is applied:
     * - read previous CC
     * - write current counter value to CC (furtherest in future)
     * - if previous CC was in one tick from now wait half of the 32k tick and clear event which
     *   may be set. Half tick delay is used because CC is latched in the middle of the 32k tick.
     */
    now = nrf_rtc_counter_get(p_instance->p_reg);
    prev_cc_set = nrf_rtc_cc_get(p_instance->p_reg, cc);
    nrf_rtc_cc_set(p_instance->p_reg, cc, now);
    nrf_rtc_event_clear(p_instance->p_reg, cc_evt);

    if (ticks_sub(prev_cc_set, now) == 1)
    {
        nrf_delay_us(16);
        nrf_rtc_event_clear(p_instance->p_reg, cc_evt);
    }

    now = nrf_rtc_counter_get(p_instance->p_reg);
    diff = ticks_sub(abs_value, now);

    nrf_rtc_event_enable(p_instance->p_reg, cc_int_mask);

    /* Setting CC for +1 from now may not generate event. In that case set CC+2 and check if counter
     * changed during that process. If changed it means that 1 tick expired. */
    if (diff == 1)
    {
        nrf_rtc_cc_set(p_instance->p_reg, cc, abs_value + 1);
        nrf_delay_us(16);
        if (now != nrf_rtc_counter_get(p_instance->p_reg))
        {
            /* one tick elapsed already. */
            return NRF_ERROR_TIMEOUT;
        }
    } else {
        nrf_rtc_cc_set(p_instance->p_reg, cc, abs_value);
        now = nrf_rtc_counter_get(p_instance->p_reg);
        diff = ticks_sub(abs_value - 1, now);
        /* Check if counter equals cc value or is behind in the safe window. If yes it means that
         * CC expired. */
        if (diff > (RTC_COUNTER_COUNTER_Msk - safe_window))
        {
            return NRF_ERROR_TIMEOUT;
        }
        else if (diff == 0)
        {
            /* If cc value == counter + 1, it may hit +1 case. */
            nrf_rtc_cc_set(p_instance->p_reg, cc, abs_value + 1);
            if (now != nrf_rtc_counter_get(p_instance->p_reg))
            {
                /* one tick elapsed already. */
                return NRF_ERROR_TIMEOUT;
            }
        }
    }

    evt_enable(p_instance, cc_int_mask, true);

    return NRF_SUCCESS;
}

void drv_rtc_overflow_enable(drv_rtc_t const * const p_instance, bool irq_enable)
{
    evt_enable(p_instance, NRF_RTC_INT_OVERFLOW_MASK, irq_enable);
}

void drv_rtc_overflow_disable(drv_rtc_t const * const p_instance)
{
    evt_disable(p_instance, NRF_RTC_INT_OVERFLOW_MASK);
}

bool drv_rtc_overflow_pending(drv_rtc_t const * const p_instance)
{
    return evt_pending(p_instance, NRF_RTC_EVENT_OVERFLOW);
}

void drv_rtc_tick_enable(drv_rtc_t const * const p_instance, bool irq_enable)
{
    evt_enable(p_instance, NRF_RTC_INT_TICK_MASK, irq_enable);
}

void drv_rtc_tick_disable(drv_rtc_t const * const p_instance)
{
    evt_disable(p_instance, NRF_RTC_INT_TICK_MASK);
}

bool drv_rtc_tick_pending(drv_rtc_t const * const p_instance)
{
    return evt_pending(p_instance, NRF_RTC_EVENT_TICK);
}

void drv_rtc_compare_enable(drv_rtc_t const * const p_instance,
                            uint32_t                cc,
                            bool                    irq_enable)
{
    evt_enable(p_instance, (uint32_t)NRF_RTC_INT_COMPARE0_MASK << cc, irq_enable);
}

void drv_rtc_compare_disable(drv_rtc_t const * const p_instance, uint32_t cc)
{
    evt_disable(p_instance, (uint32_t)NRF_RTC_INT_COMPARE0_MASK << cc);
}

bool drv_rtc_compare_pending(drv_rtc_t const * const p_instance, uint32_t cc)
{
    nrf_rtc_event_t cc_evt = CC_IDX_TO_CC_EVENT(cc);
    return evt_pending(p_instance, cc_evt);
}

uint32_t drv_rtc_compare_get(drv_rtc_t const * const p_instance, uint32_t cc)
{
    return nrf_rtc_cc_get(p_instance->p_reg, cc);
}

uint32_t drv_rtc_counter_get(drv_rtc_t const * const p_instance)
{
    return nrf_rtc_counter_get(p_instance->p_reg);
}

void drv_rtc_irq_trigger(drv_rtc_t const * const p_instance)
{
    NVIC_SetPendingIRQ(p_instance->irq);
}

#define drv_rtc_rtc_0_irq_handler RTC0_IRQHandler
#define drv_rtc_rtc_1_irq_handler RTC1_IRQHandler
#define drv_rtc_rtc_2_irq_handler RTC2_IRQHandler

#if defined(APP_TIMER_V2_RTC0_ENABLED)
void drv_rtc_rtc_0_irq_handler(void)
{
    m_handlers[DRV_RTC_RTC0_INST_IDX](m_cb[DRV_RTC_RTC0_INST_IDX].p_instance);
}
#endif

#if defined(APP_TIMER_V2_RTC1_ENABLED)
void drv_rtc_rtc_1_irq_handler(void)
{
    m_handlers[DRV_RTC_RTC1_INST_IDX](m_cb[DRV_RTC_RTC1_INST_IDX].p_instance);
}
#endif

#if defined(APP_TIMER_V2_RTC2_ENABLED)
void drv_rtc_rtc_2_irq_handler(void)
{
    m_handlers[DRV_RTC_RTC2_INST_IDX](m_cb[DRV_RTC_RTC2_INST_IDX].p_instance);
}
#endif
