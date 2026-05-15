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
#include "app_timer.h"
#include "nrf_atfifo.h"
#include "nrf_sortlist.h"
#include "nrf_delay.h"
#if APP_TIMER_WITH_PROFILER
#include "app_util_platform.h"
#endif
#if APP_TIMER_CONFIG_USE_SCHEDULER
#include "app_scheduler.h"
#endif
#include <stddef.h>
#define NRF_LOG_MODULE_NAME APP_TIMER_LOG_NAME
#if APP_TIMER_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       APP_TIMER_CONFIG_LOG_LEVEL
#define NRF_LOG_INFO_COLOR  APP_TIMER_CONFIG_INFO_COLOR
#define NRF_LOG_DEBUG_COLOR APP_TIMER_CONFIG_DEBUG_COLOR
#else //APP_TIMER_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       0
#endif //APP_TIMER_CONFIG_LOG_ENABLED
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#include "drv_rtc.h"

/**
 * Maximum possible relative value is limited by safe window to detect cases when requested
 * compare event has already occured.
 */
#define APP_TIMER_SAFE_WINDOW APP_TIMER_TICKS(APP_TIMER_SAFE_WINDOW_MS)

#define APP_TIMER_RTC_MAX_VALUE   (DRV_RTC_MAX_CNT - APP_TIMER_SAFE_WINDOW)

/* Check if timer is idle */
#define APP_TIMER_IS_IDLE(timer) (timer->end_val == APP_TIMER_IDLE_VAL)

static drv_rtc_t m_rtc_inst = DRV_RTC_INSTANCE(1);

#if APP_TIMER_WITH_PROFILER
static uint8_t m_max_user_op_queue_utilization;     /**< Maximum observed timer user operations queue utilization. */
static uint8_t m_current_user_op_queue_utilization; /**< Currently observed timer user operations queue utilization. */
#endif /* APP_TIMER_WITH_PROFILER */

/**
 * @brief Timer requests types.
 */
typedef enum
{
    TIMER_REQ_START,
    TIMER_REQ_STOP,
    TIMER_REQ_STOP_ALL
} app_timer_req_type_t;

/**
 * @brief Operation request structure.
 */
typedef struct
{
    app_timer_req_type_t type;    /**< Request type. */
    app_timer_t *        p_timer; /**< Timer instance. */
} timer_req_t;

static app_timer_t * volatile mp_active_timer; /**< Timer currently handled by RTC driver. */
static bool                   m_global_active; /**< Flag used to globally disable all timers. */
static uint64_t m_base_counter;
static uint64_t m_stamp64;

/* Request FIFO instance. */
NRF_ATFIFO_DEF(m_req_fifo, timer_req_t, APP_TIMER_CONFIG_OP_QUEUE_SIZE);

/* Sortlist instance. */
static bool compare_func(nrf_sortlist_item_t * p_item0, nrf_sortlist_item_t *p_item1);
NRF_SORTLIST_DEF(m_app_timer_sortlist, compare_func); /**< Sortlist used for storing queued timers. */

/**
 * @brief Return current 64 bit timestamp
 */
static uint64_t get_now(void)
{
    uint64_t now = m_base_counter + drv_rtc_counter_get(&m_rtc_inst);

    /* it is possible that base was not updated and overflow occured, in that case 'now' will be
     * 24bit value behind. Additional timestamp updated on every 24 bit period is used to detect
     * that case. Apart from that 'now' should never be behind previously read timestamp.
     */
    if (now < m_stamp64) {
        now += (DRV_RTC_MAX_CNT + 1);
    }

    return now;
}
/**
 * @brief Function used for comparing items in sorted list.
 */
static inline bool compare_func(nrf_sortlist_item_t * p_item0, nrf_sortlist_item_t *p_item1)
{
    app_timer_t * p0 = CONTAINER_OF(p_item0, app_timer_t, list_item);
    app_timer_t * p1 = CONTAINER_OF(p_item1, app_timer_t, list_item);

    uint64_t p0_end = p0->end_val;
    uint64_t p1_end = p1->end_val;
    return (p0_end <= p1_end) ? true : false;
}

#if APP_TIMER_CONFIG_USE_SCHEDULER
static void scheduled_timeout_handler(void * p_event_data, uint16_t event_size)
{
    ASSERT(event_size == sizeof(app_timer_event_t));
    app_timer_event_t const * p_timer_event = (app_timer_event_t *)p_event_data;

    p_timer_event->timeout_handler(p_timer_event->p_context);
}
#endif

