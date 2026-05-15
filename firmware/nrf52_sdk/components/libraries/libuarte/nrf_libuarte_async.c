/**
 * Copyright (c) 2019 - 2021, Nordic Semiconductor ASA
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
#include "sdk_config.h"
#include "nrf_libuarte_async.h"
#include "app_error.h"
#include "nrf_balloc.h"
#include "nrfx_timer.h"
#include "nrfx_rtc.h"
#include "nrfx_ppi.h"
#include "nrf_uart.h"
#include "nrf_queue.h"
#define NRF_LOG_MODULE_NAME libUARTE_async
#if NRF_LIBUARTE_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       NRF_LIBUARTE_CONFIG_LOG_LEVEL
#define NRF_LOG_INFO_COLOR  NRF_LIBUARTE_CONFIG_INFO_COLOR
#define NRF_LOG_DEBUG_COLOR NRF_LIBUARTE_CONFIG_DEBUG_COLOR
#else // NRF_LIBUARTE_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       0
#endif // NRF_LIBUARTE_CONFIG_LOG_ENABLED
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#if defined(NRFX_RTC_ENABLED) && NRFX_RTC_ENABLED
#define RTC_IN_USE 1
#else
#define RTC_IN_USE 0
#endif

#if defined(NRFX_TIMER_ENABLED) && NRFX_TIMER_ENABLED
#define TIMER_IN_USE 1
#else
#define TIMER_IN_USE 0
#endif

#define FAULT_IRQ_LEVEL 0xFF

/** Macro is setting up PPI channel set which consist of event, task and optional fork.
 *
 * @param _ch   Channel.
 * @param _evt  Event.
 * @param _tsk  Task.
 * @param _fork Fork. If NULL fork is not configured.
 */
#define PPI_CH_SETUP(_ch, _evt, _tsk, _fork)            \
    ret = nrfx_ppi_channel_assign(_ch, _evt, _tsk);     \
    if (ret != NRF_SUCCESS)                             \
    {                                                   \
        return NRF_ERROR_INTERNAL;                      \
    }                                                   \
    if (_fork)                                          \
    {                                                   \
        ret = nrfx_ppi_channel_fork_assign(_ch, _fork); \
        if (ret != NRF_SUCCESS)                         \
        {                                               \
            return NRF_ERROR_INTERNAL;                  \
        }                                               \
    }

/* @brief Function returns interrupt level which is the next,lower priority.
 *
 * If SoftDevice is present then it takes into account which priorities are used
 * by the SoftDevice.
 *
 * @note Caller of this function does not check if error is returned. Error is returned if input
 * priority belongs to SoftDevice. In that case SoftDevice will detect attempt to interrupt level
 * misuse.
 *
 * @param prio Interrupt priority.
 *
 * @return Priority which is one level lower or fault indicator (0xFF).
 */
static uint8_t irq_prio_inc(uint8_t prio)
{
#ifdef SOFTDEVICE_PRESENT
    static const uint8_t sd_next_irq_lut[] = {
            FAULT_IRQ_LEVEL, /* 0 used by softdevice */
            FAULT_IRQ_LEVEL, /* 1 used by softdevice */
            APP_IRQ_PRIORITY_MID, /* 2 + 1 = 3 */
            APP_IRQ_PRIORITY_LOW_MID, /* 3 + 1 = 5 as 4 is used by softdevice */
            FAULT_IRQ_LEVEL, /* 4 used by softdevice */
            APP_IRQ_PRIORITY_LOW /* 5 + 1 = 6 */,
            APP_IRQ_PRIORITY_LOWEST, /* 6 + 1 = 7 */
    };
    return sd_next_irq_lut[prio];
#else
    return prio + 1;
#endif
}

#if NRF_LIBUARTE_ASYNC_WITH_APP_TIMER
static void app_timer_handler(void * p_context);
#define local_app_timer_cnt_get() app_timer_cnt_get()
#define local_app_timer_start(p_timer, ticks, p_context) app_timer_start(p_timer, ticks, p_context)
#define local_app_timer_stop(p_timer) app_timer_stop(p_timer)
#define local_app_timer_create(p_timer) app_timer_create(p_timer, APP_TIMER_MODE_SINGLE_SHOT, app_timer_handler)
#define local_app_timer_cnt_diff_compute(to, from) app_timer_cnt_diff_compute(to, from)
#else
#ifndef APP_TIMER_CONFIG_RTC_FREQUENCY
#define APP_TIMER_CONFIG_RTC_FREQUENCY 0
#endif

