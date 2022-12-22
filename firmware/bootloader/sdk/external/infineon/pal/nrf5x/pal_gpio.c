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
* \brief This file implements the platform abstraction layer APIs for gpio.
*
* \addtogroup  grPAL
* @{
*/


/**********************************************************************************************************************
 * HEADER FILES
 *********************************************************************************************************************/
#include "optiga/pal/pal_gpio.h"
#include "optiga/pal/pal_ifx_i2c_config.h"
#include "nrf_gpio.h"
#include "pal_pin_config.h"

/**********************************************************************************************************************
 * MACROS
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * LOCAL DATA
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * LOCAL ROUTINES
 *********************************************************************************************************************/

void setup_nrf_gpio(uint32_t pin)
{
    // don't touch pin config for unused pins
    if (pin == OPTIGA_PIN_UNUSED) {
        return;
    }

    // remove our flags to allow nrf_gpio_* functions to work
    const uint32_t pin_nr = pin & ~OPTIGA_PIN_ALL_MASKS;

    // Init pin direction
    nrf_gpio_cfg_output(pin_nr);

    // Set pin to initial state
    nrf_gpio_pin_write(pin_nr, pin & OPTIGA_PIN_INITIAL_VAL_MASK);
}

void write_nrf_gpio(uint32_t pin, bool value)
{
    // Skip pins marked for one time init or unused
    if ((pin == OPTIGA_PIN_UNUSED) || (pin & OPTIGA_PIN_ONE_TIME_INIT_MASK)) {
        return;
    }

    // remove our flags to allow nrf_gpio_* functions to work
    const uint32_t pin_nr = pin & ~OPTIGA_PIN_ALL_MASKS;
    nrf_gpio_pin_write(pin_nr, value);
}

/**********************************************************************************************************************
 * API IMPLEMENTATION
 *********************************************************************************************************************/

pal_status_t pal_gpio_init(const pal_gpio_t * p_gpio_context)
{
    const uint32_t vdd_pin = (uint32_t)(optiga_vdd_0.p_gpio_hw);
    const uint32_t rst_pin = (uint32_t)(optiga_reset_0.p_gpio_hw);

    setup_nrf_gpio(vdd_pin);
    setup_nrf_gpio(rst_pin);

    return PAL_STATUS_SUCCESS;
}

/**
* Sets the gpio pin to high state
*
* <b>API Details:</b>
*      The API sets the pin high, only if the pin is assigned to a valid gpio context.<br>
*      Otherwise the API returns without any failure status.<br>
*
*\param[in] p_gpio_context Pointer to pal layer gpio context
*
*
*/
void pal_gpio_set_high(const pal_gpio_t* p_gpio_context)
{
    if (p_gpio_context != NULL)
    {
         write_nrf_gpio((uint32_t)(p_gpio_context->p_gpio_hw), true);
    }
}

/**
* Sets the gpio pin to low state
*
* <b>API Details:</b>
*      The API set the pin low, only if the pin is assigned to a valid gpio context.<br>
*      Otherwise the API returns without any failure status.<br>
*
*\param[in] p_gpio_context Pointer to pal layer gpio context
*
*/
void pal_gpio_set_low(const pal_gpio_t* p_gpio_context)
{
    if (p_gpio_context != NULL)
    {
        write_nrf_gpio((uint32_t)(p_gpio_context->p_gpio_hw), false);
    }
}

/**
* @}
*/

