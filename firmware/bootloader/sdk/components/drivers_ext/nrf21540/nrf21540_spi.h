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
#ifndef NRF21540_SPI_H_
#define NRF21540_SPI_H_

#include <stdbool.h>
#include <stdint.h>
#include "nrfx_spim.h"
#include "nrf21540_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nRF21540 SPI interface parameters defines.
 */
#define NRF21540_SPI_LENGTH_BYTES 2         ///< SPI tx/rx buffer size in bytes.
#define NRF21540_SPI_COMMAND_ADDR_BYTE 0    ///< Position of command field in SPI frame.
#define NRF21540_SPI_DATA_BYTE 1            ///< Position of data field in SPI frame.
#define NRF21540_SPI_COMMAND_Pos 6          ///< Command code bit-position in command field.
#define NRF21540_SPI_REG_Pos 0              ///< Register address bit-position in command field.
#define NRF21540_SPI_COMMAND_NOP 0x00       ///< 'NOP' command code.
#define NRF21540_SPI_COMMAND_READ 0x02      ///< 'READ' command code.
#define NRF21540_SPI_COMMAND_WRITE 0x03     ///< 'WRITE' command code.

/**@brief CONFREG0 register bitfields.
 */
#define NRF21540_BITS_CONFREG0_TX_EN_Pos 0     ///< Position of TX_EN field.
#define NRF21540_BITS_CONFREG0_TX_EN_Msk (1 << NRF21540_BITS_CONFREG0_TX_EN_Pos) ///< Bit mask of TX_EN field.
#define NRF21540_BITS_CONFREG0_TX_EN_Disable 0 ///< Disable TX mode.
#define NRF21540_BITS_CONFREG0_TX_EN_Enable 1  ///< Enable TX mode.

#define NRF21540_BITS_CONFREG0_MODE_Pos 1 ///< Position of MODE field.
#define NRF21540_BITS_CONFREG0_MODE_Msk (1 << NRF21540_BITS_CONFREG0_MODE_Pos) ///< Bit mask of MODE field.
#define NRF21540_BITS_CONFREG0_MODE_0 0   ///< Selects MODE 0.
#define NRF21540_BITS_CONFREG0_MODE_1 1   ///< Selects MODE 1.

#define NRF21540_BITS_CONFREG0_TX_GAIN_Pos 2  ///< Position of TX_GAIN field.
#define NRF21540_BITS_CONFREG0_TX_GAIN_Msk (0x1F << NRF21540_BITS_CONFREG0_TX_GAIN_Pos) ///< Bit mask of TX_GAIN field.
#define NRF21540_BITS_CONFREG0_TX_GAIN_Min 0  ///< Minimum TX_GAIN register value
#define NRF21540_BITS_CONFREG0_TX_GAIN_Max 31 ///< Maximum TX_GAIN register value

/**@brief CONFREG1 register bitfields.
 */
#define NRF21540_BITS_CONFREG1_RX_EN_Pos 0     ///< Position of RX_EN field.
#define NRF21540_BITS_CONFREG1_RX_EN_Msk (1 << NRF21540_BITS_CONFREG1_RX_EN_Pos) ///< Bit mask of TX_EN field.
#define NRF21540_BITS_CONFREG1_RX_EN_Disable 0 ///< Disable RX mode.
#define NRF21540_BITS_CONFREG1_RX_EN_Enable 1  ///< Enable RX mode.

#define NRF21540_BITS_CONFREG1_UICR_EN_Pos 2     ///< Position of UICR_EN field.
#define NRF21540_BITS_CONFREG1_UICR_EN_Msk (1 << NRF21540_BITS_CONFREG1_UICR_EN_Pos) ///< Bit mask of UICR_EN field.
#define NRF21540_BITS_CONFREG1_UICR_EN_Disable 0 ///< Disable UICR program mode.
#define NRF21540_BITS_CONFREG1_UICR_EN_Enable 1  ///< Enable UICR program mode.

