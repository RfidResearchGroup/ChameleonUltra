/**
 * Copyright (c) 2012 - 2021, Nordic Semiconductor ASA
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
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(BUTTON)
#include "app_button.h"
#include "app_timer.h"
#include "app_error.h"
#include "nrf_drv_gpiote.h"
#include "nrf_assert.h"

#define NRF_LOG_MODULE_NAME app_button
#if APP_BUTTON_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       APP_BUTTON_CONFIG_LOG_LEVEL
#define NRF_LOG_INFO_COLOR  APP_BUTTON_CONFIG_INFO_COLOR
#define NRF_LOG_DEBUG_COLOR APP_BUTTON_CONFIG_DEBUG_COLOR
#else //APP_BUTTON_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       0
#endif //APP_BUTTON_CONFIG_LOG_ENABLED
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

/*
 * For each pin state machine is used. Since GPIOTE PORT event is common for all pin is might be
 * missed. Module relies on interrupt from GPIOTE only to active periodic app_timer in which pin
 * is sampled. Timer is stopped when there is no active buttons (all buttons are in idle state).
 *
 * Transition to the new state is based on currently sampled button value. State machine has
 * following transitions:
 *
 * -----------------------------------------------------
 * | value | current state    | new state              |
 * |---------------------------------------------------|
 * |  0    | IDLE             | IDLE                   |
 * |  1    | IDLE             | PRESS_ARMED            |
 * |  0    | PRESS_ARMED      | IDLE                   |
 * |  1    | PRESS_ARMED      | PRESS_DETECTED         |
 * |  1    | PRESS_DETECTED   | PRESSED (push event)   |
 * |  0    | PRESS_DETECTED   | PRESS_ARMED            |
 * |  0    | PRESSED          | RELEASE_DETECTED       |
 * |  1    | PRESSED          | PRESSED                |
 * |  0    | RELEASE_DETECTED | IDLE (release event)   |
 * |  1    | RELEASE_DETECTED | PRESSED                |
 * -----------------------------------------------------
 *
 */
static app_button_cfg_t const *       mp_buttons = NULL;           /**< Button configuration. */
static uint8_t                        m_button_count;              /**< Number of configured buttons. */
static uint32_t                       m_detection_delay;           /**< Delay before a button is reported as pushed. */
APP_TIMER_DEF(m_detection_delay_timer_id);  /**< Polling timer id. */

static uint64_t m_pin_active;

#define BIT_PER_PIN 4
#define PINS 32*GPIO_COUNT

STATIC_ASSERT(BIT_PER_PIN == 4);

static uint8_t m_pin_states[PINS*BIT_PER_PIN/8];

typedef enum {
    BTN_IDLE,
    BTN_PRESS_ARMED,
    BTN_PRESS_DETECTED,
    BTN_PRESSED,
    BTN_RELEASE_DETECTED
} btn_state_t;

/* Retrieve given pin state. States are stored in pairs (4 bit per pin) in byte array. */
static btn_state_t state_get(uint8_t pin)
{
    uint8_t pair_state = m_pin_states[pin >> 1];
    uint8_t state = (pin & 0x1) ? (pair_state >> BIT_PER_PIN) : (pair_state & 0x0F);

    return (btn_state_t)state;
}

/* Set pin state. */
static void state_set(uint8_t pin, btn_state_t state)
{
    uint8_t mask = (pin & 1) ? 0x0F : 0xF0;
    uint8_t state_mask = (pin & 1) ?
                        ((uint8_t)state << BIT_PER_PIN) : (uint8_t)state;
    m_pin_states[pin >> 1] &= mask;
    m_pin_states[pin >> 1] |= state_mask;
}

/* Find configuration structure for given pin. */
static app_button_cfg_t const * button_get(uint8_t pin)
{
    for (int i = 0; i < m_button_count; i++)
    {
        app_button_cfg_t const * p_btn = &mp_buttons[i];
        if (pin == p_btn->pin_no) {
            return p_btn;
        }
    }

    /* If button is not found then configuration is wrong. */
    ASSERT(false);
    return NULL;
}

static void usr_event(uint8_t pin, uint8_t type)
{
    app_button_cfg_t const * p_btn = button_get(pin);

    if (p_btn && p_btn->button_handler)
    {
        NRF_LOG_DEBUG("Pin %d %s", pin, (type == APP_BUTTON_PUSH) ? "pressed" : "released");
        p_btn->button_handler(pin, type);
    }
}

