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
#ifndef NRF21540_H_
#define NRF21540_H_

#include <stdbool.h>
#include <stdint.h>
#include "nrf21540_spi.h"
#include "nrf21540_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @file
* @brief nRF21540 front-end Bluetooth range extender.
*
*
* @defgroup nrf21540 nRF21540 front-end Bluetooth range extender.
* @{
* @ingroup ext_drivers
* @brief nRF21540 front-end Bluetooth range extender.
*/

#if NRF21540_USE_SPI_MANAGEMENT
#if NRF21540_USE_GPIO_MANAGEMENT
#error Only one management manner can be active
#endif
#elif !NRF21540_USE_GPIO_MANAGEMENT
#error At least one management manner must be active
#endif // NRF21540_USE_SPI_MANAGEMENT

/**@brief Initialization of modules needed by nRF21540:
 *         - SPI
 *         - GPIO
 *         - GPIOTE
 *         - PPI
 *         - RADIO
 *         - NVIC
 *
 * @return          NRF based error code.
 *                  NRF_ERROR_INTERNAL when driver is in error state,
 *                    or SPI initialization has failed. Reinitialization is required.
 *                  NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                    to perform the operation (@sa nrf21540_state_t).
 *                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_init(void);

/**@brief Set nRF21540 to TX mode.
 *
 * @note Dependently on configuration GPIO or SPI interface will be used.
 *
 * @param[in] user_trigger_event    event that triggers start of procedure - this event
 *                                    will be connected to appropriate PPI channel.
 *                                    @ref NRF21540_EXECUTE_NOW value causes start procedure
 *                                    immediately.
 * @param[in] mode                  @ref NRF21540_EXEC_MODE_BLOCKING - function will wait for
 *                                    finishing configuration including settling times required
 *                                    by nRF21540 (waits till all procedure has finished).
 *                                  @ref NRF21540_EXEC_MODE_NON_BLOCKING - function will start
 *                                    procedure and set busy flag. User code can be executed
 *                                    at this time and busy flag will be unset when done.
 * @return                          NRF based error code.
 *                                  NRF_ERROR_INTERNAL when driver is in error state.
 *                                    Reinitialization is required.
 *                                  NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                                    to perform the operation (@sa nrf21540_state_t).
 *                                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_tx_set(uint32_t user_trigger_event, nrf21540_execution_mode_t mode);

/**@brief Set nRF21540 to TX mode.
 *
 * @note Dependently on configuration GPIO or SPI interface will be used
 *    (NRF21540_USE_SPI_MANAGEMENT/NRF21540_USE_GPIO_MANAGEMENT).
 *
 * @param[in] user_trigger_event    event that triggers start of procedure - this event
 *                                    will be connected to appropriate PPI channel.
 *                                    @ref NRF21540_EXECUTE_NOW value causes start procedure
 *                                    immediately.
 * @param[in] mode                  @ref NRF21540_EXEC_MODE_BLOCKING - function will wait for
 *                                    finishing configuration including settling times required
 *                                    by nRF21540 (waits till all procedure has finished).
 *                                  @ref NRF21540_EXEC_MODE_NON_BLOCKING - function will start
 *                                    procedure and set busy flag. User code can be executed
 *                                    at this time and busy flag will be unset when done.
 * @return                          NRF based error code.
 *                                  NRF_ERROR_INTERNAL when driver is in error state.
 *                                    Reinitialization is required.
 *                                  NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                                    to perform the operation (@sa nrf21540_state_t).
 *                                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_rx_set(uint32_t user_trigger_event, nrf21540_execution_mode_t mode);

/**@brief Function choses one of two physical antenna outputs.
 *
 * @param[in] antenna   One of antenna outputs. See @ref nrf21540_antenna_t.
 * @return              NRF based error code.
 *                      NRF_ERROR_BUSY when driver performs another operation at
 *                        the moment.
 *                      NRF_SUCCESS on success.
 */
ret_code_t nrf21540_ant_set(nrf21540_antenna_t antenna);

/**@brief Function choses one of two predefined power modes in nRF21540.
 *
 * @details Refer to nRF21540 Objective Product Specification, section: TX power control.
 *
 * @param[in] mode   Power mode. See @ref nrf21540_pwr_mode_t.
 * @return           NRF based error code.
 *                   NRF_ERROR_BUSY when driver performs another operation at
 *                     the moment.
 *                   NRF_SUCCESS on success.
 */
ret_code_t nrf21540_pwr_mode_set(nrf21540_pwr_mode_t mode);

/**@brief nRF21540 power down.
 *
 * @details Disables chip functionality and enter power save mode.
 *
 * @note Dependently on configuration GPIO or SPI interface will be used.
 *
 * @param[in] user_trigger_event    event that triggers start of procedure - this event
 *                                    will be connected to appropriate PPI channel.
 *                                    @ref NRF21540_EXECUTE_NOW value causes start procedure
 *                                    immediately.
 * @param[in] mode                  @ref NRF21540_EXEC_MODE_BLOCKING - function will wait for
 *                                    finishing configuration including settling times required
 *                                    by nRF21540 (waits till all procedure has finished).
 *                                  @ref NRF21540_EXEC_MODE_NON_BLOCKING - function will start
 *                                    procedure and set busy flag. User code can be executed
 *                                    at this time and busy flag will be unset when done.
 * @return                          NRF_ERROR_INTERNAL when driver is in error state.
 *                                    Reinitialization is required then.
 *                                  NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                                    to perform the operation (@sa nrf21540_state_t).
 *                                  NRF_ERROR_BUSY when driver performs another operation at
 *                                    the moment.
 *                                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_power_down(uint32_t user_trigger_event, nrf21540_execution_mode_t mode);

/**@brief Checks if nRF21540 driver is in error state.
 *
 * @return true if driver is in error state and should be reinitialized.
 */
bool nrf21540_is_error(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_H_
