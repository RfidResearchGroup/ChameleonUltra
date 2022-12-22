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
#ifndef PCAL6408A_H__
#define PCAL6408A_H__

#include "nrf_twi_sensor.h"
#include "pcal6408a_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup pcal6408a_driver PCAL6408A Driver
 * @ingroup ext_drivers
 *
 * @brief Module for configuring and using PCAL6408A GPIO expander.
 *
 * @{
 */

/**
 * @brief First possible expander address.
 */
#define PCAL6408A_BASE_ADDRESS_FIRST      0x20u
/**
 * @brief Second possible expander address.
 */
#define PCAL6408A_BASE_ADDRESS_SECOND     0x21u

/**
 * @brief Device registers.
 */
typedef enum
{
    PCAL6408A_REG_INPUT_PORT                    = 0x00,
    PCAL6408A_REG_OUTPUT_PORT                   = 0x01,
    PCAL6408A_REG_POLARITY_INVERSION            = 0x02,
    PCAL6408A_REG_CONFIGURATION                 = 0x03,
    PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_0       = 0x40,
    PCAL6408A_REG_OUTPUT_DRIVE_STRENGTH_1       = 0x41,
    PCAL6408A_REG_INPUT_LATCH                   = 0x42,
    PCAL6408A_REG_PULL_UP_DOWN_ENABLE           = 0x43,
    PCAL6408A_REG_PULL_UP_DOWN_SELECT           = 0x44,
    PCAL6408A_REG_INTERRUPT_MASK                = 0x45,
    PCAL6408A_REG_INTERRUPT_STATUS              = 0x46,
    PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION     = 0x4F
} pcal6408a_registers_t;

/**
 * @brief Enumerator used for setting the direction of a pin.
 */
typedef enum
{
    PCAL6408A_PIN_DIR_OUTPUT,   /**< Output. */
    PCAL6408A_PIN_DIR_INPUT     /**< Input. */
} pcal6408a_pin_dir_t;

/**
 * @brief Enumerator used for setting the direction of a port.
 */
typedef enum
{
    PCAL6408A_PORT_DIR_OUTPUT = 0x00,   /**< Output. */
    PCAL6408A_PORT_DIR_INPUT  = 0xFF    /**< Input. */
} pcal6408a_port_dir_t;

/**
 * @brief Enumerator used for setting the state of a pin configured as an output.
 */
typedef enum
{
    PCAL6408A_PIN_CLR,  /**< Clear. */
    PCAL6408A_PIN_SET   /**< Set. */
} pcal6408a_pin_set_t;

/**
 * @brief Enumerator used for selecting the pin to be pulled down or up.
 */
typedef enum
{
    PCAL6408A_PIN_NOPULL,   /**< No pull. */
    PCAL6408A_PIN_PULLDOWN, /**< Pin pulldown resistor enabled. */
    PCAL6408A_PIN_PULLUP    /**< Pin pullup resistor enabled. */
} pcal6408a_pin_pull_t;

/**
 * @brief Enumerator used for selecting the operation for a port.
 */
typedef enum
{
    PCAL6408A_PORT_WRITE,   /**< Mask is written to the port. */
    PCAL6408A_PORT_CLEAR,   /**< Positive bits in mask are cleared in port. */
    PCAL6408A_PORT_SET      /**< Positive bits in mask are set in port. */
} pcal6408a_port_op_t;

/**
 * @brief Enumerator used for setting the drive strength of a pin.
 */
typedef enum
{
    PCAL6408A_PIN_25_DRIVE_STRENGTH, /**< Drive strength set to 25% of current drive capability. */
    PCAL6408A_PIN_50_DRIVE_STRENGTH, /**< Drive strength set to 50% of current drive capability. */
    PCAL6408A_PIN_75_DRIVE_STRENGTH, /**< Drive strength set to 75% of current drive capability. */
    PCAL6408A_PIN_100_DRIVE_STRENGTH /**< Drive strength set to 100% of current drive capability. */
} pcal6408a_pin_drive_strength_t;

/**
 * @brief Enumerator used for setting push-pull or open-drain I/O stage for a port.
 */
typedef enum
{
    PCAL6408A_PORT_PUSH_PULL,   /**< Push-pull I/O stage. */
    PCAL6408A_PORT_OPEN_DRAIN   /**< Open-drain I/O stage. */
} pcal6408a_port_io_stage_t;


/**
 * @brief Macro that defines expander module.
 *
 * @param[in] pcal6408a_inst_name Name of the instance to be created.
 * @param[in] instance_count      Number of connected expanders.
 */
#define PCAL6408A_INSTANCES_DEF_START(pcal6408a_inst_name, instance_count) \
    static pcal6408a_instance_t pcal6408a_inst_name[instance_count]

/**
 * @brief Macro that converts absolute pin number to pin number dependent on number of expander.
 *
 * @param[in] pin_num      Absolute pin number ranging from 0 to 7.
 * @param[in] instance_num Number of expander, order is the same as pcal6408a_add_instance calls.
 */
#define PIN_NUM_CONVERT(pin_num, instance_num) \
    (pin_num + instance_num * PCAL6408A_INNER_PIN_COUNT)


 /**
 * @brief Function initialising expander module.
 *
 * @param[in] p_instances Pointer to expander module.
 * @param[in] count       Number of connected expanders.
 */
void pcal6408a_init(pcal6408a_instance_t * p_instances, uint8_t count);

/**
 * @brief Function adding expander instance.
 *
 * @note Should be called for every connected expander.
 *       Order of calls define order of pins and ports.
 *
 * @param[in] p_twi_sensor   Pointer to common sensor instance. @ref NRF_TWI_SENSOR_DEF
 * @param[in] sensor_address Address of expander on I2C bus.
 *
 * @retval NRF_ERROR_MODULE_NOT_INITIALIZED If expander module wasn't initialised
 * @retval NRF_ERROR_STORAGE_FULL           If trying to add more instances than defined.
 * @retval other                            Error code from nrf_twi_sensor
 *                                          @ref nrf_twi_sensor_write
 */
ret_code_t pcal6408a_add_instance(nrf_twi_sensor_t * p_twi_sensor, uint8_t sensor_address);

/**
 * @brief Function for writing current configuration to expander.
 *
 * @param[in] instance_num Number of expander, order is the same as pcal6408a_add_instance calls.
 *
 * @retval NRF_ERROR_INVALID_PARAM If there is no expander with given number.
 * @retval other                   Error code from nrf_twi_sensor @ref nrf_twi_sensor_write
 */
ret_code_t pcal6408a_cfg_write(uint8_t instance_num);

/**
 * @brief Function for reading current configuration of expander.
 *
 * @param[in] instance_num Number of expander, order is the same as pcal6408a_add_instance calls.
 *
 * @retval NRF_ERROR_INVALID_PARAM If there is no expander with given number.
 * @retval other                   Error code from nrf_twi_sensor @ref nrf_twi_sensor_write
 */
ret_code_t pcal6408a_cfg_read(uint8_t instance_num);

/**
 * @brief Function for setting register configuration of a single pin.
 *
 * @param[in] reg_addr Register address.
 * @param[in] pin      Pin number.
 * @param[in] value    Value to set.
 *
 * @return Error code from nrf_twi_sensor @ref nrf_twi_sensor_write
 */
ret_code_t pcal6408a_pin_cfg_reg_set(pcal6408a_registers_t reg_addr, uint32_t pin, uint8_t value);

/**
 * @brief Function for getting register configuration of a single pin.
 *
 * @param[in]  reg_addr Register address.
 * @param[in]  pin      Pin number.
 *
 * @return Pin configuration value
 */
uint8_t pcal6408a_pin_cfg_reg_get(pcal6408a_registers_t reg_addr, uint32_t pin);

/**
 * @brief Function for setting register configuration of a port.
 *
 * @param[in] reg_addr Register address.
 * @param[in] port     Port number.
 * @param[in] mask     Mask for the operation.
 * @param[in] flag     Operation, whether mask should be written into register,
 *                     values should be cleared or set @ref pcal6408a_port_op_t
 *
 * @retval NRF_ERROR_INVALID_PARAM If there is no port with such number or invalid flag operation.
 * @retval other                   Error code from nrf_twi_sensor @ref nrf_twi_sensor_write
 */
ret_code_t pcal6408a_port_cfg_reg_set(pcal6408a_registers_t reg_addr,
                                      uint32_t              port,
                                      uint8_t               mask,
                                      pcal6408a_port_op_t   flag);

/**
 * @brief Function for getting register configuration of a port.
 *
 * @note When reading input register, it should be updated prior using this function,
 *       with @ref pcal6408a_pin_data_update
 *       When reading interrupt status register, it should be updated prior using this function,
 *       with @ref pcal6408a_int_status_update
 *
 * @param[in]  reg_addr Register address.
 * @param[in]  port     Port number.
 *
 * @return Register value
 */
uint8_t  pcal6408a_port_cfg_reg_get(pcal6408a_registers_t reg_addr, uint32_t port);

/**
 * @brief Function for updating pin data.
 *
 * @param user_cb Function to be called after pin data update is done.
 *
 * @return Return error code from nrf_twi_sensor @ref nrf_twi_sensor_reg_read
 */
ret_code_t pcal6408a_pin_data_update(nrf_twi_sensor_reg_cb_t user_cb);

/**
 * @brief Function for updating interrupt status data.
 *
 * @param user_cb Function to be called after interrupt status update is done.
 *
 * @return Return error code from nrf_twi_sensor @ref nrf_twi_sensor_reg_read
 */
ret_code_t pcal6408a_int_status_update(nrf_twi_sensor_reg_cb_t user_cb);

/**
 * @brief Function for setting polarity inversion of a given pin.
 *
 * @note Note that the pin must be configured as an input for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number.
 * @param[in] state
 * @arg       true       Enables polarity inversion.
 * @arg       false      Disables polarity inversion.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_polarity_inversion(uint32_t pin_number, bool state);

/**
 * @brief Function for setting interrupt of a given pin.
 *
 * @note Note that the pin must be configured as an input for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number.
 * @param[in] state
 * @arg       true       Disables interrupt.
 * @arg       false      Enables interrupt.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_interrupt(uint32_t pin_number, bool state);

/**
 * @brief Function for setting input latch of a given pin.
 *
 * @note Note that the pin must be configured as an input for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number.
 * @param[in] state
 * @arg       true       Enables input latch.
 * @arg       false      Disables input latch.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_latch(uint32_t pin_number, bool state);

/**
 * @brief Function for setting drive strength for a given pin.
 *
 * @note Note that the pin must be configured as an output for this function to have any effect.
 *
 * @param[in] pin_number            Specifies the pin number.
 * @param[in] drive_strength_config Drive strength of current drive capability (25%, 50%, 75%
 *                                  or 100%) @ref pcal6408a_pin_drive_strength_t
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
ret_code_t pcal6408a_pin_cfg_drive_strength(uint32_t                       pin_number,
                                            pcal6408a_pin_drive_strength_t drive_strength_config);

/**
 * @brief Function for setting polarity inversion of a given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an input.
 *
 * @param[in] port_number   Specifies the port number.
 * @param[in] polarity_mask Specifies the mask.
 * @param[in] flag          Operation, whether mask should be written into register,
 *                          values should be cleared or set @ref pcal6408a_port_op_t
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_cfg_polarity_inversion(uint32_t            port_number,
                                                                 uint8_t             polarity_mask,
                                                                 pcal6408a_port_op_t flag);

/**
 * @brief Function for setting interrupt of a given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an input.
 *
 * @param[in] port_number    Specifies the port number.
 * @param[in] interrupt_mask Specifies the mask.
 * @param[in] flag           Operation, whether mask should be written into register,
 *                           values should be cleared or set @ref pcal6408a_port_op_t
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_cfg_interrupt(uint32_t            port_number,
                                                        uint8_t             interrupt_mask,
                                                        pcal6408a_port_op_t flag);

/**
 * @brief Function for setting input latch of a given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an input.
 *
 * @param[in] port_number Specifies the port number.
 * @param[in] latch_mask  Specifies the mask.
 * @param[in] flag        Operation, whether mask should be written into register,
 *                        values should be cleared or set @ref pcal6408a_port_op_t
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_cfg_latch(uint32_t            port_number,
                                                    uint8_t             latch_mask,
                                                    pcal6408a_port_op_t flag);

/**
 * @brief Function for setting drive strength for a given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an output.
 *
 * @param[in] port_number         Specifies the port number.
 * @param[in] drive_strength_mask Specifies the mask. Note that for each pin there are dedicated
 *                                two adjacent bits.
 * @param[in] flag                Operation, whether mask should be written into register,
 *                                values should be cleared or set @ref pcal6408a_port_op_t
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
ret_code_t pcal6408a_port_cfg_drive_strength(uint32_t            port_number,
                                             uint16_t            drive_strength_mask,
                                             pcal6408a_port_op_t flag);

/**
 * @brief Function for selecting push-pull or open-drain I/O stage for the given port.
 *
 * @param[in] port_number     Specifies the port number.
 * @param[in] io_stage_config I/O stage of the port (push-pull or open-drain)
 *                            @ref pcal6408a_port_io_stage_t
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_cfg_io_stage(uint32_t                  port_number,
                                                       pcal6408a_port_io_stage_t io_stage_config);

 /**
 * @brief Function for configuring the given pin number as output.
 *
 * @param[in] pin_number Specifies the pin number.
 *
 * @return Error code from pin config set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_output(uint32_t pin_number);

/**
 * @brief Function for configuring the given pin number as input.
 *
 * @param[in] pin_number  Specifies the pin number.
 * @param[in] pull_config State of the pin pull resistor (no pull, pulled down, or pulled high)
 *                        @ref pcal6408a_pin_pull_t
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
ret_code_t pcal6408a_pin_cfg_input(uint32_t pin_number, pcal6408a_pin_pull_t pull_config);

/**
 * @brief Function for setting a pin.
 *
 * @note Note that the pin must be configured as an output for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number to set.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_set(uint32_t pin_number);

/**
 * @brief Function for clearing a pin.
 *
 * @note Note that the pin must be configured as an output for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number to clear.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_clear(uint32_t pin_number);

/**
 * @brief Function for configuring the pin range as outputs.
 *
 * @note For configuring only one pin as an output use @ref pcal6408a_pin_cfg_output.
 *
 * @param[in] pin_range_start Specifies the start number (inclusive) in the range of pin numbers
 *                            to be configured.
 * @param[in] pin_range_end   Specifies the end number (inclusive) in the range of pin numbers
 *                            to be configured.
 *
 * @retval NRF_ERROR_INVALID_LENGTH If start number is greater than end number.
 * @retval other                    Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
ret_code_t pcal6408a_range_cfg_output(uint32_t pin_range_start, uint32_t pin_range_end);

/**
 * @brief Function for configuring the pin range as inputs.
 *
 * @note For configuring only one pin as an input use @ref pcal6408a_pin_cfg_input.
 *
 * @param[in] pin_range_start Specifies the start number (inclusive) in the range of pin numbers
 *                            to be configured.
 * @param[in] pin_range_end   Specifies the end number (inclusive) in the range of pin numbers
 *                            to be configured.
 * @param[in] pull_config     State of the pin pull resistor (no pull, pulled down, or pulled high)
 *                            @ref pcal6408a_pin_pull_t
 *
 * @retval NRF_ERROR_INVALID_LENGTH If start number is greater than end number.
 * @retval other                    Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
ret_code_t pcal6408a_range_cfg_input(uint32_t             pin_range_start,
                                     uint32_t             pin_range_end,
                                     pcal6408a_pin_pull_t pull_config);

/**
 * @brief Function for setting the direction for a given pin.
 *
 * @param[in] pin_number Specifies the pin number.
 * @param[in] direction  Specifies the direction.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_dir_set(uint32_t            pin_number,
                                                 pcal6408a_pin_dir_t direction);

/**
 * @brief Function for toggling a given pin.
 *
 * @note Note that the pin must be configured as an output for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_toggle(uint32_t pin_number);

/**
 * @brief Function for writing a value to a given pin.
 *
 * @note Note that the pin must be configured as an output for this function to have any effect.
 *
 * @param[in] pin_number Specifies the pin number.
 * @param[in] value      Specifies the value to be written to the pin.
 * @arg       0          Clears the pin.
 * @arg       1          Sets the pin.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_pin_write(uint32_t pin_number, uint8_t value);

/**
 * @brief Function for reading the input level of a given pin.
 *
 * @note Input data should be updated prior using this function, with @ref pcal6408a_pin_data_update
 *
 * @param[in] pin_number Specifies the pin number.
 *
 * @return Error code from pin_cfg_reg_set @ref pcal6408a_pin_cfg_reg_set
 */
__STATIC_INLINE uint32_t pcal6408a_pin_read(uint32_t pin_number);

/**
 * @brief Function for setting the direction of a port.
 *
 * @param[in] port_number Specifies the port number.
 * @param[in] direction   Specifies the direction.
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_dir_set(uint32_t             port_number,
                                                  pcal6408a_port_dir_t direction);

/**
 * @brief Function for reading a given port.
 *
 * @note Input data should be updated prior using this function, with @ref pcal6408a_pin_data_update
 *
 * @param[in] port_number Specifies the port number.
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE uint32_t pcal6408a_port_read(uint32_t port_number);

/**
 * @brief Function for writing to a given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an output.
 *
 * @param[in] port_number Specifies the port number.
 * @param[in] value       Specifies the value to be written to the port.
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_write(uint32_t port_number, uint8_t value);

/**
 * @brief Function for setting individual pins on given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an output.
 *
 * @param[in] port_number Specifies the port number.
 * @param[in] set_mask    Mask specifying which pins to set. A bit set to 1 indicates that
 *                        the corresponding port pin shall be set.
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_set(uint32_t port_number, uint8_t set_mask);

/**
 * @brief Function for clearing individual pins on given port.
 *
 * @note Note that this function have an effect only for pins that are configured as an output.
 *
 * @param[in] port_number Specifies the port number.
 * @param[in] clr_mask    Mask specifying which pins to clear. A bit set to 1 indicates that
 *                        the corresponding port pin shall be cleared.
 *
 * @return Error code from port_cfg_reg_set @ref pcal6408a_port_cfg_reg_set
 */
__STATIC_INLINE ret_code_t pcal6408a_port_clear(uint32_t port_number, uint8_t clr_mask);

#ifndef SUPPRESS_INLINE_IMPLEMENTATION

__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_polarity_inversion(uint32_t pin_number, bool state)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_POLARITY_INVERSION, pin_number, state);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_interrupt(uint32_t pin_number, bool state)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_INTERRUPT_MASK, pin_number, state);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_cfg_latch(uint32_t pin_number, bool state)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_INPUT_LATCH, pin_number, state);
}

