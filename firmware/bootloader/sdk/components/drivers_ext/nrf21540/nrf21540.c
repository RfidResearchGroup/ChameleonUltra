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
#include "nrf21540.h"
#include "nrf_assert.h"
#include "nrf21540_defs.h"
#include "nrf21540_macro.h"
#include "nrf_radio.h"
#include "nrf_ppi.h"
#include "nrf_gpiote.h"
#include "nrf_timer.h"
#include "boards.h"
#if NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#include "nrf_egu.h"
#endif

#define NRF21540_BUSY_CHECK(mode)                       \
    if (mode == NRF21540_EXEC_MODE_BLOCKING)            \
    {                                                   \
        while(is_driver_busy())                         \
        {                                               \
                                                        \
        }                                               \
    }                                                   \
    else if (is_driver_busy())                          \
    {                                                   \
      return NRF_ERROR_BUSY;                            \
    }

#define NRF21540_ERROR_CHECK(invalid_state_condition)   \
    if (device_state_get() == NRF21540_STATE_ERROR)     \
    {                                                   \
        m_nrf21540_data.busy = false;                   \
        return NRF_ERROR_INTERNAL;                      \
    }                                                   \
    if (invalid_state_condition)                        \
    {                                                   \
        m_nrf21540_data.busy = false;                   \
        return NRF_ERROR_INVALID_STATE;                 \
    }

/**@brief nRF21540 chip state.
 *
 * @details driver state variable possible values.
 */
typedef enum {
    NRF21540_STATE_OFF,   ///< Chip inactive, line PDN is low, SPI communication impossible.
    NRF21540_STATE_READY, ///< SPI is active, but nether transmit nor receive can be performed.
    NRF21540_STATE_TX,    ///< Transmit state - chip can perform transmiting data.
    NRF21540_STATE_RX,    ///< Receive state - chip can receive data.
    NRF21540_STATE_ERROR, ///< Invalid state - requires reinit.
} nrf21540_state_t;

/**@brief nRF21540 static data. */
static struct
{
    volatile nrf21540_state_t cur_state;   ///< driver state variable.
    volatile nrf21540_trx_t cur_direction; ///< currently serviced radio communication direction.
    volatile bool busy;                    ///< driver is busy at the moment (during changing state phase).
#if NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
    volatile uint32_t shorts;
#endif
} m_nrf21540_data;

/**@brief Function checks if nRF21540 driver is busy now.
 *
 * @details Based on driver busy variable.
 *
 * @return true if nRF21540 driver is busy.
 */
static inline bool is_driver_busy(void)
{
    return m_nrf21540_data.busy;
}

/**@brief Function checks if nRF21540 is powered down.
 *
 * @details Based on driver state variable.
 *
 * @return true if nRF21540 is in power down state.
 */
static inline bool is_device_off(void)
{
    return m_nrf21540_data.cur_state == NRF21540_STATE_OFF;
}

/**@brief Function checks if nRF21540 is powered up state.
 *
 * @details Based on driver state variable.
 *
 * @return true if nRF21540 is in power up state.
 */
static inline bool is_device_on(void)
{
    return m_nrf21540_data.cur_state != NRF21540_STATE_OFF;
}

/**@brief Function checks if nRF21540 can transmit or receive data.
 *
 * @details Based on driver state variable.
 *
 * @return true if nRF21540 is in TX or RX mode.
 */
static inline bool is_device_ready_for_transmission(void)
{
    return (m_nrf21540_data.cur_state == NRF21540_STATE_TX ||
            m_nrf21540_data.cur_state == NRF21540_STATE_RX);
}

/**@brief Function changes driver state.
 *
 * @details Changes driver state variable value.
 *
 * @param[in] new_state   state that will be store.
 */
static inline void device_state_set(nrf21540_state_t new_state)
{
    m_nrf21540_data.cur_state = new_state;
}

/**@brief Function returns driver state variable value.
 *
 * @details Based on driver state variable.
 *
 * @return @ref nrf21540_state_t based state variable value.
 */
static inline nrf21540_state_t device_state_get(void)
{
    return m_nrf21540_data.cur_state;
}

/**@brief Function returns task related to transmission direction.
 *
 * @param[in] dir direction of radio transfer. See @nrf21540_trx_t.
 *
 * @return task corresponding to given transmission direction.
 */
static inline nrf_radio_task_t nrf21540_task_get(nrf21540_trx_t dir)
{
    return dir == NRF21540_TX ? NRF_RADIO_TASK_TXEN : NRF_RADIO_TASK_RXEN;
}

/**@brief Function clears and disbles all PPI connections used by nRF21540 driver.
 *
 * @details Changes driver state variable value.
 */