#ifndef APP_TIMER_CLOCK_FREQ
#define APP_TIMER_CLOCK_FREQ 1
#endif

#ifndef APP_TIMER_MIN_TIMEOUT_TICKS
#define APP_TIMER_MIN_TIMEOUT_TICKS 0
#endif

#ifndef APP_TIMER_CONFIG_IRQ_PRIORITY
#define APP_TIMER_CONFIG_IRQ_PRIORITY 1
#endif
static void app_timer_handler(void * p_context) __attribute__((unused));
#define local_app_timer_cnt_get() 0
#define local_app_timer_start(p_timer, ticks, p_context) NRF_SUCCESS
#define local_app_timer_stop(p_timer) NRF_SUCCESS
#define local_app_timer_create(p_timer) NRF_SUCCESS
#define local_app_timer_cnt_diff_compute(to, from) 0
#endif

static uint32_t app_timer_ticks_to_us(uint32_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000000 * (APP_TIMER_CONFIG_RTC_FREQUENCY + 1)) /
                      APP_TIMER_CLOCK_FREQ);
}

static uint32_t app_timer_us_to_ticks(uint32_t us)
{
    return (uint32_t)((((uint64_t)APP_TIMER_CLOCK_FREQ/(APP_TIMER_CONFIG_RTC_FREQUENCY + 1)) * us) /
                      1000000);
}

static bool rx_buffer_schedule(const nrf_libuarte_async_t * p_libuarte)
{
    uint8_t * p_data = nrf_balloc_alloc(p_libuarte->p_rx_pool);

    if (p_data == NULL)
    {
        return false;
    }

    ret_code_t ret = nrf_queue_push(p_libuarte->p_rx_queue, &p_data);
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("RX buffer queue full.");
        return false;
    }

    p_libuarte->p_ctrl_blk->alloc_cnt++;
    nrf_libuarte_drv_rx_buf_rsp(p_libuarte->p_libuarte, p_data, p_libuarte->rx_buf_size);

    return true;
}