__STATIC_INLINE ret_code_t pcal6408a_port_cfg_polarity_inversion(uint32_t            port_number,
                                                                 uint8_t             polarity_mask,
                                                                 pcal6408a_port_op_t flag)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_POLARITY_INVERSION,
                                      port_number,
                                      polarity_mask,
                                      flag);
}

__STATIC_INLINE ret_code_t pcal6408a_port_cfg_interrupt(uint32_t            port_number,
                                                        uint8_t             interrupt_mask,
                                                        pcal6408a_port_op_t flag)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_INTERRUPT_MASK,
                                      port_number,
                                      interrupt_mask,
                                      flag);
}

__STATIC_INLINE ret_code_t pcal6408a_port_cfg_latch(uint32_t            port_number,
                                                    uint8_t             latch_mask,
                                                    pcal6408a_port_op_t flag)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_INPUT_LATCH,
                                      port_number,
                                      latch_mask,
                                      flag);
}

__STATIC_INLINE ret_code_t pcal6408a_port_cfg_io_stage(uint32_t                  port_number,
                                                       pcal6408a_port_io_stage_t io_stage_config)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT_CONFIGURATION,
                                     port_number * PCAL6408A_INNER_PIN_COUNT,
                                     io_stage_config);
}

 __STATIC_INLINE ret_code_t pcal6408a_pin_cfg_output(uint32_t pin_number)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                     pin_number,
                                     PCAL6408A_PIN_DIR_OUTPUT);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_set(uint32_t pin_number)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT, pin_number, PCAL6408A_PIN_SET);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_clear(uint32_t pin_number)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT, pin_number, PCAL6408A_PIN_CLR);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_dir_set(uint32_t pin_number, pcal6408a_pin_dir_t direction)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_CONFIGURATION, pin_number, direction);
}

