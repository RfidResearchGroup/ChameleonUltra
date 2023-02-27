/**
 * Copyright (c) 2021, Nordic Semiconductor ASA
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
 * @defgroup nrf_oberon_p256 ECC secp256r1 low-level APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations and APIs for low-level elliptic curve point operations
 * based on the NIST secp256r1 curve.
 */

#ifndef OCRYPTO_CURVE_P256_H
#define OCRYPTO_CURVE_P256_H

#include "ocrypto_sc_p256.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@cond */

// (x,y) only jacobian coordinates
typedef struct {
    ocrypto_mod_p256 x;
    ocrypto_mod_p256 y;
} ocrypto_cp_p256;

// P-256 invert context
typedef struct {
    ocrypto_mod_p256 x, x3, xn, t;
    int step;
} ocrypto_p256_invert_context;


typedef struct {
    ocrypto_cp_p256 p, q0, q1;
    uint32_t e[8]; // bits 0-255 of extended scalar, bit 256 = ~bit 255
    ocrypto_p256_invert_context inv;
    int ret, prev, dec, step;
} ocrypto_p256_mult_context;
/**@endcond */


/** Load r.x from bytes, keep r.y.
 *
 * @param[out]      r       Point with r.x loaded, r.y kept.
 * @param           p       x as as array of bytes.
 *
 * @retval 0  If @p r is a valid curve point.
 * @retval -1 Otherwise.
 */
int ocrypto_curve_p256_from32bytes(ocrypto_cp_p256 *r, const uint8_t p[32]);


/** Load point from bytes.
 *
 * @param[out]      r       Loaded point.
 * @param           p       Point as array of bytes.
 *
 * @retval 0  If @p r is a valid curve point.
 * @retval -1 Otherwise.
 */
int ocrypto_curve_p256_from64bytes(ocrypto_cp_p256 *r, const uint8_t p[64]);

/** Store p.x to bytes.
 *
 * @param[out]           r       x stored as array.
 * @param                p       Point with x to be stored.
 */
void ocrypto_curve_p256_to32bytes(uint8_t r[32], ocrypto_cp_p256 *p);

/** Store p.x to bytes.
 *
 * @param[out]           r       Point stored as array.
 * @param                p       Point to be stored.
 */
void ocrypto_curve_p256_to64bytes(uint8_t r[64], ocrypto_cp_p256 *p);

/** P256 scalar multiplication.
 *
 * r = p * s
 * r = [0,0] if p = [0,0] or s mod q = 0
 *
 * @param[out]      r       Output point.
 * @param           p       Input point.
 * @param           s       Scalar.
 *
 * @retval -1 If r = [0,0].
 * @retval 0  If 0 < s < q.
 * @retval 1  If s > q.
 */
int ocrypto_curve_p256_scalarmult(ocrypto_cp_p256 *r, const ocrypto_cp_p256 *p, const ocrypto_sc_p256 *s);

/** P256 scalar base multiplication.
 *
 * r = basePoint * s
 * r = [0,0] if s mod q = 0
 *
 * @param[out]      r       Output point.
 * @param           s       Scalar.
 *
 * @retval -1 If r = [0,0].
 * @retval 0  If 0 < s < q.
 * @retval 1  If s > q.
 */
int ocrypto_curve_p256_scalarmult_base(ocrypto_cp_p256 *r, const ocrypto_sc_p256 *s);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_CURVE_P256_H */

/** @} */
