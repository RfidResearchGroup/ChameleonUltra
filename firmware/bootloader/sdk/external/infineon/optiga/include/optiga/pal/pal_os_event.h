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
* \file
*
* \brief   This file implements the prototype declarations of pal os event
*
* \addtogroup  grPAL
* @{
*/


#ifndef _PAL_OS_EVENT_H_
#define _PAL_OS_EVENT_H_

/**********************************************************************************************************************
 * HEADER FILES
 *********************************************************************************************************************/

#include "optiga/common/Datatypes.h"
#include "optiga/pal/pal.h"

/**********************************************************************************************************************
 * MACROS
 *********************************************************************************************************************/


/**********************************************************************************************************************
 * ENUMS
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * PAL extern definitions
 *********************************************************************************************************************/

/**
 * @brief typedef for Callback function when timer elapses.
 */
typedef void (*register_callback)(void*);

#ifdef PAL_OS_HAS_EVENT_INIT
/**
 * @brief Platform specific event init function.
 */
pal_status_t pal_os_event_init(void);
#endif

/**
 * @brief Callback registration function to trigger once when timer expires.
 */
void pal_os_event_register_callback_oneshot(register_callback callback, void* callback_args, uint32_t time_us);



#endif //_PAL_OS_EVENT_H_

/**
* @}
*/
