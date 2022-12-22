/**
 * Copyright (c) 2018 - 2021, Nordic Semiconductor ASA
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
#include "pcal6408a.h"

static pcal6408a_instance_t * m_p_instances;
static uint8_t                m_max_instance_count;
static uint8_t                m_added_inst_count;

#define PCAL6408A_WRITE(p_instance, msg)            \
    nrf_twi_sensor_write(p_instance.p_sensor_data,  \
                         p_instance.sensor_addr,    \
                         msg,                       \
                         ARRAY_SIZE(msg),           \
                         true)

#define PCAL6408A_REG_OUTPUT_PORT_DEFAULT_VAL                     0xFF
#define PCAL6408A_REG_POLARITY_INVERSION_DEFAULT_VAL              0x00
#define PCAL6408A_REG_CONFIGURATION_DEFAULT_VAL                   0xFF
#define PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0_DEFAULT_VAL         0xFF
#define PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1_DEFAULT_VAL         0xFF
#define PCAL6408A_REG_INPUT_LATCH_DEFAULT_VAL                     0x00
#define PCAL6408A_REG_PULL_UP_DOWN_ENABLE_DEFAULT_VAL             0x00
#define PCAL6408A_REG_PULL_UP_DOWN_SELECT_DEFAULT_VAL             0xFF
#define PCAL6408A_REG_INTERRUPT_MASK_DEFAULT_VAL                  0xFF
#define PCAL6408A_REG_INTERRUPT_STATUS_DEFAULT_VAL                0x00
#define PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION_DEFAULT_VAL       0x00

/**
 * ================================================================================================
 * @brief General expander utility functions.
 */

void pcal6408a_init(pcal6408a_instance_t * p_instances, uint8_t count)
{
    ASSERT(p_instances != NULL);
    m_p_instances        = p_instances;
    m_max_instance_count = count;
    m_added_inst_count   = 0;
}

static void pcal6408a_default_cfg_set(uint8_t instance_num)
{
    m_p_instances[instance_num].registers[1]  = PCAL6408A_REG_OUTPUT_PORT_DEFAULT_VAL;
    m_p_instances[instance_num].registers[2]  = PCAL6408A_REG_POLARITY_INVERSION_DEFAULT_VAL;
    m_p_instances[instance_num].registers[3]  = PCAL6408A_REG_CONFIGURATION_DEFAULT_VAL;
    m_p_instances[instance_num].registers[4]  = PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0_DEFAULT_VAL;
    m_p_instances[instance_num].registers[5]  = PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1_DEFAULT_VAL;
    m_p_instances[instance_num].registers[6]  = PCAL6408A_REG_INPUT_LATCH_DEFAULT_VAL;
    m_p_instances[instance_num].registers[7]  = PCAL6408A_REG_PULL_UP_DOWN_ENABLE_DEFAULT_VAL;
    m_p_instances[instance_num].registers[8]  = PCAL6408A_REG_PULL_UP_DOWN_SELECT_DEFAULT_VAL;
    m_p_instances[instance_num].registers[9]  = PCAL6408A_REG_INTERRUPT_MASK_DEFAULT_VAL;
    m_p_instances[instance_num].registers[10] = PCAL6408A_REG_INTERRUPT_STATUS_DEFAULT_VAL;
    m_p_instances[instance_num].registers[11] = PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION_DEFAULT_VAL;
}

ret_code_t pcal6408a_add_instance(nrf_twi_sensor_t * p_twi_sensor, uint8_t sensor_address)
{
    ASSERT(p_twi_sensor != NULL);

    if (m_p_instances == NULL)
    {
        return NRF_ERROR_MODULE_NOT_INITIALIZED;
    }

    if (m_added_inst_count >= m_max_instance_count)
    {
        return NRF_ERROR_STORAGE_FULL;
    }

    m_p_instances[m_added_inst_count].p_sensor_data = p_twi_sensor;
    m_p_instances[m_added_inst_count].sensor_addr   = sensor_address;
    pcal6408a_default_cfg_set(m_added_inst_count);
    m_added_inst_count++;
    ret_code_t err_code = pcal6408a_cfg_write(m_added_inst_count - 1);

    return err_code;
}