static void uart_evt_handler(void * context, nrf_libuarte_drv_evt_t * p_evt)
{
    ret_code_t ret;
    const nrf_libuarte_async_t * p_libuarte = (const nrf_libuarte_async_t *)context;

    switch (p_evt->type)
    {
    case NRF_LIBUARTE_DRV_EVT_TX_DONE:
    {
        NRF_LOG_DEBUG("(evt) TX completed (%d)", p_evt->data.rxtx.length);
        nrf_libuarte_async_evt_t evt = {
            .type = NRF_LIBUARTE_ASYNC_EVT_TX_DONE,
            .data = {
                .rxtx = {
                    .p_data = p_evt->data.rxtx.p_data,
                    .length = p_evt->data.rxtx.length,
                }
            }
        };
        p_libuarte->p_ctrl_blk->evt_handler(p_libuarte->p_ctrl_blk->context, &evt);
        break;
    }
    case NRF_LIBUARTE_DRV_EVT_RX_BUF_REQ:
    {
        if (p_libuarte->p_ctrl_blk->rx_halted)
        {
            break;
        }

        if (rx_buffer_schedule(p_libuarte) == false)
        {
            if (p_libuarte->p_ctrl_blk->hwfc)
            {
                p_libuarte->p_ctrl_blk->rx_halted = true;
            }
            else
            {
                NRF_LOG_ERROR("(evt) Failed to allocate buffer for RX.");
                APP_ERROR_CHECK_BOOL(false);
            }
        }
        break;
    }
    case NRF_LIBUARTE_DRV_EVT_RX_DATA:
    {

        uint32_t rx_amount = p_evt->data.rxtx.length - p_libuarte->p_ctrl_blk->sub_rx_count;
        if (rx_amount)
        {
            p_libuarte->p_ctrl_blk->rx_count += rx_amount;
            nrf_libuarte_async_evt_t evt = {
                .type = NRF_LIBUARTE_ASYNC_EVT_RX_DATA,
                .data = {
                    .rxtx = {
                        .p_data = &p_evt->data.rxtx.p_data[p_libuarte->p_ctrl_blk->sub_rx_count],
                        .length = rx_amount,
                    }
                }
            };
            NRF_LOG_DEBUG("(evt) RX: %d (addr:0x%08X, internal index: %d)",
                          rx_amount,
                          p_evt->data.rxtx.p_data,
                          p_libuarte->p_ctrl_blk->sub_rx_count);

            p_libuarte->p_ctrl_blk->sub_rx_count = 0;

            if(p_evt->data.rxtx.p_data != p_libuarte->p_ctrl_blk->p_curr_rx_buf)
            {
                NRF_LOG_ERROR("(evt) RX buffer address mismatch");
            }

            ret = nrf_queue_pop(p_libuarte->p_rx_queue, &p_libuarte->p_ctrl_blk->p_curr_rx_buf);
            if (ret != NRF_SUCCESS)
            {
                NRF_LOG_ERROR("RX buffer queue empty.");
                APP_ERROR_CHECK_BOOL(false);
            }

            p_libuarte->p_ctrl_blk->evt_handler(p_libuarte->p_ctrl_blk->context, &evt);
        }
        else
        {
            NRF_LOG_ERROR("(evt) RX with 0 length: 0x%08X", p_evt->data.rxtx.p_data);
            //zero length packet is freed immediately and not forwarded to the application.
            APP_ERROR_CHECK_BOOL(false);
        }
        break;
    }
    case NRF_LIBUARTE_DRV_EVT_ERROR:
    {
        nrf_libuarte_async_evt_t evt = {
            .type = NRF_LIBUARTE_ASYNC_EVT_ERROR,
            .data = {
                .errorsrc = p_evt->data.errorsrc
            }
        };
        p_libuarte->p_ctrl_blk->evt_handler(p_libuarte->p_ctrl_blk->context, &evt);
        break;
    }
    case NRF_LIBUARTE_DRV_EVT_OVERRUN_ERROR:
    {
        NRF_LOG_WARNING("Overrun error - data loss due to UARTE interrupt not handled on time.");
        uint32_t rx_amount = p_evt->data.overrun_err.overrun_length - p_libuarte->p_ctrl_blk->sub_rx_count;
        p_libuarte->p_ctrl_blk->rx_count += rx_amount;
        nrf_libuarte_async_evt_t evt = {
            .type = NRF_LIBUARTE_ASYNC_EVT_OVERRUN_ERROR,
            .data = {
                .overrun_err = { .overrun_length = p_evt->data.overrun_err.overrun_length}
            }
        };
        p_libuarte->p_ctrl_blk->evt_handler(p_libuarte->p_ctrl_blk->context, &evt);
        break;
    }
    default:
        APP_ERROR_CHECK_BOOL(false);
        break;
    }
}

void nrf_libuarte_async_timeout_handler(const nrf_libuarte_async_t * p_libuarte)
{
    NRFX_IRQ_DISABLE((IRQn_Type)NRFX_IRQ_NUMBER_GET(p_libuarte->p_libuarte->uarte));

    uint32_t capt_rx_count = p_libuarte->p_libuarte->timer.p_reg->CC[3];

    if (capt_rx_count > p_libuarte->p_ctrl_blk->rx_count)
    {
        uint32_t rx_amount = capt_rx_count - p_libuarte->p_ctrl_blk->rx_count;
        nrf_libuarte_async_evt_t evt = {
            .type = NRF_LIBUARTE_ASYNC_EVT_RX_DATA,
            .data = {
                .rxtx = {
                    .p_data = &p_libuarte->p_ctrl_blk->p_curr_rx_buf[p_libuarte->p_ctrl_blk->sub_rx_count],
                    .length = rx_amount,
                }
            }
        };
        NRF_LOG_DEBUG("(tmr evt) RX: %d (addr:0x%08X, internal index: %d)",
                      rx_amount,
                      evt.data.rxtx.p_data,
                      p_libuarte->p_ctrl_blk->sub_rx_count);

        p_libuarte->p_ctrl_blk->sub_rx_count += rx_amount;
        p_libuarte->p_ctrl_blk->rx_count = capt_rx_count;
        p_libuarte->p_ctrl_blk->evt_handler(p_libuarte->p_ctrl_blk->context, &evt);
    }

    NRFX_IRQ_ENABLE((IRQn_Type)NRFX_IRQ_NUMBER_GET(p_libuarte->p_libuarte->uarte));
}