/**
 * @brief Function called on timer expiration
 * If end value is not reached it is assumed that it was partial expiration and time is put back
 * into the list. Otherwise function calls user handler if timer was not stopped before. If timer
 * is in repeated mode then timer is rescheduled.
 *
 * @param p_timer Timer instance.
 *
 * @return True if reevaluation of sortlist needed (becasue it was updated).
 */
static bool timer_expire(app_timer_t * p_timer)
{
    ASSERT(p_timer->handler);
    bool ret = false;

    if ((m_global_active == true) && (p_timer != NULL))
    {
        if (get_now() >= p_timer->end_val) {
            bool cont;

            /* timer expired */
            CRITICAL_REGION_ENTER();
            /* In case of single shot, set timer to idle. */
            if (p_timer->repeat_period == 0)
            {
                p_timer->end_val = APP_TIMER_IDLE_VAL;
            }
            CRITICAL_REGION_EXIT();
    #if APP_TIMER_CONFIG_USE_SCHEDULER
            app_timer_event_t timer_event;

            timer_event.timeout_handler = p_timer->handler;
            timer_event.p_context       = p_timer->p_context;
            uint32_t err_code = app_sched_event_put(&timer_event,
                                                    sizeof(timer_event),
                                                    scheduled_timeout_handler);
            APP_ERROR_CHECK(err_code);
    #else
            NRF_LOG_DEBUG("Timer expired (context: %d)", (uint32_t)p_timer->p_context)
            p_timer->handler(p_timer->p_context);
    #endif
            CRITICAL_REGION_ENTER();
            /* check active flag as it may have been stopped in the user handler */
            if (p_timer->repeat_period && !APP_TIMER_IS_IDLE(p_timer))
            {
                p_timer->end_val += p_timer->repeat_period;
                cont = true;
            }
            else
            {
                cont = false;
            }
            CRITICAL_REGION_EXIT();

            if (cont)
            {
                nrf_sortlist_add(&m_app_timer_sortlist, &p_timer->list_item);
                ret = true;
            }
        }
        else if (!APP_TIMER_IS_IDLE(p_timer))
        {
            nrf_sortlist_add(&m_app_timer_sortlist, &p_timer->list_item);
            ret = true;
        }
    }
    return ret;
}

/**
 * @brief Function is configuring RTC driver to trigger timeout interrupt for given timer.
 *
 * It is possible that RTC driver will indicate that timeout already occured. In that case timer
 * expires and function indicates that RTC was not configured.
 *
 * @param          p_timer Timer instance.
 * @param [in,out] p_rerun Flag indicating that sortlist reevaluation is required.
 *
 * @return True if RTC was successfully configured, false if timer already expired and RTC was not
 *         configured.
 *
 */
static bool rtc_schedule(app_timer_t * p_timer, bool * p_rerun)
{
    ret_code_t ret = NRF_ERROR_TIMEOUT;
    *p_rerun = false;
    /* In case timer got stopped in between, end_val will be very far in the
     * future. RTC will be reconfigured on the next iteration.
     */
    uint64_t end_val = p_timer->end_val;
    int64_t remaining = (int64_t)(end_val - get_now());

    if (remaining > 0) {
        uint32_t cc_val = ((uint32_t)remaining > APP_TIMER_RTC_MAX_VALUE) ?
                (app_timer_cnt_get() + APP_TIMER_RTC_MAX_VALUE) : end_val;

        ret = drv_rtc_windowed_compare_set(&m_rtc_inst, 0, cc_val, APP_TIMER_SAFE_WINDOW);
        NRF_LOG_DEBUG("Setting CC to 0x%08x (err: %d)", cc_val & DRV_RTC_MAX_CNT, ret);
        if (ret == NRF_SUCCESS)
        {
            return true;
        }
    }
    else
    {
        drv_rtc_compare_disable(&m_rtc_inst, 0);
    }

    if (ret == NRF_ERROR_TIMEOUT)
    {
        *p_rerun = timer_expire(p_timer);
    }
    else
    {
        NRF_LOG_ERROR("Unexpected error: %d", ret);
        ASSERT(0);
    }

    return false;
}

static inline app_timer_t * sortlist_pop(void)
{
    nrf_sortlist_item_t * p_next_item = nrf_sortlist_pop(&m_app_timer_sortlist);
    return p_next_item ? CONTAINER_OF(p_next_item, app_timer_t, list_item) : NULL;
}

static inline app_timer_t * sortlist_peek(void)
{
    nrf_sortlist_item_t const * p_next_item = nrf_sortlist_peek(&m_app_timer_sortlist);
    return p_next_item ? CONTAINER_OF(p_next_item, app_timer_t, list_item) : NULL;
}

