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
#ifndef NRF21540_GPIO_H_
#define NRF21540_GPIO_H_

#include "nrf_gpio.h"
#include "nrf21540_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Function initializes GPIO interface.
 */
void nrf21540_gpio_init(void);

/**@brief Function choses one of two physical antenna outputs.
 *
 * @param[in] antenna   one of antenna outputs. See @ref nrf21540_antenna_t.
 * @return              NRF_ERROR_INVALID_PARAM when invalid argument given.
 *                      NRF_SUCCESS on success.
 */
ret_code_t  nrf21540_gpio_ant_set(nrf21540_antenna_t antenna);

#if NRF21540_USE_GPIO_MANAGEMENT
/**@brief Function returns address of task which triggers RX_EN/TX_EN pin
 *        to set nRF21540 radio trasfer direction.
 *
 * @param[in] dir             Direction of the radio transmission. See @ref nrf21540_trx_t.
 * @param[in] required_state  State of RX/TX transfer. See @ref nrf21540_bool_state_t.
 * @return                    Address of appropriate task.
 */
uint32_t nrf21540_gpio_trx_task_start_address_get(nrf21540_trx_t dir,
                                                  nrf21540_bool_state_t required_state);

/**@brief Function configures the chip and peripherals for TX/RX transfer purpose.
 *
 * @details enables/disables RX/TX transfers.
 *
 * @param[in] dir    direction of radio transfer. See @ref nrf21540_trx_t.
 */
void nrf21540_gpio_trx_enable(nrf21540_trx_t dir);

/**@brief Function choses one of two predefined power modes in nRF21540.
 *
 * @details Refer to nRF21540 Objective Product Specification, section: TX power control.
 *
 * @param[in] mode  Power mode. See @ref nrf21540_pwr_mode_t.
 * @return          NRF_ERROR_INVALID_PARAM when invalid argument given.
 *                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_gpio_pwr_mode_set(nrf21540_pwr_mode_t mode);

#endif /*NRF21540_USE_GPIO_MANAGEMENT*/

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_GPIO_H_