static void tmr_evt_handler(nrf_timer_event_t event_type, void * p_context)
{
    nrf_libuarte_async_timeout_handler((const nrf_libuarte_async_t *)p_context);
}

static void app_timer_handler(void * p_context)
{
    const nrf_libuarte_async_t * p_libuarte = p_context;
    uint32_t current_rx_count;
    uint32_t counter = local_app_timer_cnt_get();
    uint32_t ticks = app_timer_us_to_ticks(p_libuarte->p_ctrl_blk->timeout_us)/2;
    ticks = MAX(APP_TIMER_MIN_TIMEOUT_TICKS, ticks);

    if (p_libuarte->p_ctrl_blk->enabled == false)
    {
        return;
    }

    nrf_timer_task_trigger( p_libuarte->p_libuarte->timer.p_reg, NRF_TIMER_TASK_CAPTURE3);
    current_rx_count = p_libuarte->p_libuarte->timer.p_reg->CC[3];
    UNUSED_RETURN_VALUE(local_app_timer_start(*p_libuarte->p_app_timer, ticks, (void *)p_libuarte));

    if (p_libuarte->p_app_timer_ctrl_blk->rx_count != current_rx_count) {
        p_libuarte->p_app_timer_ctrl_blk->rx_count = current_rx_count;
       /* if number of bytes received changed reset timestamp and activate waiting
        * for silent period.
        */
        p_libuarte->p_app_timer_ctrl_blk->timestamp = counter;
        p_libuarte->p_app_timer_ctrl_blk->activate = true;
    } else {
        uint32_t diff;

        /* In case of detected silent period check if its length exceeds configured
         * timeout. If yes trigger timeout handler.
         */
        diff = local_app_timer_cnt_diff_compute(counter,
                                          p_libuarte->p_app_timer_ctrl_blk->timestamp);
        if (p_libuarte->p_app_timer_ctrl_blk->activate &&
            (app_timer_ticks_to_us(diff) > p_libuarte->p_ctrl_blk->timeout_us)) {
            p_libuarte->p_app_timer_ctrl_blk->activate = false;
            nrf_libuarte_async_timeout_handler(p_libuarte);
        }
    }
}

