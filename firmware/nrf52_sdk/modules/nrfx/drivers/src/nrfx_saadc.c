/**
 * Copyright (c) 2015 - 2021, Nordic Semiconductor ASA
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

#if NRFX_CHECK(NRFX_SAADC_ENABLED)
#include <nrfx_saadc.h>

#define NRFX_LOG_MODULE SAADC
#include <nrfx_log.h>

#if !defined(NRFX_SAADC_API_V2)

#define EVT_TO_STR(event)                                                       \
    (event == NRF_SAADC_EVENT_STARTED       ? "NRF_SAADC_EVENT_STARTED"       : \
    (event == NRF_SAADC_EVENT_END           ? "NRF_SAADC_EVENT_END"           : \
    (event == NRF_SAADC_EVENT_DONE          ? "NRF_SAADC_EVENT_DONE"          : \
    (event == NRF_SAADC_EVENT_RESULTDONE    ? "NRF_SAADC_EVENT_RESULTDONE"    : \
    (event == NRF_SAADC_EVENT_CALIBRATEDONE ? "NRF_SAADC_EVENT_CALIBRATEDONE" : \
    (event == NRF_SAADC_EVENT_STOPPED       ? "NRF_SAADC_EVENT_STOPPED"       : \
                                              "UNKNOWN EVENT"))))))


typedef enum
{
    NRF_SAADC_STATE_IDLE        = 0,
    NRF_SAADC_STATE_BUSY        = 1,
    NRF_SAADC_STATE_CALIBRATION = 2
} nrf_saadc_state_t;


typedef struct
{
    nrf_saadc_input_t pselp;
    nrf_saadc_input_t pseln;
} nrf_saadc_psel_buffer;

/** @brief SAADC control block.*/
typedef struct
{
    nrfx_saadc_event_handler_t    event_handler;                 ///< Event handler function pointer.
    volatile nrf_saadc_value_t  * p_buffer;                      ///< Sample buffer.
    volatile uint16_t             buffer_size;                   ///< Size of the sample buffer.
    volatile nrf_saadc_value_t  * p_secondary_buffer;            ///< Secondary sample buffer.
    volatile nrf_saadc_state_t    adc_state;                     ///< State of the SAADC.
    uint32_t                      limits_enabled_flags;          ///< Enabled limits flags.
    uint16_t                      secondary_buffer_size;         ///< Size of the secondary buffer.
    uint16_t                      buffer_size_left;              ///< When low power mode is active indicates how many samples left to convert on current buffer.
    nrf_saadc_psel_buffer         psel[NRF_SAADC_CHANNEL_COUNT]; ///< Pin configurations of SAADC channels.
    nrfx_drv_state_t              state;                         ///< Driver initialization state.
    uint8_t                       active_channels;               ///< Number of enabled SAADC channels.
    bool                          low_power_mode;                ///< Indicates if low power mode is active.
    bool                          conversions_end;               ///< When low power mode is active indicates end of conversions on current buffer.
} nrfx_saadc_cb_t;

static nrfx_saadc_cb_t m_cb;

#define LOW_LIMIT_TO_FLAG(channel)      ((2 * channel + 1))
#define HIGH_LIMIT_TO_FLAG(channel)     ((2 * channel))
#define FLAG_IDX_TO_EVENT(idx)          ((nrf_saadc_event_t)((uint32_t)NRF_SAADC_EVENT_CH0_LIMITH + \
                                            4 * idx))
#define LIMIT_EVENT_TO_CHANNEL(event)   (uint8_t)(((uint32_t)event - \
                                            (uint32_t)NRF_SAADC_EVENT_CH0_LIMITH) / 8)
#define LIMIT_EVENT_TO_LIMIT_TYPE(event)((((uint32_t)event - (uint32_t)NRF_SAADC_EVENT_CH0_LIMITH) & 4) \
                                            ? NRF_SAADC_LIMIT_LOW : NRF_SAADC_LIMIT_HIGH)
#define HW_TIMEOUT 10000

void nrfx_saadc_irq_handler(void)
{
    if (nrf_saadc_event_check(NRF_SAADC_EVENT_END))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_SAADC_EVENT_END));

        if (!m_cb.low_power_mode || m_cb.conversions_end)
        {
            nrfx_saadc_evt_t evt;
            evt.type               = NRFX_SAADC_EVT_DONE;
            evt.data.done.p_buffer = (nrf_saadc_value_t *)m_cb.p_buffer;
            evt.data.done.size     = m_cb.buffer_size;

            if (m_cb.p_secondary_buffer == NULL)
            {
                m_cb.adc_state = NRF_SAADC_STATE_IDLE;
            }
            else
            {
                m_cb.buffer_size_left   = m_cb.secondary_buffer_size;
                m_cb.p_buffer           = m_cb.p_secondary_buffer;
                m_cb.buffer_size        = m_cb.secondary_buffer_size;
                m_cb.p_secondary_buffer = NULL;
                if (!m_cb.low_power_mode)
                {
                    nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
                }
            }
            m_cb.event_handler(&evt);
            m_cb.conversions_end = false;
        }
    }
    if (m_cb.low_power_mode && nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_SAADC_EVENT_STARTED));

        if (m_cb.buffer_size_left > m_cb.active_channels)
        {
            // More samples to convert than for single event.
            m_cb.buffer_size_left -= m_cb.active_channels;
            nrf_saadc_buffer_init((nrf_saadc_value_t *)&m_cb.p_buffer[m_cb.buffer_size -
                                                                      m_cb.buffer_size_left],
                                  m_cb.active_channels);
        }
        else if ((m_cb.buffer_size_left == m_cb.active_channels) &&

                 (m_cb.p_secondary_buffer != NULL))
        {
            // Samples to convert for one event, prepare next buffer.
            m_cb.conversions_end  = true;
            m_cb.buffer_size_left = 0;
            nrf_saadc_buffer_init((nrf_saadc_value_t *)m_cb.p_secondary_buffer,
                                  m_cb.active_channels);
        }
        else if (m_cb.buffer_size_left == m_cb.active_channels)
        {
            // Samples to convert for one event, but no second buffer.
            m_cb.conversions_end  = true;
            m_cb.buffer_size_left = 0;
        }
        nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
        nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
    }
    if (nrf_saadc_event_check(NRF_SAADC_EVENT_CALIBRATEDONE))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_CALIBRATEDONE);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_SAADC_EVENT_CALIBRATEDONE));
        m_cb.adc_state = NRF_SAADC_STATE_IDLE;

        nrfx_saadc_evt_t evt;
        evt.type = NRFX_SAADC_EVT_CALIBRATEDONE;
        m_cb.event_handler(&evt);
    }
    if (nrf_saadc_event_check(NRF_SAADC_EVENT_STOPPED))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_SAADC_EVENT_STOPPED));
        m_cb.adc_state = NRF_SAADC_STATE_IDLE;
    }
    else
    {
        uint32_t          limit_flags = m_cb.limits_enabled_flags;
        uint32_t          flag_idx;
        nrf_saadc_event_t event;

        while (limit_flags)
        {
            flag_idx     = __CLZ(limit_flags);
            limit_flags &= ~((1UL << 31) >> flag_idx);
            event        = FLAG_IDX_TO_EVENT(flag_idx);
            if (nrf_saadc_event_check(event))
            {
                nrf_saadc_event_clear(event);
                nrfx_saadc_evt_t evt;
                evt.type                  = NRFX_SAADC_EVT_LIMIT;
                evt.data.limit.channel    = LIMIT_EVENT_TO_CHANNEL(event);
                evt.data.limit.limit_type = LIMIT_EVENT_TO_LIMIT_TYPE(event);
                NRFX_LOG_DEBUG("Event limit, channel: %d, limit type: %d.",
                               evt.data.limit.channel,
                               evt.data.limit.limit_type);
                m_cb.event_handler(&evt);
            }
        }
    }
}