ret_code_t pcal6408a_cfg_write(uint8_t instance_num)
{
    if (instance_num >= m_added_inst_count)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    ret_code_t err_code;

    for (uint8_t i = PCAL6408A_REG_OUTPUT_PORT; i <= PCAL6408A_REG_CONFIGURATION; i++)
    {
        err_code = nrf_twi_sensor_reg_write(m_p_instances[instance_num].p_sensor_data,
                                            m_p_instances[instance_num].sensor_addr,
                                            i,
                                            &m_p_instances[instance_num].registers[i],
                                            1);

        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        while (nrf_twi_mngr_is_idle(m_p_instances[instance_num].p_sensor_data->p_twi_mngr) != true)
        {
            // Wait for transaction to finish to not overflow msg buffer
        }
    }

    for (uint8_t i = PCAL6408A_REG_COUNT_SEQUENCE_1; i <= (PCAL6408A_REG_COUNT_ALL - 3); i++)
    {
        err_code = nrf_twi_sensor_reg_write(
                        m_p_instances[instance_num].p_sensor_data,
                        m_p_instances[instance_num].sensor_addr,
                        i - PCAL6408A_REG_COUNT_SEQUENCE_1 + PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0,
                        &m_p_instances[instance_num].registers[i],
                        1);

        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        while (nrf_twi_mngr_is_idle(m_p_instances[instance_num].p_sensor_data->p_twi_mngr) != true)
        {
            // Wait for transaction to finish to not overflow msg buffer
        }
    }

    err_code = nrf_twi_sensor_reg_write(
                m_p_instances[instance_num].p_sensor_data,
                m_p_instances[instance_num].sensor_addr,
                PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION,
                &m_p_instances[instance_num].registers[PCAL6408A_REG_COUNT_ALL - 1],
                1);

    end:
    return err_code;
}

ret_code_t pcal6408a_cfg_read(uint8_t instance_num)
{
    if (instance_num >= m_added_inst_count)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    ret_code_t err_code;

    for (uint8_t i = 0; i <= PCAL6408A_REG_CONFIGURATION; i++)
    {
        err_code = nrf_twi_sensor_reg_read(m_p_instances[instance_num].p_sensor_data,
                                           m_p_instances[instance_num].sensor_addr,
                                           i,
                                           NULL,
                                           &m_p_instances[instance_num].registers[i],
                                           1);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        while (nrf_twi_mngr_is_idle(m_p_instances[instance_num].p_sensor_data->p_twi_mngr) != true)
        {
            // Wait for transaction to finish to not overflow msg buffer
        }
    }

    for(uint8_t i = PCAL6408A_REG_COUNT_SEQUENCE_1; i <= (PCAL6408A_REG_COUNT_ALL - 3); i++)
    {
        err_code = nrf_twi_sensor_reg_read(
                    m_p_instances[instance_num].p_sensor_data,
                    m_p_instances[instance_num].sensor_addr,
                    i - PCAL6408A_REG_COUNT_SEQUENCE_1 + PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0,
                    NULL,
                    &m_p_instances[instance_num].registers[i],
                    1);

        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        while (nrf_twi_mngr_is_idle(m_p_instances[instance_num].p_sensor_data->p_twi_mngr) != true)
        {
            // Wait for transaction to finish to not overflow msg buffer
        }
    }

    err_code = nrf_twi_sensor_reg_read(
                m_p_instances[instance_num].p_sensor_data,
                m_p_instances[instance_num].sensor_addr,
                PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION,
                NULL,
                &m_p_instances[instance_num].registers[PCAL6408A_REG_COUNT_ALL - 1],
                1);
    end:
    return err_code;
}

ret_code_t pcal6408a_pin_data_update(nrf_twi_sensor_reg_cb_t user_cb)
{
    ret_code_t err_code;
    for (uint8_t i = 0; i < m_added_inst_count - 1; i++)
    {
        err_code = nrf_twi_sensor_reg_read(m_p_instances[i].p_sensor_data,
                                           m_p_instances[i].sensor_addr,
                                           PCAL6408A_REG_INPUT_PORT,
                                           NULL,
                                           &m_p_instances[i].registers[PCAL6408A_REG_INPUT_PORT],
                                           1);
        if (err_code != NRF_SUCCESS)
        {
            return err_code;
        }
    }
    return nrf_twi_sensor_reg_read(
            m_p_instances[m_added_inst_count - 1].p_sensor_data,
            m_p_instances[m_added_inst_count - 1].sensor_addr,
            PCAL6408A_REG_INPUT_PORT,
            user_cb,
            &m_p_instances[m_added_inst_count - 1].registers[PCAL6408A_REG_INPUT_PORT],
            1);
}

