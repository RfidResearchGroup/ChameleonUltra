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
 * @defgroup nrf_oberon_chacha_poly_inc ChaCha20-Poly1305 incremental APIs
 * @ingroup nrf_oberon_chacha_poly
 * @{
 * @brief Type declaration and APIs for authenticated encryption and additional data using
 *        the ChaCha20-Poly1305 algorithm in incremental steps.
 *
 * ChaCha20-Poly1305 is an authenticated encryption algorithm with optional
 * additional authenticated data developed by Daniel J.Bernstein.
 *
 * The ChaCha20 stream cipher is combined with the Poly1305 authenticator.
 *
 * @see [RFC 7539 - ChaCha20 and Poly1305 for IETF Protocols](http://tools.ietf.org/html/rfc7539)
 */

#ifndef OCRYPTO_CHACHA20_POLY1305_INC_H
#define OCRYPTO_CHACHA20_POLY1305_INC_H

#include <stdint.h>
#include <stddef.h>
#include "ocrypto_chacha20_poly1305.h"
#include "ocrypto_poly1305.h"


#ifdef __cplusplus
extern "C" {
#endif


/**@cond */
typedef struct {
    ocrypto_poly1305_ctx auth_ctx;
    uint8_t subkey[32];
    uint8_t buffer[16];
    uint32_t buffer_len;
    uint8_t cypher[64];
    uint32_t cypher_idx;
    uint32_t count;
    size_t msg_len;
    size_t aad_len;
} ocrypto_chacha20_poly1305_ctx;
/**@endcond */


/**@name Incremental ChaCha20-Poly1305 generator.
 *
 * This group of functions can be used to incrementally encode and decode using the ChaCha20-Poly1305 stream cypher.
 *
 * Use pattern:
 *
 * Encoding:
 * @code
 *   ocrypto_chacha20_poly1305_init(ctx, nonce, nonce_len, key);
 *   ocrypto_chacha20_poly1305_update_aad(ctx, aad, aad_len, nonce, nonce_len, key);
 *   ...
 *   ocrypto_chacha20_poly1305_update_aad(ctx, aad, aad_len, nonce, nonce_len, key);
 *   ocrypto_chacha20_poly1305_update_enc(ctx, ct, pt, pt_len, nonce, nonce_len, key);
 *   ...
 *   ocrypto_chacha20_poly1305_update_enc(ctx, ct, pt, pt_len, nonce, nonce_len, key);
 *   ocrypto_chacha20_poly1305_final_enc(ctx, tag);
 * @endcode
 * Decoding:
 * @code
 *   ocrypto_chacha20_poly1305_init(ctx, nonce, nonce_len, key);
 *   ocrypto_chacha20_poly1305_update_aad(ctx, aad, aad_len, nonce, nonce_len, key);
 *   ...
 *   ocrypto_chacha20_poly1305_update_aad(ctx, aad, aad_len, nonce, nonce_len, key);
 *   ocrypto_chacha20_poly1305_update_dec(ctx, pt, ct, ct_len, nonce, nonce_len, key);
 *   ...
 *   ocrypto_chacha20_poly1305_update_dec(ctx, pt, ct, ct_len, nonce, nonce_len, key);
 *   res = ocrypto_chacha20_poly1305_final_dec(ctx, tag);
 * @endcode
 */
/**@{*/

/**
 * ChaCha20-Poly1305 initialization.
 *
 * The generator state @p ctx is initialized by this function.
 *
 * @param[out] ctx   Generator state.
 * @param      n     Nonce.
 * @param      n_len Length of @p n. 0 <= @p n_len <= @c ocrypto_chacha20_poly1305_NONCE_BYTES_MAX.
 * @param      k     Encryption key.
 */
void ocrypto_chacha20_poly1305_init(
    ocrypto_chacha20_poly1305_ctx *ctx,
    const uint8_t *n, size_t n_len,
    const uint8_t k[ocrypto_chacha20_poly1305_KEY_BYTES]);

/**
 * SHA-ChaCha20-Poly1305 incremental aad input.
 *
 * The generator state @p ctx is updated to include a data chunk @p a.
 *
 * This function can be called repeatedly until the whole data is processed.
 *
 * @param      ctx   Generator state.
 * @param      a     Additional authenticated data.
 * @param      a_len Length of @p a.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_chacha20_poly1305_init is required before this function can be called.
 *
 * @remark @c ocrypto_chacha20_poly1305_update_aad must be called before any call to
 *         @c ocrypto_chacha20_poly1305_update_enc or @c ocrypto_chacha20_poly1305_update_dec.
 */
void ocrypto_chacha20_poly1305_update_aad(
    ocrypto_chacha20_poly1305_ctx *ctx,
    const uint8_t *a, size_t a_len);

/**
 * SHA-ChaCha20-Poly1305 incremental encoder input.
 *
 * The generator state @p ctx is updated to include a message chunk @p m.
 *
 * This function can be called repeatedly until the whole message is processed.
 *
 * @param      ctx   Generator state.
 * @param[out] c     Generated ciphertext. Same length as input message.
 * @param      m     Message chunk.
 * @param      m_len Length of @p m.
 * @param      n     Nonce.
 * @param      n_len Length of @p n. 0 <= @p n_len <= @c ocrypto_chacha20_poly1305_NONCE_BYTES_MAX.
 * @param      k     Encryption key.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_chacha20_poly1305_init is required before this function can be called.
 *
 * @remark @c ocrypto_chacha20_poly1305_update_enc must be called after any call to
 *         @c ocrypto_chacha20_poly1305_update_aad.
 *
 * @remark @p c and @p m can point to the same address.
 */
void ocrypto_chacha20_poly1305_update_enc(
    ocrypto_chacha20_poly1305_ctx *ctx,
    uint8_t *c,
    const uint8_t *m, size_t m_len,
    const uint8_t *n, size_t n_len,
    const uint8_t k[ocrypto_chacha20_poly1305_KEY_BYTES]);

/**
 * SHA-ChaCha20-Poly1305 incremental decoder input.
 *
 * The generator state @p ctx is updated to include a cyphertext chunk @p c.
 *
 * This function can be called repeatedly until the whole cyphertext is processed.
 *
 * @param      ctx   Generator state.
 * @param[out] m     Decoded message. Same length as received ciphertext.
 * @param      c     Cyphertext chunk.
 * @param      c_len Length of @p c.
 * @param      n     Nonce.
 * @param      n_len Length of @p n. 0 <= @p n_len <= @c ocrypto_chacha20_poly1305_NONCE_BYTES_MAX.
 * @param      k     Encryption key.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_chacha20_poly1305_init is required before this function can be called.
 *
 * @remark @c ocrypto_chacha20_poly1305_update_dec must be called after any call to
 *         @c ocrypto_chacha20_poly1305_update_aad.
 *
 * @remark @p m and @p c can point to the same address.
 */
void ocrypto_chacha20_poly1305_update_dec(
    ocrypto_chacha20_poly1305_ctx *ctx,
    uint8_t *m,
    const uint8_t *c, size_t c_len,
    const uint8_t *n, size_t n_len,
    const uint8_t k[ocrypto_chacha20_poly1305_KEY_BYTES]);

/**
 * SHA-ChaCha20-Poly1305 final encoder step.
 *
 * The generator state @p ctx is used to finalize the encryption and generate the tag.
 *
 * @param      ctx   Generator state.
 * @param[out] tag   Generated authentication tag.
 */
void ocrypto_chacha20_poly1305_final_enc(
    ocrypto_chacha20_poly1305_ctx *ctx,
    uint8_t tag[ocrypto_chacha20_poly1305_TAG_BYTES]);

/**
 * SHA-ChaCha20-Poly1305 final decoder step.
 *
 * The generator state @p ctx is used to finalize the decryption and check the tag.
 *
 * @param      ctx   Generator state.
 * @param      tag   Received authentication tag.
 *
 * @retval 0  If @p tag is valid.
 * @retval -1 Otherwise.
 */
int ocrypto_chacha20_poly1305_final_dec(
    ocrypto_chacha20_poly1305_ctx *ctx,
    const uint8_t tag[ocrypto_chacha20_poly1305_TAG_BYTES]);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_CHACHA20_POLY1305_INC_H */

/** @} */