#define NRF21540_BITS_CONFREG1_KEY_Pos 4    ///< Position of KEY field.
#define NRF21540_BITS_CONFREG1_KEY_Msk (0x0F << NRF21540_BITS_CONFREG1_KEY_Pos) ///< Bit mask of KEY field.
#define NRF21540_BITS_CONFREG1_KEY_Enter 15 ///< Enter UICR program mode.
#define NRF21540_BITS_CONFREG1_KEY_Leave 0  ///< Leave UICR program mode.

/**@brief CONFREG2 register bitfields.
 */
#define NRF21540_BITS_CONFREG2_POUTA_UICR_Pos 0  ///< Position of POUTA_UICR field.
#define NRF21540_BITS_CONFREG2_POUTA_UICR_Msk (0x1F << NRF21540_BITS_CONFREG2_POUTA_UICR_Pos) ///< Bit mask of POUTA_UICR field.
#define NRF21540_BITS_CONFREG2_POUTA_UICR_Min 0  ///< Minimum POUTA_UICR register value
#define NRF21540_BITS_CONFREG2_POUTA_UICR_Max 31 ///< Maximum POUTA_UICR register value

#define NRF21540_BITS_CONFREG2_POUTA_SEL_Pos 5  ///< Position of POUTA_SEL field.
#define NRF21540_BITS_CONFREG2_POUTA_SEL_Msk (1 << NRF21540_BITS_CONFREG2_POUTA_SEL_Pos) ///< Bit mask of POUTA_SEL field.
#define NRF21540_BITS_CONFREG2_POUTA_SEL_PROD 0 ///< Initialize TX_GAIN register with 20dBm value.
#define NRF21540_BITS_CONFREG2_POUTA_SEL_UICR 1 ///< Initialize TX_GAIN register with POUTA_UICR value.

#define NRF21540_BITS_CONFREG2_WR_UICR_Pos 7   ///< Position of WR_UICR field.
#define NRF21540_BITS_CONFREG2_WR_UICR_Msk (1 << NRF21540_BITS_CONFREG2_WR_UICR_Pos) ///< Bit mask of WR_UICR field.
#define NRF21540_BITS_CONFREG2_WR_UICR_IDLE 0  ///< EFUSE idle .
#define NRF21540_BITS_CONFREG2_WR_UICR_WRITE 1 ///< EFUSE write.

/**@brief CONFREG3 register bitfields.
 */
#define NRF21540_BITS_CONFREG3_POUTB_UICR_Pos 0   ///< Position of POUTB_UICR field.
#define NRF21540_BITS_CONFREG3_POUTB_UICR_Msk (0x1F << NRF21540_BITS_CONFREG3_POUTB_SEL_Pos) ///< Bit mask of POUTB_UICR field.
#define NRF21540_BITS_CONFREG3_POUTB_UICR_Min 0   ///< Minimum POUTB_UICR register value
#define NRF21540_BITS_CONFREG3_POUTB_UICR_Max 31  ///< Maximum POUTB_UICR register value

#define NRF21540_BITS_CONFREG3_POUTB_SEL_Pos 5  ///< Position of POUTB_SEL field.
#define NRF21540_BITS_CONFREG3_POUTB_SEL_Msk (1 << NRF21540_BITS_CONFREG3_POUTB_SEL_Pos) ///< Bit mask of POUTB_SEL field.
#define NRF21540_BITS_CONFREG3_POUTB_SEL_PROD 0 ///< Initialize TX_GAIN register with 20dBm value.
#define NRF21540_BITS_CONFREG3_POUTB_SEL_UICR 1 ///< Initialize TX_GAIN register with POUTB_UICR value.

/**@brief PARTNUMBER register bitfields.
 */
#define NRF21540_PARTNUMBER_PARTNUMBER_Pos 0 ///< Position of PARTNUMBER field.
#define NRF21540_PARTNUMBER_PARTNUMBER_Msk (0xFF << NRF21540_PARTNUMBER_PARTNUMBER_Pos) ///< Bit mask of PARTNUMBER field.