ret_code_t nrf_libuarte_async_init(const nrf_libuarte_async_t * const p_libuarte,
                                   nrf_libuarte_async_config_t const * p_config,
                                   nrf_libuarte_async_evt_handler_t evt_handler,
                                   void * context)
{
    ret_code_t ret;

    if (p_config->int_prio == APP_IRQ_PRIORITY_LOWEST ||
        ((p_libuarte->p_app_timer && NRF_LIBUARTE_ASYNC_WITH_APP_TIMER) &&
         (p_config->int_prio >= APP_TIMER_CONFIG_IRQ_PRIORITY))) {
        NRF_LOG_ERROR("Too low priority. Lowest possible priority is %d", APP_IRQ_PRIORITY_LOW);
        return NRF_ERROR_INVALID_PARAM;
    }

    if (p_libuarte->p_ctrl_blk->enabled)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    p_libuarte->p_ctrl_blk->evt_handler  = evt_handler;
    p_libuarte->p_ctrl_blk->rx_count     = 0;
    p_libuarte->p_ctrl_blk->p_curr_rx_buf = NULL;
    p_libuarte->p_ctrl_blk->rx_free_cnt  = 0;
    p_libuarte->p_ctrl_blk->sub_rx_count = 0;
    p_libuarte->p_ctrl_blk->alloc_cnt    = 0;
    p_libuarte->p_ctrl_blk->context = context;
    p_libuarte->p_ctrl_blk->timeout_us = p_config->timeout_us;
    p_libuarte->p_ctrl_blk->rx_halted = false;
    p_libuarte->p_ctrl_blk->hwfc = (p_config->hwfc == NRF_UARTE_HWFC_ENABLED);

    uint32_t i;

    uint32_t tmr_start_tsk = 0;
    uint32_t tmr_clear_tsk = 0;
    uint32_t tmr_stop_tsk = 0;
    uint32_t tmr_compare_evt = 0;

    if (p_libuarte->p_rtc && RTC_IN_USE)
    {
        nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
        rtc_config.interrupt_priority = irq_prio_inc(p_config->int_prio);
 
        rtc_config.prescaler = 0;
        ret = nrfx_rtc_init(p_libuarte->p_rtc, &rtc_config, p_libuarte->rtc_handler);
        if (ret != NRFX_SUCCESS)
        {
            return NRF_ERROR_INTERNAL;
        }

        ret = nrfx_rtc_cc_set(p_libuarte->p_rtc, 0, p_config->timeout_us/32, true);
        if (ret != NRFX_SUCCESS)
        {
            return NRF_ERROR_INTERNAL;
        }

        tmr_start_tsk = nrfx_rtc_task_address_get(p_libuarte->p_rtc, NRF_RTC_TASK_START);
        tmr_clear_tsk = nrfx_rtc_task_address_get(p_libuarte->p_rtc, NRF_RTC_TASK_CLEAR);
        tmr_stop_tsk = nrfx_rtc_task_address_get(p_libuarte->p_rtc, NRF_RTC_TASK_STOP);
        tmr_compare_evt = nrfx_rtc_event_address_get(p_libuarte->p_rtc, NRF_RTC_EVENT_COMPARE_0);
    }
    else if (p_libuarte->p_timer && TIMER_IN_USE)
    {
        nrfx_timer_config_t tmr_config = NRFX_TIMER_DEFAULT_CONFIG;
        tmr_config.frequency = NRF_TIMER_FREQ_1MHz;
        tmr_config.p_context = (void *)p_libuarte;
        tmr_config.interrupt_priority = irq_prio_inc(p_config->int_prio);

        ret = nrfx_timer_init(p_libuarte->p_timer, &tmr_config, tmr_evt_handler);
        if (ret != NRFX_SUCCESS)
        {
            return NRF_ERROR_INTERNAL;
        }
        nrfx_timer_compare(p_libuarte->p_timer, NRF_TIMER_CC_CHANNEL0, p_config->timeout_us, true);

        tmr_start_tsk = nrfx_timer_task_address_get(p_libuarte->p_timer, NRF_TIMER_TASK_START);
        tmr_clear_tsk = nrfx_timer_task_address_get(p_libuarte->p_timer, NRF_TIMER_TASK_CLEAR);
        tmr_stop_tsk = nrfx_timer_task_address_get(p_libuarte->p_timer, NRF_TIMER_TASK_SHUTDOWN);
        tmr_compare_evt = nrfx_timer_compare_event_address_get(p_libuarte->p_timer, 0);
    }
    else if (p_libuarte->p_app_timer && NRF_LIBUARTE_ASYNC_WITH_APP_TIMER) {
        /* app_timer in use */
        if(!p_libuarte->p_ctrl_blk->app_timer_created)
        {
            ret = local_app_timer_create(p_libuarte->p_app_timer);
            if (ret != NRF_SUCCESS)
            {
                return ret;
            }
            p_libuarte->p_ctrl_blk->app_timer_created = true;
        }
        p_libuarte->p_app_timer_ctrl_blk->activate = false;
        p_libuarte->p_app_timer_ctrl_blk->rx_count = 0;
        p_libuarte->p_app_timer_ctrl_blk->timestamp = 0;
    }
    else
    {
        NRF_LOG_ERROR("No timer or rtc defined");
        APP_ERROR_CHECK_BOOL(false);
        return NRF_ERROR_INTERNAL;
    }

    /* if RTC or TIMER is used then PPI channels are allocated. */
    if (p_libuarte->p_app_timer == NULL || !NRF_LIBUARTE_ASYNC_WITH_APP_TIMER)
    {
        for (i = 0; i < NRF_LIBUARTE_ASYNC_PPI_CH_MAX; i++)
        {
            ret = nrfx_ppi_channel_alloc(&p_libuarte->p_ctrl_blk->ppi_channels[i]);
            if (ret != NRFX_SUCCESS)
            {
                //we don't free already allocated channels, system is wrongly configured.
                return NRF_ERROR_INTERNAL;
            }
        }

        /*lint -save -e666 */
        PPI_CH_SETUP(p_libuarte->p_ctrl_blk->ppi_channels[NRF_LIBUARTE_ASYNC_PPI_CH_RXRDY_CLEAR],
                     nrf_uarte_event_address_get(p_libuarte->p_libuarte->uarte, NRF_UARTE_EVENT_RXDRDY),
                     tmr_start_tsk,
                     tmr_clear_tsk);

        PPI_CH_SETUP(p_libuarte->p_ctrl_blk->ppi_channels[NRF_LIBUARTE_ASYNC_PPI_CH_COMPARE_SHUTDOWN],
                     tmr_compare_evt,
                     tmr_stop_tsk,
                     (uint32_t)&p_libuarte->p_libuarte->timer.p_reg->TASKS_CAPTURE[3]);
        /*lint -restore */
    }

    nrf_libuarte_drv_config_t uart_config = {
        .tx_pin        = p_config->tx_pin,
        .rx_pin        = p_config->rx_pin,
        .cts_pin       = p_config->cts_pin,
        .rts_pin       = p_config->rts_pin,
        .startrx_evt   = nrf_uarte_event_address_get(p_libuarte->p_libuarte->uarte, NRF_UARTE_EVENT_ENDRX),
        .endrx_evt     = 0,
        .rxstarted_tsk = 0,
        .rxdone_tsk    = 0,
        .hwfc          = p_config->hwfc,
        .parity        = p_config->parity,
        .baudrate      = p_config->baudrate,
        .irq_priority  = p_config->int_prio,
        .pullup_rx     = p_config->pullup_rx,
    };

    ret = nrf_libuarte_drv_init(p_libuarte->p_libuarte, &uart_config, uart_evt_handler, (void *)p_libuarte);
    if (ret != NRF_SUCCESS)
    {
        return ret;
    }

    ret = nrf_balloc_init(p_libuarte->p_rx_pool);
    if (ret != NRF_SUCCESS)
    {
        return ret;
    }

    nrf_queue_reset(p_libuarte->p_rx_queue);
    p_libuarte->p_ctrl_blk->enabled = true;

    return ret;
}

