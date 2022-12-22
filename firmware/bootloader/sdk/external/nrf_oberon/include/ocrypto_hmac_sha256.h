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
 * @defgroup nrf_oberon_hmac HMAC - Hash-based Aessage Authentication Code
 * @ingroup nrf_oberon
 * @{
 * @brief HMAC is a hash-based Message Authentication Code utilizing a secure hash function.
 * @}
 * @defgroup nrf_oberon_hmac_256 HMAC APIs using SHA-256
 * @ingroup nrf_oberon_hmac
 * @{
 * @brief Type declarations and APIs for the HMAC-SHA256 algorithm.
 *
 * HMAC-SHA256 is an algorithm for message authentication using the
 * cryptographic hash function SHA256 and a reusable secret key. Users in
 * possession of the key can verify the integrity and authenticity of the
 * message.
 *
 * @see [RFC 2104 - HMAC: Keyed-Hashing for Message Authentication](http://tools.ietf.org/html/rfc2104)
 */

#ifndef OCRYPTO_HMAC_SHA256_H
#define OCRYPTO_HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include "ocrypto_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Maximum key length.
 */
#define ocrypto_hmac_sha256_KEY_BYTES_MAX (64)

/**
 * Length of the authenticator.
 */
#define ocrypto_hmac_sha256_BYTES (32)

/**@cond */
typedef struct
{
    ocrypto_sha256_ctx hash_ctx;
    uint8_t        ikey[ocrypto_hmac_sha256_KEY_BYTES_MAX];
    uint8_t        okey[ocrypto_hmac_sha256_KEY_BYTES_MAX];
    uint8_t        key[ocrypto_hmac_sha256_KEY_BYTES_MAX];
} ocrypto_hmac_sha256_ctx;
/**@endcond */


/**@name Incremental HMAC-SHA256 generator.
 *
 * This group of functions can be used to incrementally compute HMAC-SHA256
 * for a given message.
 */
/**@{*/

/**
 * HMAC-SHA256 initialization.
 *
 * The generator state @p ctx is initialized by this function.
 *
 * @param[out] ctx     Generator state.
 * @param      key     HMAC key.
 * @param      key_len Length of @p key.
 */
void ocrypto_hmac_sha256_init(ocrypto_hmac_sha256_ctx * ctx,
                              const uint8_t* key, size_t key_len);

/**
 * HMAC-SHA256 incremental data input.
 *
 * The generator state @p ctx is updated to hash a message chunk @p in.
 *
 * This function can be called repeatedly until the whole message is processed.
 *
 * @param[in,out] ctx    Generator state.
 * @param         in     Input data.
 * @param         in_len Length of @p in.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_hmac_sha256_init is required before this function can be called.
 */
void ocrypto_hmac_sha256_update(ocrypto_hmac_sha256_ctx * ctx,
                                const uint8_t* in, size_t in_len);

/**
 * HMAC-SHA256 output.
 *
 * The generator state @p ctx is updated to finalize the HMAC calculation.
 * The HMAC digest is put into @p r.
 *
 * @param[in,out] ctx Generator state.
 * @param[out]    r   Generated HMAC digest.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_hmac_sha256_init is required before this function can be called.
 *
 * @remark After return, the generator state @p ctx must no longer be used with
 *         @c ocrypto_hmac_sha256_update and @c ocrypto_hmac_sha256_final unless it is
 *         reinitialized using @c ocrypto_hmac_sha256_init.
 */
void ocrypto_hmac_sha256_final(ocrypto_hmac_sha256_ctx * ctx,
                               uint8_t r[ocrypto_hmac_sha256_BYTES]);
/**@}*/


/**
 * HMAC-SHA256 algorithm.
 *
 * The input message @p in is authenticated using the key @p k. The computed
 * authenticator is put into @p r. To verify the authenticator, the recipient
 * needs to recompute the HMAC authenticator and can then compare it with the
 * received authenticator.
 *
 * @param[out] r       HMAC output.
 * @param      key     HMAC key.
 * @param      key_len Length of @p key. 0 <= @p key_len <= @c ocrypto_hmac_sha256_KEY_BYTES_MAX.
 * @param      in      Input data.
 * @param      in_len  Length of @p in.
 */
void ocrypto_hmac_sha256(
    uint8_t r[ocrypto_hmac_sha256_BYTES],
    const uint8_t* key, size_t key_len,
    const uint8_t* in, size_t in_len);

/**
 * HMAC-SHA256 algorithm with AAD.
 *
 * @param[out] r       HMAC output
 * @param      key     HMAC key.
 * @param      key_len Length of @p key. 0 <= @p key_len <= @c ocrypto_hmac_sha256_KEY_BYTES_MAX.
 * @param      in      Input data.
 * @param      in_len  Length of @p in.
 * @param      aad     Additional authentication data. May be NULL.
 * @param      aad_len Length of @p aad.
 */
void ocrypto_hmac_sha256_aad(
    uint8_t r[ocrypto_hmac_sha256_BYTES],
    const uint8_t* key, size_t key_len,
    const uint8_t* in, size_t in_len,
    const uint8_t* aad, size_t aad_len);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_HMAC_SHA256_H */

/** @} */

