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
 * @defgroup nrf_oberon_ecdh_p224 ECDH P224 APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations and APIs for low-level elliptic curve point operations
 *        to do Elliptic Curve Diffie-Hellman based on the NIST secp224r1 curve.
 */

#ifndef OCRYPTO_ECDH_P224_H
#define OCRYPTO_ECDH_P224_H

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * ECDH P-224 public key generation `r = n * p`.
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
int ocrypto_ecdh_p224_public_key(uint8_t r[56], const uint8_t s[28]);

/**
 * ECDH P-224 common secret.
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
int ocrypto_ecdh_p224_common_secret(uint8_t r[28], const uint8_t s[28], const uint8_t p[56]);


#ifdef __cplusplus
}
#endif

#endif

/** @} */
