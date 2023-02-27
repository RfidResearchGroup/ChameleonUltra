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
 * @defgroup nrf_oberon_mbed_tls_ecjpake Oberon Mbed ECJPAKE APIs
 * @ingroup nrf_oberon
 * @{
 * @brief Type declarations for an alternate implementation of EC-JPAKE for mbed TLS.
 */

#ifndef ECJPAKE_ALT_H
#define ECJPAKE_ALT_H

#if defined(MBEDTLS_CONFIG_FILE)
#include MBEDTLS_CONFIG_FILE
#else
#include "mbedtls/config.h"
#endif

#include "mbedtls/ecp.h"
#include "mbedtls/md.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBERON_ECJPAKE_P256_SECRET_KEY_SIZE (32) //!< ECJPAKE P-256 secret key size in bytes.
#define OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE (64) //!< ECJPAKE P-256 public key size in bytes.

/* @brief Oberon replacement ECJPAKE context */
typedef struct mbedtls_ecjpake_context {
    mbedtls_ecjpake_role role;                                  //!< Role, either client or server.
    int point_format;                                           //!< Point format
    unsigned char secret[OBERON_ECJPAKE_P256_SECRET_KEY_SIZE];  //!< Secret.
    unsigned char s_key2[OBERON_ECJPAKE_P256_SECRET_KEY_SIZE];  //!< Secret key 2.
    unsigned char p_key1[OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE];  //!< Public key 1.
    unsigned char p_key2[OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE];  //!< Public key 2.
    unsigned char r_key1[OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE];  //!< Remote key 1.
    unsigned char r_key2[OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE];  //!< Remote key 2.
    unsigned char r_key3[OBERON_ECJPAKE_P256_PUBLIC_KEY_SIZE];  //!< Remote key 3.
} mbedtls_ecjpake_context;

#ifdef __cplusplus
}
#endif

#endif /* #ifndef ECJPAKE_ALT_H */

/** @} */