ret_code_t pcal6408a_int_status_update(nrf_twi_sensor_reg_cb_t user_cb)
{
    ret_code_t err_code;
    for (uint8_t i = 0; i < m_added_inst_count - 1; i++)
    {
        err_code = nrf_twi_sensor_reg_read(m_p_instances[i].p_sensor_data,
                                           m_p_instances[i].sensor_addr,
                                           PCAL6408A_REG_INTERRUPT_STATUS,
                                           NULL,
                                           &m_p_instances[i].registers[10],
                                           1);
        if (err_code != NRF_SUCCESS)
        {
            return err_code;
        }
    }
    return nrf_twi_sensor_reg_read(
            m_p_instances[m_added_inst_count - 1].p_sensor_data,
            m_p_instances[m_added_inst_count - 1].sensor_addr,
            PCAL6408A_REG_INTERRUPT_STATUS,
            user_cb,
            &m_p_instances[m_added_inst_count - 1].registers[10],
            1);
}

static uint8_t * get_reg_address(pcal6408a_registers_t reg_addr, uint8_t inst_num)
{
    if (reg_addr <= PCAL6408A_REG_CONFIGURATION)
    {
        return &m_p_instances[inst_num].registers[reg_addr];
    }
    else if (reg_addr == PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION)
    {
        return &m_p_instances[inst_num].registers[PCAL6408A_REG_COUNT_ALL - 1];
    }
    else if (reg_addr >= PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0
             && reg_addr <= PCAL6408A_REG_INTERRUPT_STATUS)
    {
        return &m_p_instances[inst_num].registers[PCAL6408A_REG_COUNT_SEQUENCE_1
                                                  + reg_addr
                                                  - PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0];
    }
    return NULL;
}