/* State machine processing. */
void evt_handle(uint8_t pin, uint8_t value)
{
    switch(state_get(pin))
    {
    case BTN_IDLE:
        if (value)
        {
            NRF_LOG_DEBUG("Pin %d idle->armed", pin);
            state_set(pin, BTN_PRESS_ARMED);
            CRITICAL_REGION_ENTER();
            m_pin_active |= 1ULL << pin;
            CRITICAL_REGION_EXIT();
        }
        else
        {
            /* stay in IDLE */
        }
        break;
    case BTN_PRESS_ARMED:
        state_set(pin, value ? BTN_PRESS_DETECTED : BTN_IDLE);
        NRF_LOG_DEBUG("Pin %d armed->%s", pin, value ? "detected" : "idle");
        break;
    case BTN_PRESS_DETECTED:
        if (value)
        {
            state_set(pin, BTN_PRESSED);
            usr_event(pin, APP_BUTTON_PUSH);
        }
        else
        {
            state_set(pin, BTN_PRESS_ARMED);
        }
        NRF_LOG_DEBUG("Pin %d detected->%s", pin, value ? "pressed" : "armed");
        break;
    case BTN_PRESSED:
        if (value == 0)
        {
            NRF_LOG_DEBUG("Pin %d pressed->release_detected", pin);
            state_set(pin, BTN_RELEASE_DETECTED);
        }
        else
        {
            /* stay in pressed */
        }
        break;
    case BTN_RELEASE_DETECTED:
        if (value)
        {
            state_set(pin, BTN_PRESSED);
        }
        else
        {
            state_set(pin, BTN_IDLE);
            usr_event(pin, APP_BUTTON_RELEASE);
            CRITICAL_REGION_ENTER();
            m_pin_active &= ~(1ULL << pin);
            CRITICAL_REGION_EXIT();
        }
        NRF_LOG_DEBUG("Pin %d release_detected->%s", pin, value ? "pressed" : "idle");
        break;
    }
}

static void timer_start(void)
{
    uint32_t err_code = app_timer_start(m_detection_delay_timer_id, m_detection_delay/2, NULL);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_WARNING("Failed to start app_timer (err:%d)", err_code);
    }
}

static void detection_delay_timeout_handler(void * p_context)
{
    for (int i = 0; i < m_button_count; i++)
    {
        app_button_cfg_t const * p_btn = &mp_buttons[i];
        bool is_set = nrf_drv_gpiote_in_is_set(p_btn->pin_no);
        bool is_active = !((p_btn->active_state == APP_BUTTON_ACTIVE_HIGH) ^ is_set);
        evt_handle(p_btn->pin_no, is_active);
    }

    if (m_pin_active)
    {
        timer_start();
    }
    else
    {
        NRF_LOG_DEBUG("No active buttons, stopping timer");
    }
}

/* GPIOTE event is used only to start periodic timer when first button is activated. */
static void gpiote_event_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    app_button_cfg_t const * p_btn = button_get(pin);
    bool is_set = nrf_drv_gpiote_in_is_set(p_btn->pin_no);
    bool is_active = !((p_btn->active_state == APP_BUTTON_ACTIVE_HIGH) ^ is_set);

    /* If event indicates that pin is active and no other pin is active start the timer. All
     * action happens in timeout event.
     */
    if (is_active && (m_pin_active == 0))
    {
        NRF_LOG_DEBUG("First active button, starting periodic timer");
        timer_start();
    }
}

uint32_t app_button_init(app_button_cfg_t const *       p_buttons,
                         uint8_t                        button_count,
                         uint32_t                       detection_delay)
{
    uint32_t err_code;

    if (detection_delay < 2*APP_TIMER_MIN_TIMEOUT_TICKS)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    if (!nrf_drv_gpiote_is_init())
    {
        err_code = nrf_drv_gpiote_init();
        VERIFY_SUCCESS(err_code);
    }

    /* Save configuration. */
    mp_buttons          = p_buttons;
    m_button_count      = button_count;
    m_detection_delay   = detection_delay;

    memset(m_pin_states, 0, sizeof(m_pin_states));
    m_pin_active = 0;

    while (button_count--)
    {
        app_button_cfg_t const * p_btn = &p_buttons[button_count];

#if defined(BUTTON_HIGH_ACCURACY_ENABLED) && (BUTTON_HIGH_ACCURACY_ENABLED == 1)
        nrf_drv_gpiote_in_config_t config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(p_btn->hi_accuracy);
#else
        nrf_drv_gpiote_in_config_t config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
#endif
        config.pull = p_btn->pull_cfg;

        err_code = nrf_drv_gpiote_in_init(p_btn->pin_no, &config, gpiote_event_handler);
        VERIFY_SUCCESS(err_code);
    }

    /* Create polling timer. */
    return app_timer_create(&m_detection_delay_timer_id,
                            APP_TIMER_MODE_SINGLE_SHOT,
                            detection_delay_timeout_handler);
}

uint32_t app_button_enable(void)
{
    ASSERT(mp_buttons);

    uint32_t i;
    for (i = 0; i < m_button_count; i++)
    {
        nrf_drv_gpiote_in_event_enable(mp_buttons[i].pin_no, true);
    }

    return NRF_SUCCESS;
}


uint32_t app_button_disable(void)
{
    ASSERT(mp_buttons);

    uint32_t i;
    for (i = 0; i < m_button_count; i++)
    {
       nrf_drv_gpiote_in_event_disable(mp_buttons[i].pin_no);
    }
    CRITICAL_REGION_ENTER();
    m_pin_active = 0;
    CRITICAL_REGION_EXIT();

    /* Make sure polling timer is not running. */
    return app_timer_stop(m_detection_delay_timer_id);
}


bool app_button_is_pushed(uint8_t button_id)
{
    ASSERT(button_id <= m_button_count);
    ASSERT(mp_buttons != NULL);

    app_button_cfg_t const * p_btn = &mp_buttons[button_id];
    bool is_set = nrf_drv_gpiote_in_is_set(p_btn->pin_no);

    return !(is_set ^ (p_btn->active_state == APP_BUTTON_ACTIVE_HIGH));
}
#endif //NRF_MODULE_ENABLED(BUTTON)
