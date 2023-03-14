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
 * @defgroup nrf_oberon_ecdh_p256 ECDH APIs
 * @ingroup nrf_oberon
 * @{
 * @brief APIs to do Elliptic Curve Diffie-Hellman using the NIST secp256r1 curve.
 */

#ifndef OCRYPTO_ECDH_P256_H
#define OCRYPTO_ECDH_P256_H

#include <stdint.h>
#include "ocrypto_curve_p256.h"


#ifdef __cplusplus
extern "C" {
#endif

/**@cond */
typedef struct {
    ocrypto_p256_mult_context mul;
    int ret;
} ocrypto_ecdh_p256_context;
/**@endcond */


/**
 * ECDH P-256 public key generation `r = n * p`.
 *
 * Given a secret key @p s the corresponding public key is computed and put
 * into @p r.
 *
 * @param[out] r Generated public key.
 * @param      s Secret key. Must be pre-filled with random data.
 *
 * @retval 0  If @p s is a valid secret key.
 * @retval -1 Otherwise.
 *
 * @remark @p r may be same as @p s.
 */
int ocrypto_ecdh_p256_public_key(uint8_t r[64], const uint8_t s[32]);

/**
 * ECDH P-256 common secret.
 *
 * The common secret is computed from both the client's public key @p p
 * and the server's secret key @p s and put into @p r.
 *
 * @param[out] r Generated common secret.
 * @param      s Server private key.
 * @param      p Client public key.
 *
 * @retval 0  If @p s is a valid secret key and @p p is a valid public key.
 * @retval -1 Otherwise.
 *
 * @remark @p r may be same as @p s or @p p.
 */
int ocrypto_ecdh_p256_common_secret(uint8_t r[32], const uint8_t s[32], const uint8_t p[64]);


/**@name Incremental ECDH P-256 calculation.
 *
 * This group of functions can be used to incrementally calculate
 * the ECDH P-256 public key and common secret.
 * Each call completes in less than 25ms on a 16MHz Cortex-M0.
 *
 * Use pattern:
 *
 * Public Key:
 * @code
 *   ocrypto_ecdh_p256_public_key_init(ctx, sKey);
 *   while (ocrypto_ecdh_p256_public_key_iterate(ctx));
 *   res = ocrypto_ecdh_p256_public_key_final(ctx, pKey);
 * @endcode
 * Common Secret:
 * @code
 *   ocrypto_ecdh_p256_common_secret_init(ctx, sKey, pKey);
 *   while (ocrypto_ecdh_p256_common_secret_iterate(ctx));
 *   res = ocrypto_ecdh_p256_common_secret_final(ctx, secet);
 * @endcode
 */
/**@{*/

/**
 * Incremental ECDH P-256 public key generation start.
 *
 * Key generation is started and the context @p ctx is initialized by this function.
 *
 * @param[out] ctx Context.
 * @param      s   Secret key. Must be pre-filled with random data.
 */
void ocrypto_ecdh_p256_public_key_init(ocrypto_ecdh_p256_context *ctx, const uint8_t s[32]);

/**
 * Incremental ECDH P-256 public key generation step.
 *
 * The key calculation is advanced and the context @p ctx is updated by this function.
 *
 * @param     ctx Context.
 *
 * @retval 1  If another call to @c ocrypto_ecdh_p256_public_key_init is needed.
 * @retval 0  If key generation should be completed by a call to @c ocrypto_ecdh_p256_public_key_final.
 */
int ocrypto_ecdh_p256_public_key_iterate(ocrypto_ecdh_p256_context *ctx);

/**
 * Incremental ECDH P-256 public key generation final step.
 *
 * Key generation is finalized and the context @p ctx is used to generate the key.
 *
 * @param      ctx Context.
 * @param[out] r   Generated public key.
 *
 * @retval 0  If @p s is a valid secret key.
 * @retval -1 Otherwise.
 */
int ocrypto_ecdh_p256_public_key_final(ocrypto_ecdh_p256_context *ctx, uint8_t r[64]);

/**
 * Incremental ECDH P-256 common secret generation start.
 *
 * Common secret calculation is started and the context @p ctx is initialized by this function.
 *
 * @param[out] ctx Context.
 * @param      s   Server private key.
 * @param      p   Client public key.
 */
void ocrypto_ecdh_p256_common_secret_init(ocrypto_ecdh_p256_context *ctx, const uint8_t s[32], const uint8_t p[64]);

/**
 * Incremental ECDH P-256 common secret generation step.
 *
 * Common secret calculation is advanced and the context @p ctx is updated by this function.
 *
 * @param     ctx Context.
 *
 * @retval 1  If another call to @c ocrypto_ecdh_p256_common_secret_iterate is needed.
 * @retval 0  If key generation should be completed by a call to @c ocrypto_ecdh_p256_common_secret_final.
 */
int ocrypto_ecdh_p256_common_secret_iterate(ocrypto_ecdh_p256_context *ctx);

/**
 * Incremental ECDH P-256 common secret generation final step.
 *
 * Common secret calculation is finalized and the context @p ctx is used to generate the secret.
 *
 * @param      ctx Context.
 * @param[out] r   Generated common secret.
 *
 * @retval 0  If @p s is a valid secret key and @p p is a valid public key.
 * @retval -1 Otherwise.
 */
int ocrypto_ecdh_p256_common_secret_final(ocrypto_ecdh_p256_context *ctx, uint8_t r[32]);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_ECDH_P256_H */

/** @} */