ret_code_t pcal6408a_pin_cfg_reg_set(pcal6408a_registers_t reg_addr, uint32_t pin, uint8_t value)
{
    ASSERT(pin <= (PCAL6408A_INNER_PIN_COUNT * m_added_inst_count));

    uint8_t * p_reg_val;
    uint8_t   mask;
    uint8_t   inst_num = pin / PCAL6408A_INNER_PIN_COUNT;

    pin %= PCAL6408A_INNER_PIN_COUNT;

    if (reg_addr == PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0)
    {
        if (pin > PCAL6408A_DRIVE_STRENGTH_REG_0_PIN_MAX)
        {
            return NRF_ERROR_INVALID_PARAM;
        }

        mask = 3; // Current control register parameter is 2 bits long.
        pin *= 2;
    }
    else if (reg_addr == PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1)
    {
        if (pin <= PCAL6408A_DRIVE_STRENGTH_REG_0_PIN_MAX)
        {
            return NRF_ERROR_INVALID_PARAM;
        }

        mask = 3; // Current control register parameter is 2 bits long.
        pin %= PCAL6408A_INNER_PIN_COUNT / 2;
        pin *= 2;
    }
    else
    {
        mask = 1;
    }

    p_reg_val = get_reg_address(reg_addr, inst_num);

    if (p_reg_val == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    NRF_TWI_SENSOR_REG_SET(*p_reg_val, (uint8_t) (mask << pin), pin, value);

    uint8_t send_msg[] = {
        reg_addr,
        *p_reg_val
    };

    return PCAL6408A_WRITE(m_p_instances[inst_num], send_msg);
}

uint8_t pcal6408a_pin_cfg_reg_get(pcal6408a_registers_t reg_addr, uint32_t pin)
{
    ASSERT(pin <= (PCAL6408A_INNER_PIN_COUNT * m_added_inst_count));

    uint8_t *  p_reg_val;
    uint8_t    mask     = 1;
    uint8_t    inst_num = pin / PCAL6408A_INNER_PIN_COUNT;

    pin %= PCAL6408A_INNER_PIN_COUNT;
    if (reg_addr == PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0
        || reg_addr == PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1)
    {
        if (pin > PCAL6408A_DRIVE_STRENGTH_REG_0_PIN_MAX)
        {
            reg_addr = PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1;
            pin %= PCAL6408A_INNER_PIN_COUNT / 2;
        }
        else
        {
            reg_addr = PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0;
        }

        mask = 3; //Current control register parameter is 2 bits long.
        pin *= 2;
    }

    p_reg_val = get_reg_address(reg_addr, inst_num);

    if (p_reg_val == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    return NRF_TWI_SENSOR_REG_VAL_GET(*p_reg_val, (mask << pin), pin);
}

ret_code_t pcal6408a_port_cfg_reg_set(pcal6408a_registers_t reg_addr,
                                      uint32_t              port,
                                      uint8_t               mask,
                                      pcal6408a_port_op_t   flag)
{
    if (port >= m_added_inst_count)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    uint8_t * p_reg_val;
    uint8_t   inst_num = port;

    p_reg_val = get_reg_address(reg_addr, inst_num);

    if (p_reg_val == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    switch (flag)
    {
        case PCAL6408A_PORT_WRITE:
            *p_reg_val = mask;
            break;
        case PCAL6408A_PORT_CLEAR:
            *p_reg_val &= ~mask;
            break;
        case PCAL6408A_PORT_SET:
            *p_reg_val |= mask;
            break;
        default:
            break;
    }

    uint8_t send_msg[] = {
        reg_addr,
        *p_reg_val
    };

    return PCAL6408A_WRITE(m_p_instances[inst_num], send_msg);
}

uint8_t pcal6408a_port_cfg_reg_get(pcal6408a_registers_t reg_addr, uint32_t port)
{
    ASSERT(port < m_added_inst_count);

    uint8_t   inst_num = port;
    uint8_t * p_reg_val;

    p_reg_val = get_reg_address(reg_addr, inst_num);

    if (p_reg_val == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    return *p_reg_val;
}

ret_code_t pcal6408a_pin_cfg_drive_strength(uint32_t                       pin_number,
                                            pcal6408a_pin_drive_strength_t drive_strength_config)
{
    ret_code_t err_code;

    if ((pin_number % PCAL6408A_INNER_PIN_COUNT) <= PCAL6408A_DRIVE_STRENGTH_REG_0_PIN_MAX)
    {
        err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0,
                                             pin_number,
                                             drive_strength_config);
    }
    else
    {
        err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1,
                                             pin_number,
                                             drive_strength_config);
    }
    return err_code;
}

ret_code_t pcal6408a_port_cfg_drive_strength(uint32_t            port_number,
                                             uint16_t            drive_strength_mask,
                                             pcal6408a_port_op_t flag)
{
    ret_code_t err_code;
    err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0,
                                          port_number,
                                          (uint8_t)drive_strength_mask, flag);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1,
                                          port_number,
                                          (uint8_t)(drive_strength_mask >> 8), flag);
    return err_code;
}


/**
 * ===============================================================================================
 * @brief Functions compatible with nrf_gpio
 */

ret_code_t pcal6408a_pin_cfg_input(uint32_t pin_number, pcal6408a_pin_pull_t pull_config)
{
    ret_code_t err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                                    pin_number,
                                                    PCAL6408A_PIN_DIR_INPUT);
    if (err_code != NRF_SUCCESS)
    {
        goto end;
    }

    switch (pull_config)
    {
        case PCAL6408A_PIN_NOPULL:
            err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE, pin_number, 0);
            break;

        case PCAL6408A_PIN_PULLDOWN:
            err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE, pin_number, 1);
            if (err_code != NRF_SUCCESS)
            {
                goto end;
            }

            err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_SELECT, pin_number, 0);
            break;

        case PCAL6408A_PIN_PULLUP:
            err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE, pin_number, 1);
            if (err_code != NRF_SUCCESS)
            {
                goto end;
            }
            err_code = pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_SELECT, pin_number, 1);
            break;

        default:
            break;
    }
    end:
    return err_code;
}

