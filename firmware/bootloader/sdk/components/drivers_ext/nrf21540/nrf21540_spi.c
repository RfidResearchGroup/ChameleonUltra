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
#include "nrf21540_spi.h"
#include <string.h>
#include "nrf_assert.h"
#include "boards.h"
#include "nrf_ppi.h"
#include "nrf21540_defs.h"
#include "nrf_timer.h"

#if NRF21540_USE_SPI_MANAGEMENT

static uint8_t       m_spi_tx_data[NRF21540_SPI_LENGTH_BYTES]; ///< SPI tx buffer.
static uint8_t       m_spi_rx_data[NRF21540_SPI_LENGTH_BYTES]; ///< SPI rx buffer.
static volatile bool m_spi_xfer_done;  ///< Flag indicates that SPI completed the transfer.

/**@brief Structure keeps content of important registers of nRF21540.
 *
 * @details Driver keeps this data because it needs to operate at single bits
 *          included in these registers (otherwise it should read it content
 *          during every operation).
 */

static struct {
    uint8_t CONFREG0; ///< CONFREG0 register's content.
    uint8_t CONFREG1; ///< CONFREG1 register's content.
} m_confreg_statics;

static const nrfx_spim_t spi = NRFX_SPIM_INSTANCE(NRF21540_SPIM_NO);  /**< SPI instance. */

/**@brief Function waits for SPI transfer has finished
 *
 * @details Used in blocking mode transfer
 */
static inline void wait_for_transfer_end(void)
{
    while (!m_spi_xfer_done)
    {}
    m_spi_xfer_done = false;
}

/**@brief Handler called by nrfx driver when SPI event occurs.
 *
 * @param[in] p_event   Event which triggers the handler.
 * @param[in] p_context Context.
 */
static void spim_event_handler(nrfx_spim_evt_t const *p_event, void *p_context)
{
    m_spi_xfer_done = true;
}

/**@brief Function reads the content of nRF21540 chip register.
 *
 * @details Preparation of read register operation. Every register has one byte size.
 *
 * @param[in] reg              Register address to read.
 * @param[in] mode             if NRF21540_EXEC_MODE_BLOCKING the function will wait for data
 *                             received.
 * @param[in] start_now        if enabled, transmision immediately initialized,
 *                             otherwise transfer will be triggered by external event.
 */
static uint8_t spi_reg_read(nrf21540_reg_t reg, nrf21540_execution_mode_t mode, bool start_now)
{
    ASSERT(!(mode == NRF21540_EXEC_MODE_BLOCKING && start_now == false));
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(m_spi_tx_data,
                                                         NRF21540_SPI_LENGTH_BYTES,
                                                         m_spi_rx_data,
                                                         NRF21540_SPI_LENGTH_BYTES);
    m_spi_tx_data[NRF21540_SPI_COMMAND_ADDR_BYTE] =
        (NRF21540_SPI_COMMAND_READ << NRF21540_SPI_COMMAND_Pos) | (reg << NRF21540_SPI_REG_Pos);
    (void)nrfx_spim_xfer(&spi, &xfer_desc, 0);
    if (mode == NRF21540_EXEC_MODE_BLOCKING)
    {
        wait_for_transfer_end();
    }
    return m_spi_rx_data[NRF21540_SPI_DATA_BYTE];
}

/**@brief Function writes the content of nRF21540 chip register.
 *
 * @details Preparation of data to send. Every register has one byte size.
 *
 * @param[in] reg              Register address to write.
 * @param[in] data             Data to write.
 * @param[in] mode             if NRF21540_EXEC_MODE_BLOCKING the function will wait for transfer
 *                             finished after sending data.
 * @param[in] start_now        if enabled, transmision immediately initialized,
 *                             otherwise transfer will be triggered by external event.
 */
static void spi_reg_write(nrf21540_reg_t reg, uint8_t data, nrf21540_execution_mode_t mode, bool start_now)
{
    ASSERT(!(mode == NRF21540_EXEC_MODE_BLOCKING && start_now == false));
    nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(m_spi_tx_data,
                                                         NRF21540_SPI_LENGTH_BYTES,
                                                         m_spi_rx_data,
                                                         NRF21540_SPI_LENGTH_BYTES);
    m_spi_tx_data[NRF21540_SPI_COMMAND_ADDR_BYTE] =
        (NRF21540_SPI_COMMAND_WRITE << NRF21540_SPI_COMMAND_Pos) | (reg << NRF21540_SPI_REG_Pos);
    m_spi_tx_data[NRF21540_SPI_DATA_BYTE] = data;
    uint32_t flags = start_now ? 0 : NRFX_SPIM_FLAG_HOLD_XFER;
    (void)nrfx_spim_xfer(&spi, &xfer_desc, flags);
    if (mode == NRF21540_EXEC_MODE_BLOCKING)
    {
        wait_for_transfer_end();
    }
}

