/**
* \copyright
* MIT License
*
* Copyright (c) 2019 Infineon Technologies AG
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
* \endcopyright
*
* \author Infineon Technologies AG
*
* \file pal_pin_config.h
*
* \brief    This file defines the pins the OPTIGA is connected to.
*
* \addtogroup  grPAL
*
* @{
*/

#ifndef _OPTIGA_PIN_CONFIG_H_
#define _OPTIGA_PIN_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "boards.h"
#include <stdint.h>

// To select a pin configuration set its define to '1' and all others to '0'

// Trust X Shield
// OPTIGA as 2Go board on the TrustX shield, e.g. TrustX2Go and TrustM2Go
#define OPTIGA_PIN_CONFIG_2GO 0
// OPTIGA soldered on the TrustX shield
#define OPTIGA_PIN_CONFIG_TRUSTX_SHIELD 0

// MY IOT ADAPTER
// Product Link: https://www.infineon.com/cms/en/product/evaluation-boards/my-iot-adapter/
// OPTIGA as 2Go board on MY IOT ADAPTER Slot 1
#define OPTIGA_PIN_CONFIG_MYIOT_SLOT1 0
// OPTIGA as 2Go board on MY IOT ADAPTER Slot 2 or 3
#define OPTIGA_PIN_CONFIG_MYIOT_SLOT2_3 1

/*
 * The following defines pack additional information into the highest bits of
 * a void*. This is safe, because on nrf52 the pin description uses less than 8 bit
 * and a void* has 32 bits on this platform.
 */

/** @brief set a pin to this value to mark it as unused and it will not be initialised */
#define OPTIGA_PIN_UNUSED               UINT32_C(0xFFFFFFFF)

#define OPTIGA_PIN_INITIAL_VAL_MASK     (UINT32_C(1) << 31)
/** @brief defines the initial state of the pin */
#define OPTIGA_PIN_INITIAL_VAL_HIGH     OPTIGA_PIN_INITIAL_VAL_MASK
#define OPTIGA_PIN_INITIAL_VAL_LOW      0

#define OPTIGA_PIN_ONE_TIME_INIT_MASK   (UINT32_C(1) << 30)
/** @brief If this flag is set, the pin will be initialised with the specified value, but not used */
#define OPTIGA_PIN_ONE_TIME_INIT        OPTIGA_PIN_ONE_TIME_INIT_MASK

#define OPTIGA_PIN_ALL_MASKS            (OPTIGA_PIN_INITIAL_VAL_MASK | OPTIGA_PIN_ONE_TIME_INIT_MASK)

#if OPTIGA_PIN_CONFIG_2GO == 1
#define OPTIGA_PIN_VDD      (ARDUINO_9_PIN | OPTIGA_PIN_INITIAL_VAL_LOW | OPTIGA_PIN_ONE_TIME_INIT)
#define OPTIGA_PIN_RST      (ARDUINO_7_PIN | OPTIGA_PIN_INITIAL_VAL_LOW)
#elif  OPTIGA_PIN_CONFIG_TRUSTX_SHIELD == 1
#define OPTIGA_PIN_VDD      (ARDUINO_9_PIN | OPTIGA_PIN_INITIAL_VAL_HIGH)
#define OPTIGA_PIN_RST      (ARDUINO_7_PIN | OPTIGA_PIN_INITIAL_VAL_LOW)
#elif OPTIGA_PIN_CONFIG_MYIOT_SLOT1 == 1
#define OPTIGA_PIN_RST      (ARDUINO_10_PIN | OPTIGA_PIN_INITIAL_VAL_LOW)
#define OPTIGA_PIN_VDD      OPTIGA_PIN_UNUSED
#elif OPTIGA_PIN_CONFIG_MYIOT_SLOT2_3 == 1
#define OPTIGA_PIN_RST      (ARDUINO_5_PIN | OPTIGA_PIN_INITIAL_VAL_LOW)
#define OPTIGA_PIN_VDD      OPTIGA_PIN_UNUSED
#else
#error "No pin configuration selected"
#endif

/** @brief PIN for I2C SCL to Infineon OPTIGA Trust X device */
#define OPTIGA_PIN_I2C_SCL  (ARDUINO_SCL_PIN)
/** @brief PIN for I2C SDA to Infineon OPTIGA Trust X device */
#define OPTIGA_PIN_I2C_SDA  (ARDUINO_SDA_PIN)

#ifdef __cplusplus
}
#endif

#endif /*_OPTIGA_PIN_CONFIG_H_*/

/**
* @}
*/