/**
 * @brief Function for deactivating all timers which are in the sorted list (active timers).
 */
static void sorted_list_stop_all(void)
{
    app_timer_t * p_next;
    do
    {
        p_next = sortlist_pop();
        if (p_next)
        {
            p_next->end_val = APP_TIMER_IDLE_VAL;
        }
    } while (p_next);
}

/**
 * @brief Function for handling RTC counter overflow.
 *
 * Increment base counter used to calculate 64 bit timestamp.
 */
static void on_overflow_evt(void)
{
    NRF_LOG_DEBUG("Overflow EVT");
    m_base_counter += (DRV_RTC_MAX_CNT + 1);
}

/**
 * #brief Function for handling RTC compare event - active timer expiration.
 */
static void on_compare_evt(drv_rtc_t const * const  p_instance)
{
    if (mp_active_timer)
    {
        /* If assert fails it suggests that safe window should be increased. */
        ASSERT(app_timer_cnt_diff_compute(drv_rtc_counter_get(p_instance),
                                          drv_rtc_compare_get(p_instance, 0)) < APP_TIMER_SAFE_WINDOW);

        NRF_LOG_INST_DEBUG(mp_active_timer->p_log, "Compare EVT");
        UNUSED_RETURN_VALUE(timer_expire(mp_active_timer));
        mp_active_timer = NULL;
    }
    else
    {
        NRF_LOG_WARNING("Compare event but no active timer (already stopped?)");
    }
}

/**
 * @brief Channel 1 is triggered in the middle of 24 bit period to updated control timestamp in
 * place where there is no risk of overflow.
 */
static void on_compare1_evt(drv_rtc_t const * const  p_instance)
{
    m_stamp64 = get_now();
}

/**
 * @brief Function updates RTC.
 *
 * Function is called at the end of RTC interrupt when all new user request and/or timer expiration
 * occured. It configures RTC if there is any pending timer, reconfigures if the are timers with
 * shorted timeout than active one or stops RTC if there is no active timers.
 */
static void rtc_update(drv_rtc_t const * const  p_instance)
{
    while(1)
    {
        app_timer_t * p_next = sortlist_peek();
        bool rtc_reconf = false;
        if (p_next) //Candidate for active timer
        {
            /* If timer was stopped just remove it from the sortlist and continue.
             * Note that it is possible, that stop/start requests are pending in
             * the request queue if added from higher priority context. In
             * that case end_val was first set to invalid value and then to the
             * new timeout in the future. In that case, timer location in sortlist
             * is invalid. However, it will all be sorted out when stop and start
             * requests are handled.
             */
            if (APP_TIMER_IS_IDLE(p_next)) {
                (void)sortlist_pop();
                continue;
            }
            else if (mp_active_timer == NULL)
            {
                //There is no active timer so candidate will become active timer.
                rtc_reconf = true;
            }
            else if (p_next->end_val < mp_active_timer->end_val)
            {
                //Candidate has shorter timeout than current active timer. Candidate will replace active timer.
                //Active timer is put back into sorted list.
                rtc_reconf = true;
                if (!APP_TIMER_IS_IDLE(mp_active_timer))
                {
                    NRF_LOG_INST_DEBUG(mp_active_timer->p_log, "Timer preempted.");
                    nrf_sortlist_add(&m_app_timer_sortlist, &mp_active_timer->list_item);
                }
            }

            if (rtc_reconf)
            {
                bool rerun;
                p_next = sortlist_pop();
                NRF_LOG_INST_DEBUG(p_next->p_log, "Activating timer (CC:%d/%08x).", p_next->end_val, p_next->end_val);
                if (rtc_schedule(p_next, &rerun))
                {
                    if (!APP_TIMER_KEEPS_RTC_ACTIVE && (mp_active_timer == NULL))
                    {
                        drv_rtc_start(p_instance);
                    }
                    mp_active_timer = p_next;

                    if (rerun == false)
                    {
                        //RTC was successfully updated and sortlist was not updated. Function can be terminated.
                        break;
                    }
                }
                else
                {
                    //If RTC driver indicated that timeout already occured a new candidate will be taken from sorted list.
                    NRF_LOG_INST_DEBUG(p_next->p_log,"Timer expired before scheduled to RTC.");
                    mp_active_timer = NULL;
                }
            }
            else
            {
                //RTC will not be updated. Function can terminate.
                break;
            }
        }
        else //No candidate for active timer.
        {
            if (!APP_TIMER_KEEPS_RTC_ACTIVE && (mp_active_timer == NULL))
            {
                drv_rtc_stop(p_instance);
            }
            break;
        }
    }
}