/**@brief Function reads content of important nRF21540's registers and stores
 *        it to dedicated structure (@ref m_confreg_statics).
 *
 * @return Return NRF based error code.
 */
static ret_code_t m_confreg_statics_content_update(void)
{
    ret_code_t ret = nrf21540_pdn_drive(true, NRF21540_EXEC_MODE_BLOCKING);
    if (ret != NRF_SUCCESS)
    {
        return ret;
    }
    m_confreg_statics.CONFREG0 = spi_reg_read(NRF21540_REG_CONFREG0,
                                              NRF21540_EXEC_MODE_BLOCKING, true);
    m_confreg_statics.CONFREG1 = spi_reg_read(NRF21540_REG_CONFREG1,
                                              NRF21540_EXEC_MODE_BLOCKING, true);
    return nrf21540_pdn_drive(false, NRF21540_EXEC_MODE_BLOCKING);
}

ret_code_t nrf21540_spi_init(void)
{
    ret_code_t ret;
    nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
    spi_config.frequency      = NRF_SPIM_FREQ_4M;
    spi_config.ss_pin         = NRF21540_CS_PIN;
    spi_config.miso_pin       = NRF21540_MISO_PIN;
    spi_config.mosi_pin       = NRF21540_MOSI_PIN;
    spi_config.sck_pin        = NRF21540_CLK_PIN;
    spi_config.ss_active_high = false;
    ret = nrfx_spim_init(&spi, &spi_config, spim_event_handler, NULL);
    if (ret != NRFX_SUCCESS)
    {
        return NRF_ERROR_INTERNAL;
    }
    return m_confreg_statics_content_update();
}


/**@brief Function enables or disables nRF21540 TX mode.
 *
 * @details Preparation of appropriate register content and tranfer initialization.
 *
 * @param[in] state NRF21540_DISABLE/NRF21540_ENABLE causes TX mode disabled/enabled.
 */
static void tx_en_drive(nrf21540_bool_state_t state)
{
    uint8_t reg_val;
    if (state == NRF21540_ENABLE)
    {
        reg_val = m_confreg_statics.CONFREG0 | NRF21540_BITS_CONFREG0_TX_EN_Enable;
    }
    else
    {
        reg_val = m_confreg_statics.CONFREG0 &(~NRF21540_BITS_CONFREG0_TX_EN_Enable);
    }
    spi_reg_write(NRF21540_REG_CONFREG0, reg_val, NRF21540_EXEC_MODE_NON_BLOCKING, false);
}

/**@brief Function enables or disables nRF21540 RX mode.
 *
 * @details Preparation of appropriate register content and tranfer initialization.
 *
 * @param[in] state NRF21540_DISABLE/NRF21540_ENABLE causes RX mode disabled/enabled.
 */
static void rx_en_drive(nrf21540_bool_state_t state)
{
    uint8_t reg_val;
    if (state == NRF21540_ENABLE)
    {
        reg_val = m_confreg_statics.CONFREG1 | NRF21540_BITS_CONFREG1_RX_EN_Enable;
    }
    else
    {
        reg_val = m_confreg_statics.CONFREG1 &(~NRF21540_BITS_CONFREG1_RX_EN_Disable);
    }
    spi_reg_write(NRF21540_REG_CONFREG1, reg_val, NRF21540_EXEC_MODE_NON_BLOCKING, false);
}

inline uint32_t nrf21540_spim_trx_task_start_address_get(void)
{
    return nrfx_spim_start_task_get(&spi);
}

void nrf21540_spim_for_trx_configure(nrf21540_trx_t dir, nrf21540_bool_state_t required_state)
{
    if (dir == NRF21540_TX)
    {
        tx_en_drive(required_state);
    }
    else
    {
        rx_en_drive(required_state);
    }
    if (required_state == NRF21540_ENABLE)
    {
        uint32_t task_start_address = nrfx_spim_start_task_get(&spi);

        nrf_ppi_channel_endpoint_setup(NRF21540_TRX_PPI_CHANNEL,
            (uint32_t)nrf_timer_event_address_get(NRF21540_TIMER,
                                                  NRF21540_TIMER_CC_PD_PG_EVENT),
            task_start_address);
        nrf_ppi_channel_enable(NRF21540_TRX_PPI_CHANNEL);
    }
}

ret_code_t nrf21540_spi_pwr_mode_set(nrf21540_pwr_mode_t mode)
{
    if (mode == NRF21540_PWR_MODE_A)
    {
        spi_reg_write(NRF21540_REG_CONFREG0, NRF21540_BITS_CONFREG0_MODE_0,
                      NRF21540_EXEC_MODE_BLOCKING, true);
    }
    else if (mode == NRF21540_PWR_MODE_B)
    {
        spi_reg_write(NRF21540_REG_CONFREG0, NRF21540_BITS_CONFREG0_MODE_1,
                      NRF21540_EXEC_MODE_BLOCKING, true);
    }
    else
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    return NRF_SUCCESS;
}

#endif /*NRF21540_USE_SPI_MANAGEMENT*/