nrfx_err_t nrfx_saadc_init(nrfx_saadc_config_t const * p_config,
                           nrfx_saadc_event_handler_t  event_handler)
{
    NRFX_ASSERT(p_config);
    NRFX_ASSERT(event_handler);
    nrfx_err_t err_code;

    if (m_cb.state != NRFX_DRV_STATE_UNINITIALIZED)
    {
        err_code = NRFX_ERROR_INVALID_STATE;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }

    m_cb.event_handler = event_handler;
    nrf_saadc_resolution_set(p_config->resolution);
    nrf_saadc_oversample_set(p_config->oversample);
    m_cb.low_power_mode       = p_config->low_power_mode;
    m_cb.state                = NRFX_DRV_STATE_INITIALIZED;
    m_cb.adc_state            = NRF_SAADC_STATE_IDLE;
    m_cb.active_channels      = 0;
    m_cb.limits_enabled_flags = 0;
    m_cb.conversions_end      = false;

    nrf_saadc_int_disable(NRF_SAADC_INT_ALL);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);
    NRFX_IRQ_PRIORITY_SET(SAADC_IRQn, p_config->interrupt_priority);
    NRFX_IRQ_ENABLE(SAADC_IRQn);
    nrf_saadc_int_enable(NRF_SAADC_INT_END);

    if (m_cb.low_power_mode)
    {
        nrf_saadc_int_enable(NRF_SAADC_INT_STARTED);
    }

    nrf_saadc_enable();

    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));

    return err_code;
}


void nrfx_saadc_uninit(void)
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);

    nrf_saadc_int_disable(NRF_SAADC_INT_ALL);
    NRFX_IRQ_DISABLE(SAADC_IRQn);
    nrf_saadc_task_trigger(NRF_SAADC_TASK_STOP);

    // Wait for ADC being stopped.
    bool result;
    NRFX_WAIT_FOR(nrf_saadc_event_check(NRF_SAADC_EVENT_STOPPED), HW_TIMEOUT, 0, result);
    NRFX_ASSERT(result);

    nrf_saadc_disable();
    m_cb.adc_state = NRF_SAADC_STATE_IDLE;

    for (uint32_t channel = 0; channel < NRF_SAADC_CHANNEL_COUNT; ++channel)
    {
        if (m_cb.psel[channel].pselp != NRF_SAADC_INPUT_DISABLED)
        {
            nrfx_err_t err_code = nrfx_saadc_channel_uninit(channel);
            NRFX_ASSERT(err_code == NRFX_SUCCESS);
        }
    }

    m_cb.state = NRFX_DRV_STATE_UNINITIALIZED;
}


nrfx_err_t nrfx_saadc_channel_init(uint8_t                                  channel,
                                   nrf_saadc_channel_config_t const * const p_config)
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);
    NRFX_ASSERT(channel < NRF_SAADC_CHANNEL_COUNT);
    // Oversampling can be used only with one channel.
    NRFX_ASSERT((nrf_saadc_oversample_get() == NRF_SAADC_OVERSAMPLE_DISABLED) ||
                (m_cb.active_channels == 0));

#if defined(SAADC_CH_PSELP_PSELP_VDDHDIV5)
    NRFX_ASSERT((p_config->pin_p <= NRF_SAADC_INPUT_VDDHDIV5) &&
                (p_config->pin_p > NRF_SAADC_INPUT_DISABLED));
    NRFX_ASSERT(p_config->pin_n <= NRF_SAADC_INPUT_VDDHDIV5);
#else
    NRFX_ASSERT((p_config->pin_p <= NRF_SAADC_INPUT_VDD) &&
                (p_config->pin_p > NRF_SAADC_INPUT_DISABLED));
    NRFX_ASSERT(p_config->pin_n <= NRF_SAADC_INPUT_VDD);
#endif

    nrfx_err_t err_code;

    // A channel can only be initialized if the driver is in the idle state.
    if (m_cb.adc_state != NRF_SAADC_STATE_IDLE)
    {
        err_code = NRFX_ERROR_BUSY;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }

#ifdef NRF52_PAN_74
    if ((p_config->acq_time == NRF_SAADC_ACQTIME_3US) ||
        (p_config->acq_time == NRF_SAADC_ACQTIME_5US))
    {
        nrf_saadc_disable();
    }
#endif //NRF52_PAN_74

    if (m_cb.psel[channel].pselp == NRF_SAADC_INPUT_DISABLED)
    {
        ++m_cb.active_channels;
    }
    m_cb.psel[channel].pselp = p_config->pin_p;
    m_cb.psel[channel].pseln = p_config->pin_n;
    nrf_saadc_channel_init(channel, p_config);

#ifdef NRF52_PAN_74
    if ((p_config->acq_time == NRF_SAADC_ACQTIME_3US) ||
        (p_config->acq_time == NRF_SAADC_ACQTIME_5US))
    {
        nrf_saadc_enable();
    }
#endif //NRF52_PAN_74

    NRFX_LOG_INFO("Channel initialized: %d.", channel);
    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