ret_code_t pcal6408a_range_cfg_output(uint32_t pin_range_start, uint32_t pin_range_end)
{
    if (pin_range_start > pin_range_end)
    {
        return NRF_ERROR_INVALID_LENGTH;
    }

    uint8_t start_port = pin_range_start / PCAL6408A_INNER_PIN_COUNT;
    uint8_t end_port   = pin_range_end / PCAL6408A_INNER_PIN_COUNT;
    uint8_t range_value;
    ret_code_t err_code;

    if (start_port == end_port)
    {
        range_value = (0xFF >> (PCAL6408A_INNER_PIN_COUNT - pin_range_end - 1)) &
                      (0xFF << pin_range_start);
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              start_port,
                                              range_value,
                                              PCAL6408A_PORT_CLEAR);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }
    else
    {
        range_value = 0xFF << pin_range_start;
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              start_port,
                                              range_value,
                                              PCAL6408A_PORT_CLEAR);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        range_value = 0xFF >> (PCAL6408A_INNER_PIN_COUNT - pin_range_end - 1);
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              start_port,
                                              range_value,
                                              PCAL6408A_PORT_CLEAR);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }

    range_value = 0xFF;
    for (uint8_t i = (start_port + 1); i < end_port; i++)
    {
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              i,
                                              range_value,
                                              PCAL6408A_PORT_CLEAR);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }

    end:
    return err_code;
}

static ret_code_t pcal6408a_port_pull_cfg_set(uint32_t             port,
                                              uint8_t              mask,
                                              pcal6408a_pin_pull_t pull_config)
{
    ret_code_t err_code = NRF_SUCCESS;
    switch (pull_config)
    {
        case PCAL6408A_PIN_NOPULL:
            err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE,
                                                  port,
                                                  mask,
                                                  PCAL6408A_PORT_CLEAR);
            break;

        case PCAL6408A_PIN_PULLDOWN:
            err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE,
                                                  port,
                                                  mask,
                                                  PCAL6408A_PORT_SET);
            if (err_code != NRF_SUCCESS)
            {
                goto end;
            }

            err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_SELECT,
                                                  port,
                                                  mask,
                                                  PCAL6408A_PORT_CLEAR);
            break;

        case PCAL6408A_PIN_PULLUP:
            err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_ENABLE,
                                                  port,
                                                  mask,
                                                  PCAL6408A_PORT_SET);
            if (err_code != NRF_SUCCESS)
            {
                goto end;
            }

            err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_PULL_UP_DOWN_SELECT,
                                                  port,
                                                  mask,
                                                  PCAL6408A_PORT_SET);
            break;

        default:
            break;
    }

    end:
    return err_code;
}

ret_code_t pcal6408a_range_cfg_input(uint32_t             pin_range_start,
                                     uint32_t             pin_range_end,
                                     pcal6408a_pin_pull_t pull_config)
{
    if (pin_range_start > pin_range_end)
    {
        return NRF_ERROR_INVALID_LENGTH;
    }

    uint8_t start_port = pin_range_start / PCAL6408A_INNER_PIN_COUNT;
    uint8_t end_port   = pin_range_end / PCAL6408A_INNER_PIN_COUNT;
    uint8_t range_value;
    ret_code_t err_code = NRF_SUCCESS;

    if (start_port == end_port)
    {
        range_value = (0xFF >> (PCAL6408A_INNER_PIN_COUNT - pin_range_end - 1))
                      & (0xFF << pin_range_start);
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              start_port,
                                              range_value,
                                              PCAL6408A_PORT_SET);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        err_code = pcal6408a_port_pull_cfg_set(start_port, range_value, pull_config);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }
    else
    {
        range_value = 0xFF << pin_range_start;
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              start_port,
                                              range_value,
                                              PCAL6408A_PORT_SET);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }

        err_code = pcal6408a_port_pull_cfg_set(start_port, range_value, pull_config);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
        range_value = 0xFF >> (PCAL6408A_INNER_PIN_COUNT - pin_range_end - 1);
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              end_port,
                                              range_value,
                                              PCAL6408A_PORT_SET);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
        err_code = pcal6408a_port_pull_cfg_set(end_port, range_value, pull_config);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }

    range_value = 0xFF;
    for (uint8_t i = (start_port + 1); i < end_port; i++)
    {
        err_code = pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                              i,
                                              range_value,
                                              PCAL6408A_PORT_SET);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
        err_code = pcal6408a_port_pull_cfg_set(i, range_value, pull_config);
        if (err_code != NRF_SUCCESS)
        {
            goto end;
        }
    }

    end:
    return err_code;
}