static void ppi_cleanup(void)
{
    nrf_ppi_channel_disable(NRF21540_PDN_PPI_CHANNEL);
    nrf_ppi_channel_disable(NRF21540_USER_PPI_CHANNEL);
    nrf_ppi_channel_disable(NRF21540_TRX_PPI_CHANNEL);
    nrf_ppi_channel_and_fork_endpoint_setup(NRF21540_PDN_PPI_CHANNEL, 0, 0, 0);
    nrf_ppi_channel_and_fork_endpoint_setup(NRF21540_USER_PPI_CHANNEL, 0, 0, 0);
    nrf_ppi_channel_and_fork_endpoint_setup(NRF21540_TRX_PPI_CHANNEL, 0, 0, 0);
}

/**@brief Function clears nRF21540 driver events. */
static void events_clear()
{
    nrf_timer_event_clear(NRF21540_TIMER, NRF21540_TIMER_CC_PD_PG_EVENT);
    nrf_timer_event_clear(NRF21540_TIMER, NRF21540_TIMER_CC_START_TO_PDN_UP_EVENT);
    NRF21540_RADIO_EVENT_CLEAR(NRF21540_RADIO_EVENT_READY);
    NRF21540_RADIO_EVENT_CLEAR(NRF21540_RADIO_EVENT_DISABLED);
}

/**@brief Timer interrupt handler.
 *
 * @details checking time related events occurences and changing driver state if necessary.
 */
void NRF21540_TIMER_IRQ_HANDLER(void)
{
    if (nrf_timer_event_check(NRF21540_TIMER, NRF21540_TIMER_CC_PD_PG_EVENT))
    {
        nrf_timer_event_clear(NRF21540_TIMER, NRF21540_TIMER_CC_PD_PG_EVENT);
        if (is_device_off() && nrf_gpio_pin_read(NRF21540_PDN_PIN) == 1)
        {
            device_state_set(NRF21540_STATE_READY);
        }
        else if (is_device_on() && nrf_gpio_pin_read(NRF21540_PDN_PIN) == 0)
        {
            device_state_set(NRF21540_STATE_OFF);
            ppi_cleanup();
            m_nrf21540_data.busy = false;
        }
        else
        {
            device_state_set(NRF21540_STATE_ERROR);
            ppi_cleanup();
            m_nrf21540_data.busy = false;
        }
    }
}

/**@brief nRF21540 interrupt handler.
 *
 * @details checking radio related events occurences and changing driver state if necessary.
 */
void NRF21540_RADIO_IRQ_HANDLER(void)
{
    if (NRF21540_RADIO_EVENT_CHECK(NRF21540_RADIO_EVENT_READY))
    {
        NRF21540_RADIO_EVENT_CLEAR(NRF21540_RADIO_EVENT_READY);
        nrf_ppi_channel_disable(NRF21540_USER_PPI_CHANNEL);
#if NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
        if (NRF21540_RADIO_SHORTS_ENABLE_CHECK(RADIO_SHORTS_READY_START_Msk))
        {
            nrf_radio_task_trigger(NRF_RADIO_TASK_START);
        }
#endif
        if (device_state_get() == NRF21540_STATE_READY)
        {
            device_state_set(m_nrf21540_data.cur_direction == NRF21540_TX ?
                NRF21540_STATE_TX : NRF21540_STATE_RX);
            ppi_cleanup();
            NRF21540_RADIO_INT_DISABLE(NRF21540_RADIO_READY_Msk);
            m_nrf21540_data.busy = false;
        }
    }
    if (NRF21540_RADIO_EVENT_CHECK(NRF21540_RADIO_EVENT_DISABLED))
    {
        NRF21540_RADIO_EVENT_CLEAR(NRF21540_RADIO_EVENT_DISABLED);
        nrf_ppi_channel_disable(NRF21540_USER_PPI_CHANNEL);
        nrf_timer_task_trigger(NRF21540_TIMER, NRF_TIMER_TASK_START);
        if (is_device_ready_for_transmission())
        {
            NRF21540_RADIO_INT_DISABLE(NRF21540_RADIO_DISABLED_Msk);
            device_state_set(NRF21540_STATE_READY);
        }
    }
}

/**@brief Function resets nRF21540 driver.
 *
 * @details sets driver state variable to NRF21540_STATE_OFF value,
 *          cleans all used PPIs and events.
 */
static void driver_reset(void)
{
    device_state_set(NRF21540_STATE_OFF);
    ppi_cleanup();
    events_clear();
    NRF21540_RADIO_INT_DISABLE(NRF21540_RADIO_INTERRUPT_MASK);
    m_nrf21540_data.busy = false;
}