/**
 * @brief Function for processing user requests.
 *
 * Function is called only in the context of RTC interrupt.
 */
static void timer_req_process(drv_rtc_t const * const  p_instance)
{
    nrf_atfifo_item_get_t fifo_ctx;
    timer_req_t *         p_req = nrf_atfifo_item_get(m_req_fifo, &fifo_ctx);

    while (p_req)
    {
        switch (p_req->type)
        {
            case TIMER_REQ_START:
                /* Check for idle in most of the cases is not needed but it serves
                 * for following corner case:
                 * - timer was active (request processed)
                 * - timer was stopped and started from higher priority which
                 *   interrupted handling timeout. End_val is currently set to the
                 *   timeout value of the next start request. If that value already
                 *   expired, timeout expires before stop, start requests are handled.
                 * - When start request is handled, timer is idle and should not be
                 *   added to the queue but just dropped.
                 */
                if (!APP_TIMER_IS_IDLE(p_req->p_timer))
                {
                    nrf_sortlist_add(&m_app_timer_sortlist, &(p_req->p_timer->list_item));
                    NRF_LOG_INST_DEBUG(p_req->p_timer->p_log,"Start request (expiring at %d/0x%08x).",
                                                  p_req->p_timer->end_val, p_req->p_timer->end_val);
                }
                break;
            case TIMER_REQ_STOP:
                if (p_req->p_timer == mp_active_timer)
                {
                    mp_active_timer = NULL;
                }
                else
                {
                    bool found = nrf_sortlist_remove(&m_app_timer_sortlist, &(p_req->p_timer->list_item));
                    if (!found)
                    {
                         NRF_LOG_INFO("Timer not found on sortlist (stopping expired timer).");
                    }
                }
                NRF_LOG_INST_DEBUG(p_req->p_timer->p_log,"Stop request.");
                break;
            case TIMER_REQ_STOP_ALL:
                sorted_list_stop_all();
                m_global_active = true;
                NRF_LOG_INFO("Stop all request.");
                break;
            default:
                break;
        }
#if APP_TIMER_WITH_PROFILER
        CRITICAL_REGION_ENTER();
#endif
        UNUSED_RETURN_VALUE(nrf_atfifo_item_free(m_req_fifo, &fifo_ctx));
#if APP_TIMER_WITH_PROFILER
        if (m_max_user_op_queue_utilization < m_current_user_op_queue_utilization)
        {
            m_max_user_op_queue_utilization = m_current_user_op_queue_utilization;
        }
        --m_current_user_op_queue_utilization;
        CRITICAL_REGION_EXIT();
#endif /* APP_TIMER_WITH_PROFILER */
        p_req = nrf_atfifo_item_get(m_req_fifo, &fifo_ctx);
    }
}

static void rtc_irq(drv_rtc_t const * const  p_instance)
{
    if (drv_rtc_overflow_pending(p_instance))
    {
        on_overflow_evt();
    }
    if (drv_rtc_compare_pending(p_instance, 0))
    {
        on_compare_evt(p_instance);
    }
    if (drv_rtc_compare_pending(p_instance, 1))
    {
        on_compare1_evt(p_instance);
    }

    timer_req_process(p_instance);
    rtc_update(p_instance);
}

/**
 * @brief Function for triggering processing user requests.
 *
 * @note All user requests are processed in a single context - RTC interrupt.
 */
static inline void timer_request_proc_trigger(void)
{
    drv_rtc_irq_trigger(&m_rtc_inst);
}

/**
 * @brief Function for putting user request into the request queue
 */
static ret_code_t timer_req_schedule(app_timer_req_type_t type, app_timer_t * p_timer)
{
    nrf_atfifo_item_put_t fifo_ctx;
    timer_req_t * p_req;
#if APP_TIMER_WITH_PROFILER
    CRITICAL_REGION_ENTER();
#endif
    p_req = nrf_atfifo_item_alloc(m_req_fifo, &fifo_ctx);
#if APP_TIMER_WITH_PROFILER
    if (p_req)
    {
        ++m_current_user_op_queue_utilization;
    }
    CRITICAL_REGION_EXIT();
#endif /* APP_TIMER_WITH_PROFILER */
    if (p_req)
    {
        p_req->type    = type;
        p_req->p_timer = p_timer;
        if (nrf_atfifo_item_put(m_req_fifo, &fifo_ctx))
        {
            timer_request_proc_trigger();
        }
        else
        {
            NRF_LOG_WARNING("Scheduling interrupted another scheduling.");
        }
        return NRF_SUCCESS;
    }
    else
    {
        return NRF_ERROR_NO_MEM;
    }
}