void nrf_libuarte_async_uninit(const nrf_libuarte_async_t * const p_libuarte)
{
    if (p_libuarte->p_ctrl_blk->enabled == false)
    {
        return;
    }

    p_libuarte->p_ctrl_blk->enabled = false;

    /* if HW timeout was used */
    if (p_libuarte->p_app_timer == NULL || !NRF_LIBUARTE_ASYNC_WITH_APP_TIMER)
    {
        uint32_t i;
        ret_code_t ret;
        for (i = 0; i < NRF_LIBUARTE_ASYNC_PPI_CH_MAX; i++)
        {
            ret = nrfx_ppi_channel_disable(p_libuarte->p_ctrl_blk->ppi_channels[i]);
            ASSERT(ret == NRF_SUCCESS)
            ret = nrfx_ppi_channel_free(p_libuarte->p_ctrl_blk->ppi_channels[i]);
            ASSERT(ret == NRF_SUCCESS)
        }
    }

    if (p_libuarte->p_rtc && RTC_IN_USE)
    {
        nrfx_rtc_disable(p_libuarte->p_rtc);
        nrfx_rtc_uninit(p_libuarte->p_rtc);
    }
    else if (p_libuarte->p_timer && TIMER_IN_USE)
    {
        nrfx_timer_disable(p_libuarte->p_timer);
        nrfx_timer_uninit(p_libuarte->p_timer);
    }
    else if (p_libuarte->p_app_timer && NRF_LIBUARTE_ASYNC_WITH_APP_TIMER)
    {
        UNUSED_RETURN_VALUE(local_app_timer_stop(*p_libuarte->p_app_timer));
    }

    nrf_libuarte_drv_uninit(p_libuarte->p_libuarte);
}

