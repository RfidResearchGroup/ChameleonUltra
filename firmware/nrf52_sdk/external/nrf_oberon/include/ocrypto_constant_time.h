/**
 * Copyright (c) 2019 - 2021, Nordic Semiconductor ASA
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

/**@file
 * @defgroup nrf_oberon Oberon cryptographic library
 * @{
 * @brief Highly optimized cryptographic algorithm implementation for Cortex-M0, Cortex-M4,
 * and Cortex-M33. Created by Oberon, under distribution license with Nordic Semiconductor ASA.
 * @}
 *
 * @defgroup nrf_oberon_constant_time Constant time APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Timing-invariant functions to use with cryptography.
 *
 * Collection of timing-invariant implementations of basic functions.
 */

#ifndef OCRYPTO_CONSTANT_TIME_H
#define OCRYPTO_CONSTANT_TIME_H

#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Variable length comparison.
 *
 * @param x      Memory region to compare with @p y.
 * @param y      Memory region to compare with @p x.
 * @param length Number of bytes to compare, @p length > 0.
 *
 * @retval 1 If @p x and @p y point to equal memory regions.
 * @retval 0 Otherwise.
 */
int ocrypto_constant_time_equal(const void *x, const void *y, size_t length);

/**
 * Variable length compare to zero.
 *
 * @param x      Pointer to memory region that will be compared.
 * @param length Number of bytes to compare, @p length > 0.
 *
 * @retval 1 If @p x is equal to a zero memory region.
 * @retval 0 Otherwise.
 */
int ocrypto_constant_time_is_zero(const void *x, size_t length);

/**
 * Variable length copy.
 *
 * @param x      Pointer to memory region to copy @p y to.
 * @param y      Pointer to memory region to copy to @p x.
 * @param length Number of bytes to copy, @p length > 0.
 */
void ocrypto_constant_time_copy(void *x, const void *y, size_t length);

/**
 * Variable length fill with zero.
 *
 * @param x      Pointer to memory region to be filled with zero.
 * @param length Number of bytes to fill, @p length > 0.
 */
void ocrypto_constant_time_fill_zero(void *x, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_CONSTANT_TIME_H */

/** @} */