ret_code_t app_timer_init(void)
{
    ret_code_t err_code;
    drv_rtc_config_t config = {
        .prescaler          = APP_TIMER_CONFIG_RTC_FREQUENCY,
        .interrupt_priority = APP_TIMER_CONFIG_IRQ_PRIORITY
    };

    err_code = NRF_ATFIFO_INIT(m_req_fifo);
    if (err_code != NRFX_SUCCESS)
    {
        return err_code;
    }

    err_code = drv_rtc_init(&m_rtc_inst, &config, rtc_irq);
    if (err_code != NRFX_SUCCESS)
    {
        return err_code;
    }
    drv_rtc_overflow_enable(&m_rtc_inst, true);
    drv_rtc_compare_set(&m_rtc_inst, 1, DRV_RTC_MAX_CNT >> 1, true);
    if (APP_TIMER_KEEPS_RTC_ACTIVE)
    {
        drv_rtc_start(&m_rtc_inst);
    }

    m_global_active = true;
    return err_code;
}

ret_code_t app_timer_create(app_timer_id_t const *      p_timer_id,
                            app_timer_mode_t            mode,
                            app_timer_timeout_handler_t timeout_handler)
{
    ASSERT(p_timer_id);
    ASSERT(timeout_handler);

    if (timeout_handler == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    app_timer_t * p_t = (app_timer_t *) *p_timer_id;
    p_t->end_val = APP_TIMER_IDLE_VAL;
    p_t->handler = timeout_handler;
    p_t->repeat_period = (mode == APP_TIMER_MODE_REPEATED) ? 1 : 0;
    return NRF_SUCCESS;
}

ret_code_t app_timer_start(app_timer_t * p_timer, uint32_t timeout_ticks, void * p_context)
{
    ASSERT(p_timer);
    app_timer_t * p_t = (app_timer_t *) p_timer;
    bool cont;

    CRITICAL_REGION_ENTER();
    if (APP_TIMER_IS_IDLE(p_t))
    {
        /* TImer is idle and can be started. Note that timer can still be
         * in use by the engine since stop request may be still pending if
         * it was scheduled from higher priority interrupt (same as this start).
         * In that case, end value is shifted to the future which will prevent
         * previous timeout value to expire.*/
        p_t->end_val = get_now() + timeout_ticks;
        cont = true;
    }
    else
    {
        cont = false;
    }
    CRITICAL_REGION_EXIT();

    /* Timer in use */
    if (!cont)
    {
        return NRF_SUCCESS;
    }

    p_t->p_context = p_context;

    if (p_t->repeat_period)
    {
        p_t->repeat_period = timeout_ticks;
    }

    return timer_req_schedule(TIMER_REQ_START, p_t);
}


ret_code_t app_timer_stop(app_timer_t * p_timer)
{
    ASSERT(p_timer);
    app_timer_t * p_t = (app_timer_t *) p_timer;

    bool cont;

    CRITICAL_REGION_ENTER();
    if (APP_TIMER_IS_IDLE(p_t))
    {
        /* TImer is idle and can not be stopped. */
        cont = false;
    }
    else
    {
        /* Set end value to invalid (unrealistic future) value. */
        p_t->end_val = APP_TIMER_IDLE_VAL;
        cont = true;
    }
    CRITICAL_REGION_EXIT();

    if (!cont)
    {
        return NRF_SUCCESS;
    }

    return timer_req_schedule(TIMER_REQ_STOP, p_t);
}

ret_code_t app_timer_stop_all(void)
{
    //block timer globally
    m_global_active = false;

    return timer_req_schedule(TIMER_REQ_STOP_ALL, NULL);
}

#if APP_TIMER_WITH_PROFILER
uint8_t app_timer_op_queue_utilization_get(void)
{
    return m_max_user_op_queue_utilization;
}
#endif /* APP_TIMER_WITH_PROFILER */

uint32_t app_timer_cnt_diff_compute(uint32_t   ticks_to,
                                    uint32_t   ticks_from)
{
    return ((ticks_to - ticks_from) & RTC_COUNTER_COUNTER_Msk);
}

uint32_t app_timer_cnt_get(void)
{
    return drv_rtc_counter_get(&m_rtc_inst);
}

void app_timer_pause(void)
{
    drv_rtc_stop(&m_rtc_inst);
}

void app_timer_resume(void)
{
    drv_rtc_start(&m_rtc_inst);
}
