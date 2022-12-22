/**
* MIT License
*
* Copyright (c) 2018 Infineon Technologies AG
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE
*
*
* \file
*
* \brief This file implements the prototype declarations of pal i2c
*
* \addtogroup  grPAL
* @{
*/

#ifndef _PAL_I2C_H_
#define _PAL_I2C_H_

/**********************************************************************************************************************
 * HEADER FILES
 *********************************************************************************************************************/

#include "optiga/pal/pal.h"

/**********************************************************************************************************************
 * MACROS
 *********************************************************************************************************************/
/// Event returned when I2C master completes execution
#define PAL_I2C_EVENT_SUCCESS (0x0000)
/// Event returned when I2C master operation fails
#define PAL_I2C_EVENT_ERROR   (0x0001)
/// Event returned when lower level I2C bus is busy
#define PAL_I2C_EVENT_BUSY    (0x0002)

/**********************************************************************************************************************
 * ENUMS
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * DATA STRUCTURES
 *********************************************************************************************************************/
/** @brief PAL I2C context structure */
typedef struct pal_i2c
{
    /// Pointer to I2C master platform specific context
    void* p_i2c_hw_config;
    /// I2C slave address
    uint8_t slave_address;
    /// Pointer to store the callers context information
    void* upper_layer_ctx;
    /// Pointer to store the callers handler
    void* upper_layer_event_handler;

} pal_i2c_t;

/**********************************************************************************************************************
 * API Prototypes
 *********************************************************************************************************************/
/**
 * @brief Initializes the I2C master.
 */
pal_status_t pal_i2c_init(const pal_i2c_t* p_i2c_context);

/**
 * @brief Sets the I2C Master bitrate
 */
pal_status_t pal_i2c_set_bitrate(const pal_i2c_t* p_i2c_context, uint16_t bitrate);

//Dileep:  "write on I2C bus" --> "write to I2C bus"
/**
 * @brief Writes on I2C bus.
 */
pal_status_t pal_i2c_write(pal_i2c_t* p_i2c_context, uint8_t* p_data , uint16_t length);

/**
 * @brief Reads from I2C bus.
 */
pal_status_t pal_i2c_read(pal_i2c_t* p_i2c_context, uint8_t* p_data , uint16_t length);

/**
 * @brief De-initializes the I2C master.
 */
pal_status_t pal_i2c_deinit(const pal_i2c_t* p_i2c_context);

#endif /* _PAL_I2C_H_ */

/**
* @}
*/

