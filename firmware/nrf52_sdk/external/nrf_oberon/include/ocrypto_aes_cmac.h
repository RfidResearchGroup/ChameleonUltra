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
 * @defgroup nrf_oberon_aes_cmac AES CMAC APIs
 * @ingroup nrf_oberon_aes
 * @{
 * @brief Type definitions and APIS for AES CMAC (AES Cipher-based Message Authentication Code)
 *
 * AES (advanced encryption standard) is a symmetric encryption algorithm standardized by NIST.
 * AES transfers a 128-bit block of data into an encrypted block of the same size.
 *
 * AES-CMAC (AES Cipher-based Message Authentication Code) is a block cipher-based message
 * authentication code algorithm. The AES block cipher primitive is used in variant of the
 * CBC mode to get the authentication tag.
 */

#ifndef OCRYPTO_AES_CMAC_H
#define OCRYPTO_AES_CMAC_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Length of the pseudo random function.
 */
#define ocrypto_aes_cmac_prf128_BYTES (16)

/**
 * AES-CMAC authentication algorithm.
 *
 * @param[out] tag     Resulting tag.
 * @param      tag_len Tag length, 0 < @p tag_len <= 16.
 * @param      msg     Message to authenticate.
 * @param      msg_len Message length.
 * @param      key     AES key.
 * @param      size    Key size (16, 24, or 32).
 */
void ocrypto_aes_cmac_authenticate (
    uint8_t *tag, size_t tag_len,
    const uint8_t *msg, size_t msg_len,
    const uint8_t *key, size_t size);

/**
 * AES-CMAC-PRF-128 pseudo random function algorithm.
 *
 * @param[out] prf     16 byte PRF output.
 * @param      msg     Message input.
 * @param      msg_len Message length.
 * @param      key     Key.
 * @param      key_len Key length.
 */
void ocrypto_aes_cmac_prf128 (
    uint8_t prf[ocrypto_aes_cmac_prf128_BYTES],
    const uint8_t *msg, size_t msg_len,
    const uint8_t *key, size_t key_len);


#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_AES_CMAC_H */

/** @} */

