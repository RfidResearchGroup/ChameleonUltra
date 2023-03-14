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
 * @defgroup nrf_oberon_srpt SRPT - Secure Real-Time Transport Protocol APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations and APIs for SRTP - Secure Real-time Transport Protocol.
 *
 * SRTP is an extension of the RTP protocol with an enhanced security mechanism.
 */

#ifndef OCRYPTO_SRTP_H
#define OCRYPTO_SRTP_H

#include <stddef.h>
#include <stdint.h>

#include "ocrypto_aes_key.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * SRTP Authentication Key Size.
 */
#define ocrypto_srtp_AuthKeySize  (20)

/**
 * SRTP Salt Size.
 */
#define ocrypto_srtp_SaltSize     (14)

/**
 * SRTP Maximum Key Size.
 */
#define ocrypto_srtp_MaxKeySize   (ocrypto_aes256_KEY_BYTES)

/**
 * SRTP Context.
 */
typedef struct {
    /**
     * Key size [bytes].
     */
    uint32_t keySize;

    /**
     * Tag size [bytes].
     */
    uint32_t tagSize;

    /**
     * Session encryption key (max 256 bits).
     */
    uint8_t encrKey[ocrypto_srtp_MaxKeySize];

    /**
     * Session authentication key
     * 160 bits.
     */
    uint8_t authKey[ocrypto_srtp_AuthKeySize];

    /**
     * Session salt
     * 112 bits.
     */
    uint8_t saltKey[ocrypto_srtp_SaltSize];
} ocrypto_srtp_context;

/**
 * Setup SRTP contexts.
 *
 * @param[out] srtpContext          SRTP context to be setup.
 * @param[out] srtcpContext         SRTCP context to be setup.
 * @param      key                  Master key.
 * @param      keySize              Size of the master key (16, 24, or 32 bytes)
 * @param      salt                 Master salt.
 * @param      tagSize              Size of the authentication tag.
 * @param      ssrc                 Synchronization source.
 */
void ocrypto_srtp_setupContext(
    ocrypto_srtp_context *srtpContext,
    ocrypto_srtp_context *srtcpContext,
    const uint8_t *key,
    uint32_t keySize,
    const uint8_t *salt,
    uint32_t tagSize,
    uint32_t ssrc);

/**
 * Encrypt SRTP packet.
 *
 * The final packet consists of @p numHeaderBytes encrypted in place, followed
 * by @p numDataBytes copied from @p dataBytes during encryption.
 *
 * @param         srtpContext          SRTP context.
 * @param[in,out] packet               Encrypted packet.
 * @param         dataBytes            Data bytes to be encrypted.
 * @param         numHeaderBytes       Number of header bytes.
 * @param         numDataBytes         Number of data bytes.
 * @param         index                Packet index.
 */
void ocrypto_srtp_encrypt(
    const ocrypto_srtp_context *srtpContext,
    uint8_t *packet,
    const uint8_t *dataBytes,
    size_t numHeaderBytes,
    size_t numDataBytes,
    uint32_t index);

/**
 * Decrypt SRTP packet.
 *
 * @param      srtpContext          SRTP context.
 * @param[out] data                 Decrypted data.
 * @param      packetBytes          Packet bytes.
 * @param      numPacketBytes       Number of packet bytes.
 * @param      index                Packet index.
 */
void ocrypto_srtp_decrypt(
    const ocrypto_srtp_context *srtpContext,
    uint8_t *data,
    const uint8_t *packetBytes,
    size_t numPacketBytes,
    uint32_t index);

/**
 * Generate SRTP authentication tag from bytes and index.
 *
 * @param      context              SRTP context.
 * @param[out] tag                  Authentication tag generated.
 * @param      bytes                Byte buffer.
 * @param      numBytes             Number of bytes in buffer.
 * @param      index                Index.
 */
void ocrypto_srtp_authenticate(
    const ocrypto_srtp_context *context,
    uint8_t *tag,
    const uint8_t *bytes,
    size_t numBytes,
    uint32_t index);

/**
 * Check SRTP authentication tag against bytes and index.
 *
 * @param      context              SRTP context.
 * @param      tag                  Tag.
 * @param      bytes                Byte buffer.
 * @param      numBytes             Number of bytes in buffer.
 * @param      index                Index.
 *
 * @retval 1 If the tag is valid.
 * @retval 0 Otherwise.
 */
int ocrypto_srtp_verifyAuthentication(
    const ocrypto_srtp_context *context,
    const uint8_t *tag,
    const uint8_t *bytes,
    size_t numBytes,
    uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef OCRYPTO_SRTP_H */

/** @} */