nrfx_err_t nrfx_saadc_channel_uninit(uint8_t channel)
{
    NRFX_ASSERT(channel < NRF_SAADC_CHANNEL_COUNT);
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);

    nrfx_err_t err_code;

    // A channel can only be uninitialized if the driver is in the idle state.
    if (m_cb.adc_state != NRF_SAADC_STATE_IDLE)
    {
        err_code = NRFX_ERROR_BUSY;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }

    if (m_cb.psel[channel].pselp != NRF_SAADC_INPUT_DISABLED)
    {
        --m_cb.active_channels;
    }
    m_cb.psel[channel].pselp = NRF_SAADC_INPUT_DISABLED;
    m_cb.psel[channel].pseln = NRF_SAADC_INPUT_DISABLED;
    nrf_saadc_channel_input_set(channel, NRF_SAADC_INPUT_DISABLED, NRF_SAADC_INPUT_DISABLED);
    nrfx_saadc_limits_set(channel, NRFX_SAADC_LIMITL_DISABLED, NRFX_SAADC_LIMITH_DISABLED);
    NRFX_LOG_INFO("Channel denitialized: %d.", channel);

    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


uint32_t nrfx_saadc_sample_task_get(void)
{
    return nrf_saadc_task_address_get(
                m_cb.low_power_mode ? NRF_SAADC_TASK_START : NRF_SAADC_TASK_SAMPLE);
}


nrfx_err_t nrfx_saadc_sample_convert(uint8_t channel, nrf_saadc_value_t * p_value)
{
    nrfx_err_t err_code;

    if (m_cb.adc_state != NRF_SAADC_STATE_IDLE)
    {
        err_code = NRFX_ERROR_BUSY;
        NRFX_LOG_WARNING("Function: %s error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }
    m_cb.adc_state = NRF_SAADC_STATE_BUSY;
    nrf_saadc_int_disable(NRF_SAADC_INT_STARTED | NRF_SAADC_INT_END);
    nrf_saadc_buffer_init(p_value, 1);
    if (m_cb.active_channels > 1)
    {
        for (uint32_t i = 0; i < NRF_SAADC_CHANNEL_COUNT; ++i)
        {
            nrf_saadc_channel_input_set(i, NRF_SAADC_INPUT_DISABLED, NRF_SAADC_INPUT_DISABLED);
        }
    }
    nrf_saadc_channel_input_set(channel, m_cb.psel[channel].pselp, m_cb.psel[channel].pseln);
    nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
    nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);

    bool result;
    NRFX_WAIT_FOR(nrf_saadc_event_check(NRF_SAADC_EVENT_END), HW_TIMEOUT, 0, result);
    NRFX_ASSERT(result);

    nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_END);

    NRFX_LOG_INFO("Conversion value: %d, channel %d.", *p_value, channel);

    if (m_cb.active_channels > 1)
    {
        for (uint32_t i = 0; i < NRF_SAADC_CHANNEL_COUNT; ++i)
        {
            nrf_saadc_channel_input_set(i, m_cb.psel[i].pselp, m_cb.psel[i].pseln);
        }
    }

    if (m_cb.low_power_mode)
    {
        nrf_saadc_int_enable(NRF_SAADC_INT_STARTED | NRF_SAADC_INT_END);
    }
    else
    {
        nrf_saadc_int_enable(NRF_SAADC_INT_END);
    }

    m_cb.adc_state = NRF_SAADC_STATE_IDLE;

    err_code = NRFX_SUCCESS;
    NRFX_LOG_WARNING("Function: %s, error code: %s.",
                     __func__,
                     NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


nrfx_err_t nrfx_saadc_buffer_convert(nrf_saadc_value_t * p_buffer, uint16_t size)
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);
    NRFX_ASSERT((size % m_cb.active_channels) == 0);
    nrfx_err_t err_code;

    nrf_saadc_int_disable(NRF_SAADC_INT_END | NRF_SAADC_INT_CALIBRATEDONE);
    if (m_cb.adc_state == NRF_SAADC_STATE_CALIBRATION)
    {
        nrf_saadc_int_enable(NRF_SAADC_INT_END | NRF_SAADC_INT_CALIBRATEDONE);
        err_code = NRFX_ERROR_BUSY;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }
    if (m_cb.adc_state == NRF_SAADC_STATE_BUSY)
    {
        if ( m_cb.p_secondary_buffer)
        {
            nrf_saadc_int_enable(NRF_SAADC_INT_END);
            err_code = NRFX_ERROR_BUSY;
            NRFX_LOG_WARNING("Function: %s, error code: %s.",
                             __func__,
                             NRFX_LOG_ERROR_STRING_GET(err_code));
            return err_code;
        }
        else
        {
            m_cb.p_secondary_buffer    = p_buffer;
            m_cb.secondary_buffer_size = size;
            if (!m_cb.low_power_mode)
            {
                while (nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED) == 0);
                nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
                nrf_saadc_buffer_init(p_buffer, size);
            }
            nrf_saadc_int_enable(NRF_SAADC_INT_END);
            err_code = NRFX_SUCCESS;
            NRFX_LOG_WARNING("Function: %s, error code: %s.",
                             __func__,
                             NRFX_LOG_ERROR_STRING_GET(err_code));
            return err_code;
        }
    }
    nrf_saadc_int_enable(NRF_SAADC_INT_END);
    m_cb.adc_state = NRF_SAADC_STATE_BUSY;

    m_cb.p_buffer           = p_buffer;
    m_cb.buffer_size        = size;
    m_cb.p_secondary_buffer = NULL;

    NRFX_LOG_INFO("Function: %s, buffer length: %d, active channels: %d.",
                  __func__,
                  size,
                  m_cb.active_channels);

    if (m_cb.low_power_mode)
    {
        m_cb.buffer_size_left = size;
        nrf_saadc_buffer_init(p_buffer, m_cb.active_channels);
    }
    else
    {
        nrf_saadc_buffer_init(p_buffer, size);
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
        nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
    }

    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


nrfx_err_t nrfx_saadc_sample()
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);

    nrfx_err_t err_code = NRFX_SUCCESS;
    if (m_cb.adc_state != NRF_SAADC_STATE_BUSY)
    {
        err_code = NRFX_ERROR_INVALID_STATE;
    }
    else if (m_cb.low_power_mode)
    {
        nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
    }
    else
    {
        nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
    }

    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