/**@brief HW_REVISON register bitfields.
 */
#define NRF21540_HW_REVISON_HW_REVISION_Pos 4 ///< Position of HW_REVISON field.
#define NRF21540_HW_REVISON_HW_REVISION_Msk (0xF << NRF21540_HW_REVISON_HW_REVISION_Pos) ///< Bit mask of HW_REVISON field.

/**@brief HW_ID0 register bitfields.
 */
#define NRF21540_HW_ID0_Pos 0 ///< Position of HW_ID0 field.
#define NRF21540_HW_ID0_Msk (0xFF << NRF21540_HW_ID0_Pos) ///< Bit mask of HW_ID0 field.

/**@brief HW_ID1 register bitfields.
 */
#define NRF21540_HW_ID1_Pos 0 ///< Position of HW_ID1 field.
#define NRF21540_HW_ID1_Msk (0xFF << NRF21540_HW_ID1_Pos) ///< Bit mask of HW_ID1 field.

/**@brief nRF21540 internal registers.
 */
typedef enum
{
    NRF21540_REG_CONFREG0 = 0x00,     ///< CONFREG0 register address.
    NRF21540_REG_CONFREG1 = 0x01,     ///< CONFREG1 register address.
    NRF21540_REG_CONFREG2 = 0x02,     ///< CONFREG2 register address.
    NRF21540_REG_CONFREG3 = 0x03,     ///< CONFREG3 register address.
    NRF21540_REG_PARTNUMBER = 0x14,   ///< PARTNUMBER register address.
    NRF21540_REG_HW_REVISION = 0x15,  ///< HW_REVISION register address.
    NRF21540_REG_HW_ID0 = 0x16,       ///< HW_ID0 register address.
    NRF21540_REG_HW_ID1 = 0x17,       ///< HW_ID1 register address.
} nrf21540_reg_t;

/**@brief Function initializes SPI interface.
 *
 * @return     NRF_ERROR_INTERNAL when SPIM driver initialization error occured.
 *             NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *               to perform the operation.
 *             NRF_SUCCESS on success.
 */
ret_code_t nrf21540_spi_init(void);

/**@brief Function returns address of task which triggers SPI transfer.
 *
 * @return address of appropriate task.
 */
uint32_t nrf21540_spim_trx_task_start_address_get(void);

/**@brief Function configures the chip and peripherals for TX/RX transfer purpose.
 *
 * @details It can enable/disable RX/TX transfers.
 *
 * @param[in] dir             Direction of the radio transmission. See @ref nrf21540_trx_t.
 * @param[in] required_state  State of RX/TX transfer. See @ref nrf21540_bool_state_t.
 *                            chosen transfer type.
 */
void nrf21540_spim_for_trx_configure(nrf21540_trx_t dir, nrf21540_bool_state_t required_state);

/**@brief Function choses one of predefined power modes in nRF21540.
 *
 * @details Refer to nRF21540 Objective Product Specification, section: TX power control.
 *
 * @param[in] mode  Power mode. See @ref nrf21540_pwr_mode_t.
 * @return          NRF_ERROR_INVALID_PARAM when invalid argument given.
 *                  NRF_SUCCESS on success.
 */
ret_code_t nrf21540_spi_pwr_mode_set(nrf21540_pwr_mode_t mode);

/**@brief Function sets nRF21540 power state by driving PDN pin.
 *
 * @param[in] state   Required PDN pin state.
 * @param[in] mode    Execution mode. See @ref nrf21540_execution_mode_t.
 * @return            NRF_ERROR_INVALID_PARAM when invalid argument given.
 *                    NRF_ERROR_INVALID_STATE when nRF21540's state isn't proper
 *                      to perform the operation (@sa nrf21540_state_t).
 *                    NRF_ERROR_INTERNAL when driver is in error state.
 *                      Reinitialization is required.
 *                    NRF_SUCCESS on success.
 */
ret_code_t nrf21540_pdn_drive(bool state, nrf21540_execution_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_SPI_H_
