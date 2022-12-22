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
#include "nrf21540_gpio.h"
#include "nrf_assert.h"
#include "boards.h"
#include "nrf21540_defs.h"
#include "nrf_gpiote.h"
#include "nrf_ppi.h"
#include "nrf_timer.h"

void nrf21540_gpio_init(void)
{
    nrf_gpio_cfg_output(NRF21540_ANTSEL_PIN);
#if NRF21540_USE_GPIO_MANAGEMENT
    nrf_gpio_cfg_output(NRF21540_MODE_PIN);
    //GPIOTE for TXEN pin configuration
    nrf_gpiote_task_configure(NRF21540_PA_GPIOTE_CHANNEL_NO,
                              NRF21540_TXEN_PIN,
                              (nrf_gpiote_polarity_t) GPIOTE_CONFIG_POLARITY_None,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF21540_PA_GPIOTE_CHANNEL_NO);
    //GPIOTE for RXEN pin configuration
    nrf_gpiote_task_configure(NRF21540_LNA_GPIOTE_CHANNEL_NO,
                              NRF21540_RXEN_PIN,
                              (nrf_gpiote_polarity_t) GPIOTE_CONFIG_POLARITY_None,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF21540_LNA_GPIOTE_CHANNEL_NO);
#endif /*NRF21540_USE_GPIO_MANAGEMENT*/
}

ret_code_t  nrf21540_gpio_ant_set(nrf21540_antenna_t antenna)
{
    if (antenna == NRF21540_ANT1)
    {
        nrf_gpio_pin_clear(NRF21540_ANTSEL_PIN);
    }
    else if (antenna == NRF21540_ANT2)
    {
       nrf_gpio_pin_set(NRF21540_ANTSEL_PIN);
    }
    else
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    return NRF_SUCCESS;
}

#if NRF21540_USE_GPIO_MANAGEMENT

uint32_t nrf21540_gpio_trx_task_start_address_get(nrf21540_trx_t dir,
                                                  nrf21540_bool_state_t required_state)
{
    uint8_t gpiote_rx_tx_channel =
                      dir == NRF21540_TX ?
                      NRF21540_PA_GPIOTE_CHANNEL_NO :
                      NRF21540_LNA_GPIOTE_CHANNEL_NO;
    return required_state ==  NRF21540_ENABLE ?
                      nrf_gpiote_task_addr_get(nrf_gpiote_set_task_get(gpiote_rx_tx_channel)) :
                      nrf_gpiote_task_addr_get(nrf_gpiote_clr_task_get(gpiote_rx_tx_channel));
}

void nrf21540_gpio_trx_enable(nrf21540_trx_t dir)
{
    uint32_t gpiote_task_start = nrf21540_gpio_trx_task_start_address_get(dir, NRF21540_ENABLE);
    nrf_ppi_channel_endpoint_setup(NRF21540_TRX_PPI_CHANNEL,
                   (uint32_t)nrf_timer_event_address_get(NRF21540_TIMER,
                                                         NRF21540_TIMER_CC_PD_PG_EVENT),
                   gpiote_task_start);
    nrf_ppi_channel_enable(NRF21540_TRX_PPI_CHANNEL);
}

ret_code_t  nrf21540_gpio_pwr_mode_set(nrf21540_pwr_mode_t mode)
{
    if (mode == NRF21540_PWR_MODE_A)
    {
        nrf_gpio_pin_clear(NRF21540_MODE_PIN);
    }
    else if (mode == NRF21540_PWR_MODE_B)
    {
        nrf_gpio_pin_set(NRF21540_MODE_PIN);
    }
    else
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    return NRF_SUCCESS;
}

#endif /*NRF21540_USE_GPIO_MANAGEMENT*/