nrfx_err_t nrfx_saadc_calibrate_offset()
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);

    nrfx_err_t err_code;

    if (m_cb.adc_state != NRF_SAADC_STATE_IDLE)
    {
        err_code = NRFX_ERROR_BUSY;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }

    m_cb.adc_state = NRF_SAADC_STATE_CALIBRATION;

    nrf_saadc_event_clear(NRF_SAADC_EVENT_CALIBRATEDONE);
    nrf_saadc_int_enable(NRF_SAADC_INT_CALIBRATEDONE);
    nrf_saadc_task_trigger(NRF_SAADC_TASK_CALIBRATEOFFSET);
    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.",
                  __func__,
                  NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}


bool nrfx_saadc_is_busy(void)
{
    return (m_cb.adc_state != NRF_SAADC_STATE_IDLE);
}


void nrfx_saadc_abort(void)
{
    if (nrfx_saadc_is_busy())
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);
        nrf_saadc_int_enable(NRF_SAADC_INT_STOPPED);
        nrf_saadc_task_trigger(NRF_SAADC_TASK_STOP);

        if (m_cb.adc_state == NRF_SAADC_STATE_CALIBRATION)
        {
            m_cb.adc_state = NRF_SAADC_STATE_IDLE;
        }
        else
        {
            // Wait for ADC being stopped.
            bool result;
            NRFX_WAIT_FOR((m_cb.adc_state == NRF_SAADC_STATE_IDLE), HW_TIMEOUT, 0, result);
            NRFX_ASSERT(result);
        }

        nrf_saadc_int_disable(NRF_SAADC_INT_STOPPED);

        m_cb.p_buffer           = 0;
        m_cb.p_secondary_buffer = 0;
        NRFX_LOG_INFO("Conversion aborted.");
    }
}


void nrfx_saadc_limits_set(uint8_t channel, int16_t limit_low, int16_t limit_high)
{
    NRFX_ASSERT(m_cb.state != NRFX_DRV_STATE_UNINITIALIZED);
    NRFX_ASSERT(m_cb.event_handler); // only non blocking mode supported
    NRFX_ASSERT(limit_low >= NRFX_SAADC_LIMITL_DISABLED);
    NRFX_ASSERT(limit_high <= NRFX_SAADC_LIMITH_DISABLED);
    NRFX_ASSERT(limit_low < limit_high);
    nrf_saadc_channel_limits_set(channel, limit_low, limit_high);

    uint32_t int_mask = nrf_saadc_limit_int_get(channel, NRF_SAADC_LIMIT_LOW);
    if (limit_low == NRFX_SAADC_LIMITL_DISABLED)
    {
        m_cb.limits_enabled_flags &= ~(0x80000000 >> LOW_LIMIT_TO_FLAG(channel));
        nrf_saadc_int_disable(int_mask);
    }
    else
    {
        m_cb.limits_enabled_flags |= (0x80000000 >> LOW_LIMIT_TO_FLAG(channel));
        nrf_saadc_int_enable(int_mask);
    }

    int_mask = nrf_saadc_limit_int_get(channel, NRF_SAADC_LIMIT_HIGH);
    if (limit_high == NRFX_SAADC_LIMITH_DISABLED)
    {
        m_cb.limits_enabled_flags &= ~(0x80000000 >> HIGH_LIMIT_TO_FLAG(channel));
        nrf_saadc_int_disable(int_mask);
    }
    else
    {
        m_cb.limits_enabled_flags |= (0x80000000 >> HIGH_LIMIT_TO_FLAG(channel));
        nrf_saadc_int_enable(int_mask);
    }
}
#endif // !defined(NRFX_SAADC_API_V2)

#if defined(NRFX_SAADC_API_V2) || defined(__NRFX_DOXYGEN__)

#if defined(NRF52_SERIES) && !defined(USE_WORKAROUND_FOR_ANOMALY_212)
    // ANOMALY 212 - SAADC events are missing when switching from single channel
    //               to multi channel configuration with burst enabled.
    #define USE_WORKAROUND_FOR_ANOMALY_212 1
#endif

#if defined(NRF53_SERIES) || defined(NRF91_SERIES)
    // Make sure that SAADC is stopped before channel configuration.
    #define STOP_SAADC_ON_CHANNEL_CONFIG 1

    // Make sure that SAADC calibration samples do not affect next conversions.
    #define INTERCEPT_SAADC_CALIBRATION_SAMPLES 1
#endif

/** @brief SAADC driver states.*/
typedef enum
{
    NRF_SAADC_STATE_UNINITIALIZED = 0,
    NRF_SAADC_STATE_IDLE,
    NRF_SAADC_STATE_SIMPLE_MODE,
    NRF_SAADC_STATE_SIMPLE_MODE_SAMPLE,
    NRF_SAADC_STATE_ADV_MODE,
    NRF_SAADC_STATE_ADV_MODE_SAMPLE,
    NRF_SAADC_STATE_ADV_MODE_SAMPLE_STARTED,
    NRF_SAADC_STATE_CALIBRATION
} nrf_saadc_state_t;

/** @brief SAADC control block.*/
typedef struct
{
    nrfx_saadc_event_handler_t event_handler;                ///< Event handler function pointer.
    nrf_saadc_value_t *        p_buffer_primary;             ///< Pointer to the primary result buffer.
    nrf_saadc_value_t *        p_buffer_secondary;           ///< Pointer to the secondary result buffer.
#if NRFX_CHECK(INTERCEPT_SAADC_CALIBRATION_SAMPLES)
    nrf_saadc_value_t          calib_samples[6];             ///< Scratch buffer for calibration samples.
#endif
    uint16_t                   size_primary;                 ///< Size of the primary result buffer.
    uint16_t                   size_secondary;               ///< Size of the secondary result buffer.
    uint16_t                   samples_converted;            ///< Number of samples present in result buffer when in the blocking mode.
    nrf_saadc_input_t          channels_pselp[SAADC_CH_NUM]; ///< Array holding each channel positive input.
    nrf_saadc_input_t          channels_pseln[SAADC_CH_NUM]; ///< Array holding each channel negative input.
    nrf_saadc_state_t          saadc_state;                  ///< State of the SAADC driver.
    uint8_t                    channels_configured;          ///< Bitmask of the configured channels.
    uint8_t                    channels_activated;           ///< Bitmask of the activated channels.
    uint8_t                    channels_activated_count;     ///< Number of the activated channels.
    uint8_t                    limits_low_activated;         ///< Bitmask of the activated low limits.
    uint8_t                    limits_high_activated;        ///< Bitmask of the activated high limits.
    bool                       start_on_end;                 ///< Flag indicating if the START task is to be triggered on the END event.
    bool                       oversampling_without_burst;   ///< Flag indicating whether oversampling without burst is configured.
} nrfx_saadc_cb_t;