/**@brief Function sets either TX or RX direction.
 *
 * @details Configuration of all necessarily peripherals to transmit or receive data,
 *          dependenly on interface used (SPI, or GPIO). Procedure configures nRF21540 chip and
 *          starts transmitting/receiving. Procedure will be started immediately if
 *          @ref trigger_event is 0. Otherwise event which address is @ref trigger_event value
 *          will start procedure.
 *
 * @param[in] dir           RX or TX communication that will be performed.
 * @param[in] trigger_event address of event which will trigger the procedure.
 * @param[in] mode          if NRF21540_EXEC_MODE_BLOCKING the function will wait for tx/rx
 *                            possibility.
 * @return                    NRF_ERROR_INTERNAL when driver is in error state.
 *                              Reinitialization is required.
 *                            NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                              to perform the operation (@sa nrf21540_state_t).
 *                            NRF_ERROR_BUSY when driver performs another operation at
 *                              the moment.
 *                            NRF_SUCCESS on success.
 */
static ret_code_t trx_set(nrf21540_trx_t dir, uint32_t trigger_event,
                          nrf21540_execution_mode_t mode)
{
    ASSERT(!(mode == NRF21540_EXEC_MODE_BLOCKING && trigger_event != 0));
    NRF21540_BUSY_CHECK(mode);
    NRF21540_ERROR_CHECK((dir == NRF21540_TX && device_state_get() == NRF21540_STATE_TX) ||
                         (dir == NRF21540_RX && device_state_get() == NRF21540_STATE_RX));
    uint32_t ramp_up_time = nrf_radio_modecnf0_ru_get() ? FAST_RAMP_UP_TIME : RAMP_UP_TIME;
    nrf_radio_task_t radio_task_to_start = nrf21540_task_get(dir);
    m_nrf21540_data.busy = true;
    events_clear();
    NRF21540_RADIO_INT_ENABLE(NRF21540_RADIO_READY_Msk);
    if (is_device_off())
    {
        nrf_ppi_channel_endpoint_setup(NRF21540_PDN_PPI_CHANNEL,
              (uint32_t)nrf_timer_event_address_get(NRF21540_TIMER,
                                                    NRF21540_TIMER_CC_START_TO_PDN_UP_EVENT),
              nrf_gpiote_task_addr_get(NRF21540_PDN_GPIOTE_TASK_SET));
        nrf_ppi_channel_enable(NRF21540_PDN_PPI_CHANNEL);
        nrf_timer_cc_write(NRF21540_TIMER,
                           NRF21540_TIMER_CC_PD_PG_CHANNEL,
                           ramp_up_time - NRF21540_PA_PG_TRX_TIME_US);
        nrf_timer_cc_write(NRF21540_TIMER,
                           NRF21540_TIMER_CC_START_TO_PDN_UP_CHANNEL,
                           ramp_up_time - NRF21540_PA_PG_TRX_TIME_US - NRF21540_PD_PG_TIME_US);
#if NRF21540_USE_GPIO_MANAGEMENT
        nrf21540_gpio_trx_enable(dir);
#elif NRF21540_USE_SPI_MANAGEMENT
        nrf21540_spim_for_trx_configure(dir, NRF21540_ENABLE);
#endif
        nrf_timer_shorts_enable(NRF21540_TIMER,
                                NRF21540_TIMER_CC_FINISHED_CHANNEL_STOP_MASK |
                                NRF21540_TIMER_CC_FINISHED_CHANNEL_CLEAR_MASK);
        NRF21540_RADIO_SHORTS_ENABLE(RADIO_SHORTS_READY_START_Msk);
        if (trigger_event == 0)
        {
            //start immediately.
            nrf_timer_task_trigger(NRF21540_TIMER, NRF_TIMER_TASK_START);
            nrf_radio_task_trigger(radio_task_to_start);
        }
        else
        {
            //start when user event occurs.
            nrf_ppi_channel_and_fork_endpoint_setup(
                NRF21540_USER_PPI_CHANNEL,
                trigger_event,
                (uint32_t) nrf_timer_task_address_get(NRF21540_TIMER, NRF_TIMER_TASK_START),
                (uint32_t) nrf_radio_task_address_get(radio_task_to_start));
            nrf_ppi_channel_enable(NRF21540_USER_PPI_CHANNEL);
        }
    }
    else
    {
        // at the moment we are not able to switch direction on the fly.
        // @todo switching between RXEN and TXEN.
        NRF21540_ERROR_CHECK(device_state_get() == NRF21540_STATE_RX);
        if (trigger_event == 0)
        {
            nrf_radio_task_trigger(radio_task_to_start);
        }
        else
        {
            // start when user event occurs
            nrf_ppi_channel_endpoint_setup(
                NRF21540_USER_PPI_CHANNEL,
                trigger_event,
                (uint32_t) nrf_radio_task_address_get(radio_task_to_start));
            nrf_ppi_channel_enable(NRF21540_USER_PPI_CHANNEL);
        }
    }
    m_nrf21540_data.cur_direction = dir;
    if (mode == NRF21540_EXEC_MODE_BLOCKING)
    {
        while (!is_device_ready_for_transmission());
    }
    return NRF_SUCCESS;
}

