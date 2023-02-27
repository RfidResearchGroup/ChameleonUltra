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
 * @defgroup nrf_oberon_rsa_key RSA key APIs
 * @ingroup nrf_oberon_rsa
 * @{
 * @brief Type declarations for RSA APIs.
 *
 * RSA is a number theoretic public-key encryption and signature algorithm.
 *
 * These functions support the setup of 1024 and 2048 RSA secret and public keys.
 */

#ifndef OCRYPTO_RSA_KEY_H
#define OCRYPTO_RSA_KEY_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * The Public RSA Exponent.
 */
#define PUB_EXP 65537  // 2^16 + 1


/**@name 1024-bit RSA Keys
 *
 * This group of keys is used for 1024-bit RSA.
 */
/**@{*/
/**
 * 1024-bit RSA public key.
 */
typedef struct {
    /**@cond */
    uint32_t n[32];
    // e = 65537
    /**@endcond */
} ocrypto_rsa1024_pub_key;

/**
 * 1024 bit RSA secret key.
 */
typedef struct {
    /**@cond */
    uint32_t n[32];
    uint32_t d[32];  // x^(e*d) mod n == x
    /**@endcond */
} ocrypto_rsa1024_key;

/**
 * 1024-bit RSA secret key with CRT coefficients.
 */
typedef struct {
    /**@cond */
    uint32_t n[32];
    uint32_t p[16], q[16];   // primes, p*q = n
    uint32_t dp[16], dq[16]; // d mod (p-1), d mod (q-1)
    uint32_t qinv[16];       // 1/q mod p
    /**@endcond */
} ocrypto_rsa1024_crt_key;
/**@}*/


/**@name 2048-bit RSA Keys
 *
 * This group of keys is used for 2048-bit RSA.
 */
/**@{*/
/**
 * 2048-bit RSA public key.
 */
typedef struct {
    /**@cond */
    uint32_t n[64];
    // e = 65537
    /**@endcond */
} ocrypto_rsa2048_pub_key;

/**
 * 2048-bit RSA secret key.
 */
typedef struct {
    /**@cond */
    uint32_t n[64];
    uint32_t d[64];  // x^(e*d) mod n == x
    /**@endcond */
} ocrypto_rsa2048_key;

/**
 * 2048-bit RSA secret key with CRT coefficients.
 */
typedef struct {
    /**@cond */
    uint32_t n[64];
    uint32_t p[32], q[32];   // primes, p*q = n
    uint32_t dp[32], dq[32]; // d mod (p-1), d mod (q-1)
    uint32_t qinv[32];       // 1/q mod p
    /**@endcond */
} ocrypto_rsa2048_crt_key;
/**@}*/

/**@name 1024-bit RSA key setup
 *
 * This group of functions is used for 1024-bit RSA key setup.
 */
/**@{*/
/**
 * 1024-bit RSA public key setup.
 *
 * @param[out] k       The initialized public key.
 * @param      n       The RSA modulus. Must be exactly 1024 bits.
 * @param      nlen    Length of @p n.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 *
 * @remark The public exponent is fixed at 65537.
 */
int ocrypto_rsa1024_init_pub_key(
    ocrypto_rsa1024_pub_key *k,
    const uint8_t *n, size_t nlen);

/**
 * 1024-bit RSA secret key setup.
 *
 * @param[out] k       The initialized public key.
 * @param      n       The RSA modulus. Must be exactly 1024 bits.
 * @param      nlen    Length of @p n.
 * @param      d       The secret exponent. Must be <= 1024 bits.
 * @param      dlen    Length of @p d.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 */
int ocrypto_rsa1024_init_key(
    ocrypto_rsa1024_key *k,
    const uint8_t *n, size_t nlen,
    const uint8_t *d, size_t dlen);

/**
 * 1024-bit RSA secret key setup with CRT coefficients.
 *
 * @param[out] k          The initialized secret key.
 * @param      p          The 1. RSA prime. Must be exactly 512 bits.
 * @param      plen       Length of @p p.
 * @param      q          The 2. RSA prime. Must be exactly 512 bits.
 * @param      qlen       Length of @p q.
 * @param      dp         The 1. CRT exponent. dp = d mod (p-1).
 * @param      dplen      Length of @p dp.
 * @param      dq         The 2. CRT exponent. dq = d mod (q-1).
 * @param      dqlen      Length of @p dq.
 * @param      qinv       The CRT coefficient. qinv = 1/q mod p.
 * @param      qilen      Length of @p qinv.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 */
int ocrypto_rsa1024_init_crt_key(
    ocrypto_rsa1024_crt_key *k,
    const uint8_t *p,    size_t plen,
    const uint8_t *q,    size_t qlen,
    const uint8_t *dp,   size_t dplen,
    const uint8_t *dq,   size_t dqlen,
    const uint8_t *qinv, size_t qilen);
/**@}*/

/**@name 2048-bit RSA key setup
 *
 * This group of functions is used for 2048-bit RSA key setup.
 */
/**@{*/
/**
 * 2048-bit RSA public key setup.
 *
 * @param[out] k       The initialized public key.
 * @param      n       The RSA modulus. Must be exactly 2048 bits.
 * @param      nlen    Length of @p n.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 *
 * @remark The public exponent is fixed at 65537.
 */
int ocrypto_rsa2048_init_pub_key(
    ocrypto_rsa2048_pub_key *k,
    const uint8_t *n, size_t nlen);

/**
 * 2048-bit RSA secret key setup.
 *
 * @param[out] k       The initialized public key.
 * @param      n       The RSA modulus. Must be exactly 2048 bits.
 * @param      nlen    Length of @p n.
 * @param      d       The secret exponent. Must be <= 2048 bits.
 * @param      dlen    Length of @p d.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 */
int ocrypto_rsa2048_init_key(ocrypto_rsa2048_key *k,
    const uint8_t *n, size_t nlen,
    const uint8_t *d, size_t dlen);

/**
 * 2048-bit RSA secret key setup with CRT coefficients.
 *
 * @param[out] k          The initialized secret key.
 * @param      p          The 1. RSA prime. Must be exactly 1024 bits.
 * @param      plen       Length of @p p.
 * @param      q          The 2. RSA prime. Must be exactly 1024 bits.
 * @param      qlen       Length of @p q.
 * @param      dp         The 1. CRT exponent. dp = d mod (p-1).
 * @param      dplen      Length of @p dp.
 * @param      dq         The 2. CRT exponent. dq = d mod (q-1).
 * @param      dqlen      Length of @p dq.
 * @param      qinv       The CRT coefficient. qinv = 1/q mod p.
 * @param      qilen      Length of @p qinv.
 *
 * @retval -1 If the input length is invalid.
 * @retval 0  Otherwise.
 */
int ocrypto_rsa2048_init_crt_key(
    ocrypto_rsa2048_crt_key *k,
    const uint8_t *p,    size_t plen,
    const uint8_t *q,    size_t qlen,
    const uint8_t *dp,   size_t dplen,
    const uint8_t *dq,   size_t dqlen,
    const uint8_t *qinv, size_t qilen);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_RSA_KEY_H */

/** @} */