static nrfx_saadc_cb_t m_cb;

#if NRFX_CHECK(USE_WORKAROUND_FOR_ANOMALY_212)
static void saadc_anomaly_212_workaround_apply(void)
{
    uint32_t c[SAADC_CH_NUM];
    uint32_t l[SAADC_CH_NUM];

    for (uint32_t i = 0; i < SAADC_CH_NUM; i++)
    {
        c[i] = NRF_SAADC->CH[i].CONFIG;
        l[i] = NRF_SAADC->CH[i].LIMIT;
    }
    nrf_saadc_resolution_t resolution = nrf_saadc_resolution_get();
    uint32_t u640 = *(volatile uint32_t *)0x40007640;
    uint32_t u644 = *(volatile uint32_t *)0x40007644;
    uint32_t u648 = *(volatile uint32_t *)0x40007648;

    *(volatile uint32_t *)0x40007FFC = 0;
    *(volatile uint32_t *)0x40007FFC = 1;

    for (uint32_t i = 0; i < SAADC_CH_NUM; i++)
    {
        NRF_SAADC->CH[i].CONFIG = c[i];
        NRF_SAADC->CH[i].LIMIT = l[i];
    }
    *(volatile uint32_t *)0x40007640 = u640;
    *(volatile uint32_t *)0x40007644 = u644;
    *(volatile uint32_t *)0x40007648 = u648;
    nrf_saadc_resolution_set(resolution);
}
#endif // NRFX_CHECK(USE_WORKAROUND_FOR_ANOMALY_212)

static nrfx_err_t saadc_channel_count_get(uint32_t  ch_to_activate_mask,
                                          uint8_t * p_active_ch_count)
{
    NRFX_ASSERT(ch_to_activate_mask);
    NRFX_ASSERT(ch_to_activate_mask < (1uL << SAADC_CH_NUM));

    uint8_t active_ch_count = 0;
    for (uint32_t ch_mask = 1; ch_mask < (1uL << SAADC_CH_NUM); ch_mask <<= 1)
    {
        if (ch_to_activate_mask & ch_mask)
        {
            // Check if requested channels are configured.
            if (!(m_cb.channels_configured & ch_mask))
            {
                return NRFX_ERROR_INVALID_PARAM;
            }
            active_ch_count++;
        }
    }

    *p_active_ch_count = active_ch_count;
    return NRFX_SUCCESS;
}

static bool saadc_busy_check(void)
{
    if ((m_cb.saadc_state == NRF_SAADC_STATE_IDLE)     ||
        (m_cb.saadc_state == NRF_SAADC_STATE_ADV_MODE) ||
        (m_cb.saadc_state == NRF_SAADC_STATE_SIMPLE_MODE))
    {
        return false;
    }
    else
    {
        return true;
    }
}

static void saadc_generic_mode_set(uint32_t                   ch_to_activate_mask,
                                   nrf_saadc_resolution_t     resolution,
                                   nrf_saadc_oversample_t     oversampling,
                                   nrf_saadc_burst_t          burst,
                                   nrfx_saadc_event_handler_t event_handler)
{
#if NRFX_CHECK(USE_WORKAROUND_FOR_ANOMALY_212)
    saadc_anomaly_212_workaround_apply();
#endif

#if NRFX_CHECK(STOP_SAADC_ON_CHANNEL_CONFIG)
    nrf_saadc_int_disable(NRF_SAADC_INT_STOPPED);
    nrf_saadc_task_trigger(NRF_SAADC_TASK_STOP);
    while (!nrf_saadc_event_check(NRF_SAADC_EVENT_STOPPED))
    {}
    nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);
#endif

    m_cb.limits_low_activated = 0;
    m_cb.limits_high_activated = 0;

    m_cb.p_buffer_primary = NULL;
    m_cb.p_buffer_secondary = NULL;
    m_cb.event_handler = event_handler;
    m_cb.channels_activated = ch_to_activate_mask;
    m_cb.samples_converted = 0;

    nrf_saadc_resolution_set(resolution);
    nrf_saadc_oversample_set(oversampling);
    if (event_handler)
    {
        nrf_saadc_int_set(NRF_SAADC_INT_STARTED |
                          NRF_SAADC_INT_STOPPED |
                          NRF_SAADC_INT_END);
    }
    else
    {
        nrf_saadc_int_set(0);
    }

    for (uint32_t ch_pos = 0; ch_pos < SAADC_CH_NUM; ch_pos++)
    {
        nrf_saadc_burst_t burst_to_set;
        nrf_saadc_input_t pselp;
        nrf_saadc_input_t pseln;
        if (ch_to_activate_mask & (1uL << ch_pos))
        {
            pselp = m_cb.channels_pselp[ch_pos];
            pseln = m_cb.channels_pseln[ch_pos];
            burst_to_set = burst;
        }
        else
        {
            pselp = NRF_SAADC_INPUT_DISABLED;
            pseln = NRF_SAADC_INPUT_DISABLED;
            burst_to_set = NRF_SAADC_BURST_DISABLED;
        }
        nrf_saadc_burst_set(ch_pos, burst_to_set);
        nrf_saadc_channel_input_set(ch_pos, pselp, pseln);
    }
}

nrfx_err_t nrfx_saadc_init(uint8_t interrupt_priority)
{
    nrfx_err_t err_code;
    if (m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED)
    {
        err_code = NRFX_ERROR_INVALID_STATE;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }
    m_cb.saadc_state = NRF_SAADC_STATE_IDLE;

    nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);
    nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
    nrf_saadc_int_set(0);
    NRFX_IRQ_ENABLE(SAADC_IRQn);
    NRFX_IRQ_PRIORITY_SET(SAADC_IRQn, interrupt_priority);

    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));

    return err_code;
}

void nrfx_saadc_uninit(void)
{
    nrfx_saadc_abort();
    NRFX_IRQ_DISABLE(SAADC_IRQn);
    nrf_saadc_disable();
    m_cb.saadc_state = NRF_SAADC_STATE_UNINITIALIZED;
}

