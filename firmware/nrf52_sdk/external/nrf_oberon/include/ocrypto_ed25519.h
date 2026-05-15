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
 * @defgroup nrf_oberon_ed25519 Ed25519 APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations and APIs for the Ed25519 algorithm.
 *
 * Ed25519 is a specific implementation of EdDSA, a digital signature scheme.
 * EdDSA is based on Twisted Edwards curves and is designed to be faster than
 * existing digital signature schemes without sacrificing security. It was
 * developed by Daniel J. Bernstein, et al. Ed25519 is intended to provide
 * attack resistance comparable to quality 128-bit symmetric ciphers.
 *
 * @see [Ed25519: high-speed high-security signatures](https://ed25519.cr.yp.to)
 */

#ifndef OCRYPTO_ED25519_H
#define OCRYPTO_ED25519_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Length of a public key.
 */
#define ocrypto_ed25519_PUBLIC_KEY_BYTES (32)

/**
 * Length of a secret key.
 */
#define ocrypto_ed25519_SECRET_KEY_BYTES (32)

/**
 * Length of a signature.
 */
#define ocrypto_ed25519_BYTES (64)


/**
 * Ed25519 signature key pair generation.
 *
 * Given a secret key @p sk, the corresponding public key is computed and put
 * into @p pk. The key pair can then be used to sign and verify message signatures.
 *
 * @param[out] pk Generated public key.
 * @param      sk Secret key. Must be pre-filled with random data.
 */
void ocrypto_ed25519_public_key(uint8_t pk[ocrypto_ed25519_PUBLIC_KEY_BYTES],
                                const uint8_t sk[ocrypto_ed25519_SECRET_KEY_BYTES]);

/**
 * Ed25519 signature generate.
 *
 * The message @p m is signed using the secret key @p sk and the corresponding
 * public key @p pk. The signature is put into @p sig.
 *
 * @param[out] sig   Generated signature.
 * @param      m     Input message.
 * @param      m_len Length of @p m.
 * @param      sk    Secret key.
 * @param      pk    Public key.
 */
void ocrypto_ed25519_sign(uint8_t sig[ocrypto_ed25519_BYTES],
                          const uint8_t *m, size_t m_len,
                          const uint8_t sk[ocrypto_ed25519_SECRET_KEY_BYTES],
                          const uint8_t pk[ocrypto_ed25519_PUBLIC_KEY_BYTES]);

/**
 * Ed25519 signature verification.
 *
 * The signature @p sig of the input message @p m is verified using the signer's
 * public key @p pk.
 *
 * @param sig   Input signature.
 * @param m     Input message.
 * @param m_len Length of @p m.
 * @param pk    Signer's public key.
 *
 * @retval 0  If the signature is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_ed25519_verify(const uint8_t sig[ocrypto_ed25519_BYTES],
                           const uint8_t *m, size_t m_len,
                           const uint8_t pk[ocrypto_ed25519_PUBLIC_KEY_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_ED25519_H */

/** @} */

