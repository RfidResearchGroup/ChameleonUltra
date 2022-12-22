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
#ifndef NRF21540_TYPES_H_
#define NRF21540_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * @defgroup nrf21540_types nRF21540 front-end Bluetooth range extender types
 * @{
 * @ingroup nrf21540
 */

/**@brief Value used as event zero-address - for immediate function execution.
 *        This is useful in functions with 'user_trigger_event' input parameter.
 */
#define NRF21540_EXECUTE_NOW    ((uint32_t)0)

/**@brief nRF21540 antenna outputs.
 *
 * @note Read more in the Product Specification.
 */
typedef enum
{
    NRF21540_ANT1, ///< Antenna 1 output.
    NRF21540_ANT2  ///< Antenna 2 output.
} nrf21540_antenna_t;


/**@brief nRF21540 power modes.
 *
 * @note Read more in the Product Specification.
 */
typedef enum
{
    NRF21540_PWR_MODE_A, ///< Power mode A.
    NRF21540_PWR_MODE_B  ///< Power mode B.
} nrf21540_pwr_mode_t;


/**@brief nRF21540 transmission direction modes.
 */
typedef enum
{
    NRF21540_TX, ///< Transmission direction mode transmit.
    NRF21540_RX  ///< Transmission direction mode receive.
} nrf21540_trx_t;

/**@brief State type for nRF21540 purposes.
 */
typedef enum
{
    NRF21540_DISABLE, ///< State disable.
    NRF21540_ENABLE   ///< State enable.
} nrf21540_bool_state_t;

/**@brief Modes (blocking/non-blocking) for nRF21540 purposes.
 */
typedef enum
{
    NRF21540_EXEC_MODE_NON_BLOCKING, ///< Non-blocking execution mode.
    NRF21540_EXEC_MODE_BLOCKING      ///< Blocking execution mode.
} nrf21540_execution_mode_t;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_TYPES_H_