nrfx_err_t nrfx_saadc_channels_config(nrfx_saadc_channel_t const * p_channels,
                                      uint32_t                     channel_count)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);
    NRFX_ASSERT(channel_count <= SAADC_CH_NUM);

    if (saadc_busy_check())
    {
        return NRFX_ERROR_BUSY;
    }

    m_cb.channels_configured = 0;
    uint8_t i = 0;

    for (; i < SAADC_CH_NUM; i++)
    {
        m_cb.channels_pselp[i] = NRF_SAADC_INPUT_DISABLED;
        m_cb.channels_pseln[i] = NRF_SAADC_INPUT_DISABLED;
    }

    for (i = 0; i < channel_count; i++)
    {
        if (m_cb.channels_configured & (1uL << p_channels[i].channel_index))
        {
            // This channel is already configured!
            return NRFX_ERROR_INVALID_PARAM;
        }
        nrf_saadc_channel_init(p_channels[i].channel_index,
                               &p_channels[i].channel_config);

        NRFX_ASSERT(p_channels[i].pin_p);
        m_cb.channels_pselp[p_channels[i].channel_index] = p_channels[i].pin_p;
        m_cb.channels_pseln[p_channels[i].channel_index] = p_channels[i].pin_n;
        m_cb.channels_configured |= 1U << p_channels[i].channel_index;
    }

    return NRFX_SUCCESS;
}

nrfx_err_t nrfx_saadc_simple_mode_set(uint32_t                   channel_mask,
                                      nrf_saadc_resolution_t     resolution,
                                      nrf_saadc_oversample_t     oversampling,
                                      nrfx_saadc_event_handler_t event_handler)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);

    if (saadc_busy_check())
    {
        return NRFX_ERROR_BUSY;
    }

    uint8_t active_ch_count;
    nrfx_err_t err = saadc_channel_count_get(channel_mask, &active_ch_count);
    if (err != NRFX_SUCCESS)
    {
        return err;
    }

    nrf_saadc_burst_t burst;
    if (oversampling == NRF_SAADC_OVERSAMPLE_DISABLED)
    {
        burst = NRF_SAADC_BURST_DISABLED;
    }
    else
    {
        // Burst is implicitly enabled if oversampling is enabled.
        burst = NRF_SAADC_BURST_ENABLED;
    }

    saadc_generic_mode_set(channel_mask,
                           resolution,
                           oversampling,
                           burst,
                           event_handler);

    m_cb.channels_activated_count = active_ch_count;
    m_cb.saadc_state = NRF_SAADC_STATE_SIMPLE_MODE;

    return NRFX_SUCCESS;
}

nrfx_err_t nrfx_saadc_advanced_mode_set(uint32_t                        channel_mask,
                                        nrf_saadc_resolution_t          resolution,
                                        nrfx_saadc_adv_config_t const * p_config,
                                        nrfx_saadc_event_handler_t      event_handler)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);
    NRFX_ASSERT(p_config);

    if (saadc_busy_check())
    {
        return NRFX_ERROR_BUSY;
    }

    uint8_t active_ch_count;
    nrfx_err_t err = saadc_channel_count_get(channel_mask, &active_ch_count);
    if (err != NRFX_SUCCESS)
    {
        return err;
    }

    if ((p_config->internal_timer_cc) && ((active_ch_count > 1) || (!event_handler)))
    {
        return NRFX_ERROR_NOT_SUPPORTED;
    }

    bool oversampling_without_burst = false;
    if ((p_config->oversampling != NRF_SAADC_OVERSAMPLE_DISABLED) &&
        (p_config->burst == NRF_SAADC_BURST_DISABLED))
    {
        if (active_ch_count > 1)
        {
            // Oversampling without burst is possible only on single channel.
            return NRFX_ERROR_NOT_SUPPORTED;
        }
        else
        {
            oversampling_without_burst = true;
        }
    }

    saadc_generic_mode_set(channel_mask,
                           resolution,
                           p_config->oversampling,
                           p_config->burst,
                           event_handler);

    if (p_config->internal_timer_cc)
    {
        nrf_saadc_continuous_mode_enable(p_config->internal_timer_cc);
    }
    else
    {
        nrf_saadc_continuous_mode_disable();
    }

    m_cb.channels_activated_count = active_ch_count;
    m_cb.start_on_end = p_config->start_on_end;
    m_cb.oversampling_without_burst = oversampling_without_burst;

    m_cb.saadc_state = NRF_SAADC_STATE_ADV_MODE;

    return NRFX_SUCCESS;
}

nrfx_err_t nrfx_saadc_buffer_set(nrf_saadc_value_t * p_buffer, uint16_t size)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);

    if (m_cb.p_buffer_secondary)
    {
        return NRFX_ERROR_ALREADY_INITIALIZED;
    }

    if (!nrfx_is_in_ram(p_buffer))
    {
        return NRFX_ERROR_INVALID_ADDR;
    }

    if ((size % m_cb.channels_activated_count != 0) ||
        (size >= (1uL << SAADC_EASYDMA_MAXCNT_SIZE))  ||
        (!size))
    {
        return NRFX_ERROR_INVALID_LENGTH;
    }

    switch (m_cb.saadc_state)
    {
        case NRF_SAADC_STATE_SIMPLE_MODE:
            if (m_cb.channels_activated_count != size)
            {
                return NRFX_ERROR_INVALID_LENGTH;
            }
            m_cb.size_primary     = size;
            m_cb.p_buffer_primary = p_buffer;
            break;

        case NRF_SAADC_STATE_ADV_MODE_SAMPLE_STARTED:
            nrf_saadc_buffer_init(p_buffer, size);
            /* fall-through */

        case NRF_SAADC_STATE_ADV_MODE:
            /* fall-through */

        case NRF_SAADC_STATE_ADV_MODE_SAMPLE:
            if (m_cb.p_buffer_primary)
            {
                m_cb.size_secondary     = size;
                m_cb.p_buffer_secondary = p_buffer;
            }
            else
            {
                m_cb.size_primary     = size;
                m_cb.p_buffer_primary = p_buffer;
            }
            break;

        default:
            return NRFX_ERROR_INVALID_STATE;
    }

    return NRFX_SUCCESS;
}