ret_code_t nrf21540_init(void)
{
    driver_reset();
   // GPIOTE for PDN pin configuration
    nrf_gpiote_task_configure(NRF21540_PDN_GPIOTE_CHANNEL_NO,
        NRF21540_PDN_PIN,
        (nrf_gpiote_polarity_t) GPIOTE_CONFIG_POLARITY_None,
        NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF21540_PDN_GPIOTE_CHANNEL_NO);
    nrf21540_gpio_init();
    NVIC_SetPriority(NRF21540_TIMER_IRQn, NRF21540_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(NRF21540_TIMER_IRQn);
    nrf_timer_int_enable(NRF21540_TIMER, NRF21540_TIM_INTERRUPT_MASK);
#if NRF21540_USE_SPI_MANAGEMENT
    ret_code_t ret = NRF_SUCCESS;
    ret = nrf21540_spi_init();
    if (ret != NRF_SUCCESS)
    {
        device_state_set(NRF21540_STATE_ERROR);
        return ret;
    }
#endif //NRF21540_USE_SPI_MANAGEMENT
#if NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
    nrf_ppi_channel_endpoint_setup(NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL,
          nrf_radio_event_address_get(NRF_RADIO_EVENT_READY),
          (uint32_t)nrf_egu_task_address_get(NRF21540_EGU, NRF21540_RADIO_READY_EGU_TASK));
    nrf_ppi_channel_enable(NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL);
    nrf_ppi_channel_endpoint_setup(NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL,
          nrf_radio_event_address_get(NRF_RADIO_EVENT_DISABLED),
          (uint32_t)nrf_egu_task_address_get(NRF21540_EGU, NRF21540_RADIO_DISABLED_EGU_TASK));
    nrf_ppi_channel_enable(NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL);
#endif
    NVIC_SetPriority(NRF21540_RADIO_IRQn, NRF21540_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(NRF21540_RADIO_IRQn);
    return NRF_SUCCESS;
}


ret_code_t nrf21540_pdn_drive(bool state, nrf21540_execution_mode_t mode)
{
    NRF21540_BUSY_CHECK(mode);
    NRF21540_ERROR_CHECK((state == true && is_device_on()) ||
                         (state == false && is_device_off()));
    nrf21540_state_t final_state;
    if (state)
    {
        final_state = NRF21540_STATE_READY;
    }
    else
    {
        final_state = NRF21540_STATE_OFF;
    }
    nrf_timer_cc_write(NRF21540_TIMER,
        NRF21540_TIMER_CC_PD_PG_CHANNEL,
        state ? NRF21540_PD_PG_TIME_US : 0);
    nrf_timer_shorts_enable(NRF21540_TIMER,
        NRF21540_TIMER_CC_FINISHED_CHANNEL_STOP_MASK |
        NRF21540_TIMER_CC_FINISHED_CHANNEL_CLEAR_MASK);
    nrf_timer_event_clear(NRF21540_TIMER, NRF21540_TIMER_CC_PD_PG_EVENT);
    nrf_gpiote_task_force(NRF21540_PDN_GPIOTE_CHANNEL_NO,
        !state ? NRF_GPIOTE_INITIAL_VALUE_LOW : NRF_GPIOTE_INITIAL_VALUE_HIGH);
    nrf_timer_task_trigger(NRF21540_TIMER, NRF_TIMER_TASK_START);
    if (mode == NRF21540_EXEC_MODE_BLOCKING)
    {
        while (device_state_get() != final_state)
        {

        }
    }
    return NRF_SUCCESS;
}

ret_code_t nrf21540_tx_set(uint32_t user_trigger_event, nrf21540_execution_mode_t mode)
{
    NRF21540_ERROR_CHECK(device_state_get() == NRF21540_STATE_TX);
    return trx_set(NRF21540_TX, user_trigger_event, mode);
}

ret_code_t nrf21540_rx_set(uint32_t user_trigger_event, nrf21540_execution_mode_t mode)
{
    NRF21540_ERROR_CHECK(device_state_get() == NRF21540_STATE_RX);
    return trx_set(NRF21540_RX, user_trigger_event, mode);
}


bool nrf21540_is_error(void)
{
    return device_state_get() == NRF21540_STATE_ERROR ? true : false;
}

ret_code_t nrf21540_ant_set(nrf21540_antenna_t antenna)
{
    NRF21540_BUSY_CHECK(NRF21540_EXEC_MODE_NON_BLOCKING);
    return nrf21540_gpio_ant_set(antenna);
}


ret_code_t nrf21540_pwr_mode_set(nrf21540_pwr_mode_t mode)
{
    NRF21540_BUSY_CHECK(NRF21540_EXEC_MODE_NON_BLOCKING);
#if NRF21540_USE_GPIO_MANAGEMENT
    return nrf21540_gpio_pwr_mode_set(mode);
#elif NRF21540_USE_SPI_MANAGEMENT
    return nrf21540_spi_pwr_mode_set(mode);
#endif
}

ret_code_t nrf21540_power_down(uint32_t user_trigger_event, nrf21540_execution_mode_t mode)
{
    ASSERT(!(mode == NRF21540_EXEC_MODE_BLOCKING && user_trigger_event != 0));
    NRF21540_ERROR_CHECK(is_device_off());
    NRF21540_BUSY_CHECK(mode);
    m_nrf21540_data.busy = true;
    events_clear();
    NRF21540_RADIO_INT_ENABLE(NRF21540_RADIO_DISABLED_Msk);
    if (device_state_get() == NRF21540_STATE_READY)
    {
        // when device is in ready state we jus driving PDN line down and switch off the radio.
        (void)nrf21540_pdn_drive(false, NRF21540_EXEC_MODE_NON_BLOCKING);
        nrf_radio_task_trigger(NRF_RADIO_TASK_DISABLE);
    }
    else
    {
    // When device is in tx/rx state we have to leave it and then drive PDN down.
    // Line PDN should be driven low after 5us from triggering TXEN/RXEN.
        uint32_t * trx_drv_task_address;
        nrf21540_trx_t cur_direction;
        if (device_state_get() == NRF21540_STATE_TX)
        {
            cur_direction = NRF21540_TX;
        }
        else if (device_state_get() == NRF21540_STATE_RX)
        {
            cur_direction = NRF21540_RX;
        }
        else
        {
            return NRF_ERROR_INTERNAL;
        }
        nrf_ppi_channel_endpoint_setup(NRF21540_PDN_PPI_CHANNEL,
                   (uint32_t)nrf_timer_event_address_get(NRF21540_TIMER,
                                                         NRF21540_TIMER_CC_TRX_PG_EVENT),
                   nrf_gpiote_task_addr_get(NRF21540_PDN_GPIOTE_TASK_CLR));
        nrf_ppi_channel_enable(NRF21540_PDN_PPI_CHANNEL);
        nrf_timer_shorts_enable(NRF21540_TIMER,
                                NRF21540_TIMER_CC_FINISHED_CHANNEL_STOP_MASK |
                                NRF21540_TIMER_CC_FINISHED_CHANNEL_CLEAR_MASK);
        nrf_timer_cc_write(NRF21540_TIMER,
                           NRF21540_TIMER_CC_TRX_PG_CHANNEL,
                           NRF21540_TRX_PG_TIME_US);
#if NRF21540_USE_GPIO_MANAGEMENT
        trx_drv_task_address = (uint32_t*) nrf21540_gpio_trx_task_start_address_get(cur_direction,
                                                                                NRF21540_DISABLE);
#elif NRF21540_USE_SPI_MANAGEMENT
        nrf21540_spim_for_trx_configure(cur_direction, NRF21540_DISABLE);
        trx_drv_task_address = (uint32_t*) nrf21540_spim_trx_task_start_address_get();
#endif
        if (user_trigger_event == 0)
        {
            *trx_drv_task_address = 1;
            nrf_radio_task_trigger(NRF_RADIO_TASK_DISABLE);
        }
        else
        {
            // start when user event occurs.
            nrf_ppi_channel_and_fork_endpoint_setup(
                NRF21540_USER_PPI_CHANNEL,
                user_trigger_event,
                (uint32_t)nrf_radio_task_address_get(NRF_RADIO_TASK_DISABLE),
                (uint32_t) trx_drv_task_address);
            nrf_ppi_channel_enable(NRF21540_USER_PPI_CHANNEL);
        }
        if (mode == NRF21540_EXEC_MODE_BLOCKING)
        {
            while (is_device_on());
        }
    }
    return NRF_SUCCESS;
}