void nrf_libuarte_async_enable(const nrf_libuarte_async_t * const p_libuarte)
{
    uint8_t * p_data;
    p_data = nrf_balloc_alloc(p_libuarte->p_rx_pool);
    p_libuarte->p_ctrl_blk->alloc_cnt++;
    if (p_data == NULL)
    {
        APP_ERROR_CHECK_BOOL(false);
    }

    if (p_libuarte->p_rtc && RTC_IN_USE)
    {
        nrfx_rtc_counter_clear(p_libuarte->p_rtc);
    }
    else if (p_libuarte->p_timer && TIMER_IN_USE)
    {
        nrfx_timer_clear(p_libuarte->p_timer);
    }

    if (!(p_libuarte->p_app_timer && NRF_LIBUARTE_ASYNC_WITH_APP_TIMER))
    {
        nrfx_err_t err;

        err = nrfx_ppi_channel_enable(p_libuarte->p_ctrl_blk->ppi_channels[NRF_LIBUARTE_ASYNC_PPI_CH_RXRDY_CLEAR]);
        APP_ERROR_CHECK_BOOL(err == NRFX_SUCCESS);
        err = nrfx_ppi_channel_enable(p_libuarte->p_ctrl_blk->ppi_channels[NRF_LIBUARTE_ASYNC_PPI_CH_COMPARE_SHUTDOWN]);
        APP_ERROR_CHECK_BOOL(err == NRFX_SUCCESS);
    }

    p_libuarte->p_ctrl_blk->p_curr_rx_buf = p_data;
    ret_code_t ret =  nrf_libuarte_drv_rx_start(p_libuarte->p_libuarte, p_data, p_libuarte->rx_buf_size, false);
    APP_ERROR_CHECK_BOOL(ret == NRF_SUCCESS);

    if (p_libuarte->p_app_timer && NRF_LIBUARTE_ASYNC_WITH_APP_TIMER)
    {
        uint32_t ticks = app_timer_us_to_ticks(p_libuarte->p_ctrl_blk->timeout_us)/2;
        ticks = MAX(APP_TIMER_MIN_TIMEOUT_TICKS, ticks);
        UNUSED_RETURN_VALUE(local_app_timer_start(*p_libuarte->p_app_timer, ticks, (void *)p_libuarte));
    }
}

ret_code_t nrf_libuarte_async_tx(const nrf_libuarte_async_t * const p_libuarte, uint8_t * p_data, size_t length)
{
    return nrf_libuarte_drv_tx(p_libuarte->p_libuarte, p_data, length);
}

void nrf_libuarte_async_rx_free(const nrf_libuarte_async_t * const p_libuarte, uint8_t * p_data, size_t length)
{
    p_libuarte->p_ctrl_blk->rx_free_cnt += length;
    if (p_libuarte->p_ctrl_blk->rx_free_cnt == p_libuarte->rx_buf_size)
    {
        p_data -= (p_libuarte->p_ctrl_blk->rx_free_cnt - length);
        p_libuarte->p_ctrl_blk->rx_free_cnt = 0;
        nrf_balloc_free(p_libuarte->p_rx_pool, p_data);

        p_libuarte->p_ctrl_blk->alloc_cnt--;
        if (p_libuarte->p_ctrl_blk->alloc_cnt<0)
        {
            NRF_LOG_ERROR("Freeing more RX buffers than allocated.");
            APP_ERROR_CHECK_BOOL(false);
        }
        NRF_LOG_INFO("Freeing full buffer 0x%08X, %d, (currently allocated:%d).",p_data, length, p_libuarte->p_ctrl_blk->alloc_cnt);

        if (p_libuarte->p_ctrl_blk->rx_halted)
        {
            bool ret = rx_buffer_schedule(p_libuarte);
            ASSERT(ret);
            p_libuarte->p_ctrl_blk->rx_halted = false;
        }
    }
    else if (p_libuarte->p_ctrl_blk->rx_free_cnt > p_libuarte->rx_buf_size)
    {
        NRF_LOG_ERROR("Unexpected RX free input parameter.");
        APP_ERROR_CHECK_BOOL(false);
    }
    else
    {
        NRF_LOG_INFO("Freeing partial buffer: 0x%08X, length:%d", p_data, length);
    }

}

void nrf_libuarte_async_rts_clear(const nrf_libuarte_async_t * const p_libuarte)
{
    nrf_libuarte_drv_rts_clear(p_libuarte->p_libuarte);
}

void nrf_libuarte_async_rts_set(const nrf_libuarte_async_t * const p_libuarte)
{
    nrf_libuarte_drv_rts_set(p_libuarte->p_libuarte);
}