nrfx_err_t nrfx_saadc_mode_trigger(void)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_IDLE);

    if (!m_cb.p_buffer_primary)
    {
        return NRFX_ERROR_NO_MEM;
    }

    nrfx_err_t result = NRFX_SUCCESS;
    switch (m_cb.saadc_state)
    {
        case NRF_SAADC_STATE_SIMPLE_MODE:
            nrf_saadc_enable();
            // When in simple blocking or non-blocking mode, buffer size is equal to activated channel count.
            // Single SAMPLE task is enough to obtain one sample on each activated channel.
            // This will result in buffer being filled with samples and therefore END event will appear.
            nrf_saadc_buffer_init(m_cb.p_buffer_primary, m_cb.size_primary);
            if (m_cb.event_handler)
            {
                m_cb.saadc_state = NRF_SAADC_STATE_SIMPLE_MODE_SAMPLE;
                nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
            }
            else
            {
                nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
                while (!nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED))
                {}
                nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);

                nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
                while (!nrf_saadc_event_check(NRF_SAADC_EVENT_END))
                {}
                nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
                nrf_saadc_disable();
            }
            break;

        case NRF_SAADC_STATE_ADV_MODE:
            nrf_saadc_enable();
            if (m_cb.event_handler)
            {
                // When in advanced non-blocking mode, latch whole buffer in EasyDMA.
                // END event will arrive when whole buffer is filled with samples.
                m_cb.saadc_state = NRF_SAADC_STATE_ADV_MODE_SAMPLE;
                nrf_saadc_buffer_init(m_cb.p_buffer_primary, m_cb.size_primary);
                nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
                break;
            }

            // When in advanced blocking mode, latch single chunk of buffer in EasyDMA.
            // Each chunk consists of single sample from each activated channels.
            // END event will arrive when single chunk is filled with samples.
            nrf_saadc_buffer_init(&m_cb.p_buffer_primary[m_cb.samples_converted],
                                  m_cb.channels_activated_count);

            nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
            while (!nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED))
            {}
            nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);

            if (m_cb.oversampling_without_burst)
            {
                // Oversampling without burst is possible only on single channel.
                // In this configuration more than one SAMPLE task is needed to obtain single sample.
                uint32_t samples_to_take =
                    nrf_saadc_oversample_sample_count_get(nrf_saadc_oversample_get());

                for (uint32_t sample_idx = 0; sample_idx < samples_to_take; sample_idx++)
                {
                    nrf_saadc_event_clear(NRF_SAADC_EVENT_DONE);
                    nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
                    while (!nrf_saadc_event_check(NRF_SAADC_EVENT_DONE))
                    {}
                }
            }
            else
            {
                // Single SAMPLE task is enough to obtain one sample on each activated channel.
                // This will result in chunk being filled with samples and therefore END event will appear.
                nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
            }
            while (!nrf_saadc_event_check(NRF_SAADC_EVENT_END))
            {}
            nrf_saadc_event_clear(NRF_SAADC_EVENT_END);

            m_cb.samples_converted += m_cb.channels_activated_count;
            if (m_cb.samples_converted < m_cb.size_primary)
            {
                result = NRFX_ERROR_BUSY;
            }
            else
            {
                m_cb.samples_converted  = 0;
                m_cb.p_buffer_primary   = m_cb.p_buffer_secondary;
                m_cb.size_primary       = m_cb.size_secondary;
                m_cb.p_buffer_secondary = NULL;
            }
            nrf_saadc_disable();
            break;

        default:
            result = NRFX_ERROR_INVALID_STATE;
            break;
    }

    return result;
}

void nrfx_saadc_abort(void)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);

    if (!m_cb.event_handler)
    {
        m_cb.p_buffer_primary = NULL;
        m_cb.p_buffer_secondary = NULL;
        m_cb.samples_converted = 0;
    }
    else
    {
        nrf_saadc_task_trigger(NRF_SAADC_TASK_STOP);
        if (m_cb.saadc_state == NRF_SAADC_STATE_CALIBRATION)
        {
            // STOPPED event does not appear when the calibration is ongoing
            m_cb.saadc_state = NRF_SAADC_STATE_IDLE;
        }
    }
}

nrfx_err_t nrfx_saadc_limits_set(uint8_t channel, int16_t limit_low, int16_t limit_high)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);
    NRFX_ASSERT(limit_high >= limit_low);

    if (!m_cb.event_handler)
    {
        return NRFX_ERROR_FORBIDDEN;
    }

    if ((m_cb.saadc_state == NRF_SAADC_STATE_IDLE) ||
        (m_cb.saadc_state == NRF_SAADC_STATE_CALIBRATION))
    {
        return NRFX_ERROR_INVALID_STATE;
    }

    if (!(m_cb.channels_activated & (1uL << channel)))
    {
        return NRFX_ERROR_INVALID_PARAM;
    }

    nrf_saadc_channel_limits_set(channel, limit_low, limit_high);

    uint32_t int_mask = nrf_saadc_limit_int_get(channel, NRF_SAADC_LIMIT_LOW);
    if (limit_low == INT16_MIN)
    {
        m_cb.limits_low_activated &= ~(1uL << channel);
        nrf_saadc_int_disable(int_mask);
    }
    else
    {
        m_cb.limits_low_activated |= (1uL << channel);
        nrf_saadc_int_enable(int_mask);
    }

    int_mask = nrf_saadc_limit_int_get(channel, NRF_SAADC_LIMIT_HIGH);
    if (limit_high == INT16_MAX)
    {
        m_cb.limits_high_activated &= ~(1uL << channel);
        nrf_saadc_int_disable(int_mask);
    }
    else
    {
        m_cb.limits_high_activated |= (1uL << channel);
        nrf_saadc_int_enable(int_mask);
    }

    return NRFX_SUCCESS;
}

nrfx_err_t nrfx_saadc_offset_calibrate(nrfx_saadc_event_handler_t event_handler)
{
    NRFX_ASSERT(m_cb.saadc_state != NRF_SAADC_STATE_UNINITIALIZED);

    if (saadc_busy_check())
    {
        return NRFX_ERROR_BUSY;
    }

    m_cb.saadc_state = NRF_SAADC_STATE_CALIBRATION;
    m_cb.event_handler = event_handler;

    nrf_saadc_enable();
#if NRFX_CHECK(INTERCEPT_SAADC_CALIBRATION_SAMPLES)
    nrf_saadc_buffer_init(m_cb.calib_samples, NRFX_ARRAY_SIZE(m_cb.calib_samples));
    if (event_handler)
    {
        nrf_saadc_int_set(NRF_SAADC_INT_STARTED | NRF_SAADC_INT_CALIBRATEDONE);
        nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
    }
    else
    {
        nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
        while (!nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED))
        {}
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);

        nrf_saadc_task_trigger(NRF_SAADC_TASK_CALIBRATEOFFSET);
        while (!nrf_saadc_event_check(NRF_SAADC_EVENT_CALIBRATEDONE))
        {}
        nrf_saadc_event_clear(NRF_SAADC_EVENT_CALIBRATEDONE);
        nrf_saadc_event_clear(NRF_SAADC_EVENT_END);

        nrf_saadc_disable();
        m_cb.saadc_state = NRF_SAADC_STATE_IDLE;
    }
