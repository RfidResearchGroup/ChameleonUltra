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
 * @defgroup nrf_oberon_ecjpake EC-JPAKE
 * @ingroup nrf_oberon
 * @{
 * @brief Type declaration and APIs for EC-JPAKE.
 *
 */
#ifndef OCRYPTO_ECJPAKE_P256_H
#define OCRYPTO_ECJPAKE_P256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EC-JPAKE-P256 public key and zero knowledge proof generation.
 *
 * @param[out] X       Public key.
 * @param[out] V       ZKP ephemeral public key.
 * @param[out] r       ZKP signature.
 * @param      G       Generator. May be NULL to use the default generator.
 * @param      x       Secret key. 0 < x < group order.
 * @param      v       ZKP ephemeral secret key. 0 < v < group order.
 * @param      id      Identity of originator.
 * @param      id_len  Identity length.
 *
 * @retval 0  If inputs are valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ecjpake_get_key(
    uint8_t X[64],
    uint8_t V[64],
    uint8_t r[32],
    const uint8_t G[64],
    const uint8_t x[32],
    const uint8_t v[32],
    const char *id, size_t id_len);

/**
 * EC-JPAKE-P256 zero knowledge proof verification.
 *
 * @param      G       Generator. May be NULL to use the default generator.
 * @param      X       Public key.
 * @param      V       ZKP ephemeral public key.
 * @param      r       ZKP signature.
 * @param      id      Identity of originator.
 * @param      id_len  Identity length.
 *
 * @retval 0  If proof is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ecjpake_verify_key(
    const uint8_t G[64],
    const uint8_t X[64],
    const uint8_t V[64],
    const uint8_t r[32],
    const char *id, size_t id_len);


/**
 * EC-JPAKE-P256 generator derivation.
 *
 * @param[out] G       Generator.
 * @param      X1      Public key 1.
 * @param      X2      Public key 2.
 * @param      X3      Public key 3.
 *
 * @retval 0  If the generator is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ecjpake_get_generator(
    uint8_t G[64],
    const uint8_t X1[64],
    const uint8_t X2[64],
    const uint8_t X3[64]);

/**
 * EC-JPAKE-P256 read shared secret.
 *
 * @param[out] rs          Reduced shared secret.
 * @param      secret      Shared secret.
 * @param      secret_len  Secret length.
 */
void ocrypto_ecjpake_read_shared_secret(
    uint8_t rs[32],
    const uint8_t *secret, size_t secret_len);

/**
 * EC-JPAKE-P256 shared secret handling.
 *
 * @param[out] xs          Client/server secret key.
 * @param      x2          Secret key 2.
 * @param      rs          Reduced shared secret.
 *
 * @retval 0  If the derived secret key is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ecjpake_process_shared_secret(
    uint8_t xs[32],
    const uint8_t x2[32],
    const uint8_t rs[32]);

/**
 * EC-JPAKE-P256 secret key generation.
 *
 * @param[out] secret  Resulting premaster secret.
 * @param      Xr      Remote client/server public key.
 * @param      X2      Remote public key 2.
 * @param      xs      Client/server secret key.
 * @param      x2      Secret key 2.
 *
 * @retval 0  If the key is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ecjpake_get_secret_key(
    uint8_t secret[32],
    const uint8_t Xr[64],
    const uint8_t X2[64],
    const uint8_t xs[32],
    const uint8_t x2[32]);


#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_ECJPAKE_P256_H */

/** @} */

