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
 * @defgroup nrf_oberon_curve25519 ECC Curve25519 low-level APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations and APIs for low-level elliptic curve point operations
 * based on Curve25519.
 *
 * Curve25519 is an elliptic curve offering 128 bits of security. It is designed
 * for use in the Elliptic Curve Diffie-Hellman (ECDH) key agreement scheme.
 *
 * @see [RFC 7748 - Elliptic Curves for Security](https://tools.ietf.org/html/rfc7748)
 * @see [Curve25519: high-speed elliptic-curve cryptography](http://cr.yp.to/ecdh.html)
 */

#ifndef OCRYPTO_CURVE25519_H
#define OCRYPTO_CURVE25519_H

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Length of a scalar.
 */
#define ocrypto_curve25519_SCALAR_BYTES (32)

/**
 * Length of a curve point.
 */
#define ocrypto_curve25519_BYTES (32)


/**
 * Curve25519 scalar multiplication `r = n * basePoint`.
 *
 * Given a secret key @p n, the corresponding Curve25519 public key is computed
 * and put into @p r.
 *
 * The inverse of this function is difficult to compute.
 *
 * @param[out] r Resulting curve point.
 * @param[in]  n Scalar factor.
 *
 * @remark @p r and @p n can point to the same address.
 */
void ocrypto_curve25519_scalarmult_base(
    uint8_t r[ocrypto_curve25519_BYTES],
    const uint8_t n[ocrypto_curve25519_SCALAR_BYTES]);

/**
 * Curve25519 scalar multiplication `r = n * p`.
 *
 * A shared secret is computed from the local secret key @p n and another
 * party's public key @p p and put into @p r. The same shared secret is
 * generated when the other party combines its private key with the local public
 * key.
 *
 * @param[out] r Resulting curve point.
 * @param[in]  n Scalar factor.
 * @param[in]  p Point factor.
 *
 * @remark @p r and @p n can point to the same address.
 */
void ocrypto_curve25519_scalarmult(
    uint8_t r[ocrypto_curve25519_BYTES],
    const uint8_t n[ocrypto_curve25519_SCALAR_BYTES],
    const uint8_t p[ocrypto_curve25519_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_CURVE25519_H */

/** @} */

