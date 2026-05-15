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
 * @defgroup nrf_oberon_poly1305 Poly1305 APIs
 * @ingroup nrf_oberon_chacha_poly
   @{
 * @brief Type declaration and APIs for the Poly1035 algorithm.
 *
 * Poly1305 is a message authentication code created by Daniel J.
 * Bernstein. It can be used to verify the data integrity and the
 * authenticity of a message.
 *
 * Poly1305 takes a one-time key to produce an authentication tag for a message.
 * Since a key can only be used to authenticate a single message, a new key
 * needs to be derived for each message.
 *
 * @see [RFC 7539 - ChaCha20 and Poly1305 for IETF Protocols](http://tools.ietf.org/html/rfc7539)
 * @see [Poly1305-AES: a state-of-the-art message-authentication code](http://cr.yp.to/mac.html)
 */

#ifndef OCRYPTO_POLY1305_H
#define OCRYPTO_POLY1305_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Key length.
 */
#define ocrypto_poly1305_KEY_BYTES (32)

/**
 * Authenticator length.
 */
#define ocrypto_poly1305_BYTES (16)


/**@cond */
typedef struct {
    uint32_t h[5];
} ocrypto_poly1305_ctx;
/**@endcond */


/**@name Incremental Poly1305 generator.
 *
 * This group of functions can be used to incrementally compute the Poly1305
 * authenticator for a given message and key.
 */
/**@{*/
/**
 * Poly1305 generator initialize.
 *
 * The generator state @p ctx is initialized by this function.
 *
 * @param[out] ctx Generator state.
 */
void ocrypto_poly1305_init(ocrypto_poly1305_ctx *ctx);

/**
 * Poly1305 generator.
 *
 * The generator state @p ctx is updated to authenticate a message chunk @p in
 * with a key @p k.
 *
 * This function can be called repeatedly until the whole message has been
 * processed.
 *
 * @param      ctx    Generator state.
 * @param      in     Input data.
 * @param      in_len Length of @p in.
 * @param      k      Encryption key.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_poly1305_init is required before this function can be called.
 *
 * @remark The same key @p k needs to be supplied for all message chunks.
 */
void ocrypto_poly1305_update(
    ocrypto_poly1305_ctx *ctx,
    const uint8_t *in, size_t in_len,
    const uint8_t k[ocrypto_poly1305_KEY_BYTES]);

/**
 * Poly1305 generator output.
 *
 * The generator state @p ctx is updated to finalize the authenticator for the
 * previously processed message chunks with key @p k. The authentication tag is
 * put into @p r.
 *
 * @param      ctx Generator state.
 * @param[out] r   Generated authentication tag.
 * @param      k   Encryption key.
 *
 * @remark Initialization of the generator state @p ctx through
 *         @c ocrypto_poly1305_init is required before this function can be called.
 *
 * @remark The same key @p k needs to be supplied that was used in previous
 *         @c ocrypto_poly1305_update invocations.
 *
 * @remark After return, the generator state @p ctx must no longer be used with
 *         @c ocrypto_poly1305_update and @c ocrypto_poly1305_final unless it is
 *         reinitialized using @c ocrypto_poly1305_init.
 */
void ocrypto_poly1305_final(
    ocrypto_poly1305_ctx *ctx,
    uint8_t r[ocrypto_poly1305_BYTES],
    const uint8_t k[ocrypto_poly1305_KEY_BYTES]);
/**@}*/

/**
 * Poly1305 message authentication tag.
 *
 * The Poly1305 authentication of a given input message @p in is computed and
 * put into @p r.
 *
 * @param[out] r      Generated authentication tag.
 * @param      in     Input data.
 * @param      in_len Length of @p in.
 * @param      k      Encryption key.
 */
void ocrypto_poly1305(
    uint8_t r[ocrypto_poly1305_BYTES],
    const uint8_t *in, size_t in_len,
    const uint8_t k[ocrypto_poly1305_KEY_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_POLY1305_H */

/** @} */