__STATIC_INLINE ret_code_t pcal6408a_pin_toggle(uint32_t pin_number)
{
    return pcal6408a_pin_cfg_reg_set(
            PCAL6408A_REG_OUTPUT_PORT,
            pin_number,
            !pcal6408a_pin_cfg_reg_get(PCAL6408A_REG_OUTPUT_PORT, pin_number));
}

__STATIC_INLINE ret_code_t pcal6408a_pin_write(uint32_t pin_number, uint8_t value)
{
    return pcal6408a_pin_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT, pin_number, value);
}

__STATIC_INLINE uint32_t pcal6408a_pin_read(uint32_t pin_number)
{
    return pcal6408a_pin_cfg_reg_get(PCAL6408A_REG_INPUT_PORT, pin_number);
}

__STATIC_INLINE ret_code_t pcal6408a_port_dir_set(uint32_t             port_number,
                                                  pcal6408a_port_dir_t direction)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_CONFIGURATION,
                                      port_number,
                                      direction,
                                      PCAL6408A_PORT_WRITE);
}

__STATIC_INLINE uint32_t pcal6408a_port_read(uint32_t port_number)
{
    return pcal6408a_port_cfg_reg_get(PCAL6408A_REG_INPUT_PORT, port_number);
}

__STATIC_INLINE ret_code_t pcal6408a_port_write(uint32_t port_number, uint8_t value)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT,
                                      port_number,
                                      value,
                                      PCAL6408A_PORT_WRITE);
}

__STATIC_INLINE ret_code_t pcal6408a_port_set(uint32_t port_number, uint8_t set_mask)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT,
                                      port_number,
                                      set_mask,
                                      PCAL6408A_PORT_SET);
}

__STATIC_INLINE ret_code_t pcal6408a_port_clear(uint32_t port_number, uint8_t clr_mask)
{
    return pcal6408a_port_cfg_reg_set(PCAL6408A_REG_OUTPUT_PORT,
                                      port_number,
                                      clr_mask,
                                      PCAL6408A_PORT_CLEAR);
}

#endif //SUPPRESS_INLINE_IMPLEMENTATION

/** @} */
#ifdef __cplusplus
}
#endif

#endif // PCAL6408A_H__