#else
    nrf_saadc_task_trigger(NRF_SAADC_TASK_CALIBRATEOFFSET);
    if (event_handler)
    {
        nrf_saadc_int_enable(NRF_SAADC_INT_CALIBRATEDONE);
    }
    else
    {
        while (!nrf_saadc_event_check(NRF_SAADC_EVENT_CALIBRATEDONE))
        {}
        nrf_saadc_event_clear(NRF_SAADC_EVENT_CALIBRATEDONE);
        nrf_saadc_disable();
        m_cb.saadc_state = NRF_SAADC_STATE_IDLE;
    }
#endif // NRFX_CHECK(INTERCEPT_SAADC_CALIBRATION_SAMPLES)

    return NRFX_SUCCESS;
}

static void saadc_event_started_handle(void)
{
    nrfx_saadc_evt_t evt_data;

    switch (m_cb.saadc_state)
    {
        case NRF_SAADC_STATE_ADV_MODE_SAMPLE:
            evt_data.type = NRFX_SAADC_EVT_READY;
            m_cb.event_handler(&evt_data);

            if (nrf_saadc_continuous_mode_enable_check())
            {
                // Trigger internal timer
                nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
            }

            m_cb.saadc_state = NRF_SAADC_STATE_ADV_MODE_SAMPLE_STARTED;
            if (m_cb.p_buffer_secondary)
            {
                nrf_saadc_buffer_init(m_cb.p_buffer_secondary,
                                      m_cb.size_secondary);
            }
            /* fall-through */

        case NRF_SAADC_STATE_ADV_MODE_SAMPLE_STARTED:
            if (!m_cb.p_buffer_secondary)
            {
                // Send next buffer request only if it was not provided earlier,
                // before conversion start or outside of user's callback context.
                evt_data.type = NRFX_SAADC_EVT_BUF_REQ;
                m_cb.event_handler(&evt_data);
            }
            break;

        case NRF_SAADC_STATE_SIMPLE_MODE_SAMPLE:
            nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
            break;

#if NRFX_CHECK(INTERCEPT_SAADC_CALIBRATION_SAMPLES)
        case NRF_SAADC_STATE_CALIBRATION:
            nrf_saadc_task_trigger(NRF_SAADC_TASK_CALIBRATEOFFSET);
            break;
#endif

        default:
            break;
    }
}

static void saadc_event_end_handle(void)
{
    nrfx_saadc_evt_t evt_data;
    evt_data.type = NRFX_SAADC_EVT_DONE;
    evt_data.data.done.p_buffer = m_cb.p_buffer_primary;
    evt_data.data.done.size = m_cb.size_primary;
    m_cb.event_handler(&evt_data);

    switch (m_cb.saadc_state)
    {
        case NRF_SAADC_STATE_SIMPLE_MODE_SAMPLE:
            nrf_saadc_disable();
            m_cb.saadc_state = NRF_SAADC_STATE_SIMPLE_MODE;
            break;

        case NRF_SAADC_STATE_ADV_MODE_SAMPLE_STARTED:
            m_cb.p_buffer_primary = m_cb.p_buffer_secondary;
            m_cb.size_primary     = m_cb.size_secondary;
            m_cb.p_buffer_secondary = NULL;
            if (m_cb.p_buffer_primary)
            {
                if (m_cb.start_on_end)
                {
                    nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
                }
            }
            else
            {
                nrf_saadc_disable();
                m_cb.saadc_state = NRF_SAADC_STATE_ADV_MODE;
                evt_data.type = NRFX_SAADC_EVT_FINISHED;
                m_cb.event_handler(&evt_data);
            }
            break;

        default:
            break;
    }
}

static void saadc_event_limits_handle(uint8_t limits_activated, nrf_saadc_limit_t limit_type)
{
    while (limits_activated)
    {
        uint8_t channel = __CLZ(__RBIT((uint32_t)limits_activated));
        limits_activated &= ~(1uL << channel);

        nrf_saadc_event_t event = nrf_saadc_limit_event_get(channel, limit_type);
        if (nrf_saadc_event_check(event))
        {
            nrf_saadc_event_clear(event);

            nrfx_saadc_evt_t evt_data;
            evt_data.type = NRFX_SAADC_EVT_LIMIT;
            evt_data.data.limit.channel = channel;
            evt_data.data.limit.limit_type = limit_type;
            m_cb.event_handler(&evt_data);
        }
    }
}

void nrfx_saadc_irq_handler(void)
{
    if (nrf_saadc_event_check(NRF_SAADC_EVENT_STARTED))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
        saadc_event_started_handle();
    }

    if (nrf_saadc_event_check(NRF_SAADC_EVENT_STOPPED))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_STOPPED);

        // If there was ongoing conversion the STOP task also triggers the END event
        m_cb.size_primary = nrf_saadc_amount_get();
        m_cb.p_buffer_secondary = NULL;
        /* fall-through to the END event handler */
    }

    if (nrf_saadc_event_check(NRF_SAADC_EVENT_END))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_END);

#if NRFX_CHECK(INTERCEPT_SAADC_CALIBRATION_SAMPLES)
        // When samples are intercepted into scratch buffer during calibration,
        // END event appears when the calibration finishes. This event should be ignored.
        if (m_cb.saadc_state != NRF_SAADC_STATE_CALIBRATION)
#endif
        {
            saadc_event_end_handle();
        }
    }

    saadc_event_limits_handle(m_cb.limits_low_activated,  NRF_SAADC_LIMIT_LOW);
    saadc_event_limits_handle(m_cb.limits_high_activated, NRF_SAADC_LIMIT_HIGH);

    if (nrf_saadc_event_check(NRF_SAADC_EVENT_CALIBRATEDONE))
    {
        nrf_saadc_event_clear(NRF_SAADC_EVENT_CALIBRATEDONE);
        nrf_saadc_disable();

        m_cb.saadc_state = NRF_SAADC_STATE_IDLE;

        nrfx_saadc_evt_t evt_data;
        evt_data.type = NRFX_SAADC_EVT_CALIBRATEDONE;
        m_cb.event_handler(&evt_data);

    }
}
#endif // defined(NRFX_SAADC_API_V2) || defined(__NRFX_DOXYGEN__)

#endif // NRFX_CHECK(NRFX_SAADC_ENABLED)
