/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * Minimal mbedTLS AES API implemented over the nRF5 SDK's nrf_crypto (CC310
 * hardware AES), not tiny-AES-c. Only what the DESFire engine uses: AES-128
 * CBC in both directions, plus single-block ECB (used by AES-CMAC). The
 * backend lives in mbedtls_compat.c; this header stays free of nrf_crypto
 * types so the DESFire engine keeps a clean mbedTLS include.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0

#define MBEDTLS_ERR_AES_INVALID_KEY_LENGTH -0x0020
#define MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH -0x0022

typedef struct {
    uint8_t key[16]; /* AES-128 key; handed to nrf_crypto per operation */
    uint8_t has_key;
} mbedtls_aes_context;

void mbedtls_aes_init(mbedtls_aes_context* ctx);
void mbedtls_aes_free(mbedtls_aes_context* ctx);

/* DESFire uses AES-128 only. `keybits` other than 128 is rejected with
 * MBEDTLS_ERR_AES_INVALID_KEY_LENGTH rather than silently truncating.
 * setkey_enc and setkey_dec are deliberately identical: tiny-AES-c derives the
 * inverse schedule internally, so there is no decrypt-specific setup. */
int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const uint8_t *key, unsigned int keybits);
int mbedtls_aes_setkey_dec(mbedtls_aes_context* ctx, const uint8_t *key, unsigned int keybits);

/* `input` and `output` must be identical or non-overlapping. `length` must be
 * a multiple of 16. `iv` is updated in place to the trailing ciphertext block. */
int mbedtls_aes_crypt_cbc(
    mbedtls_aes_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[16],
    const uint8_t *input,
    uint8_t *output);

/* Single-block ECB, implemented as one-block CBC with a zero IV (identical
 * result: AES-encrypt(input) for encrypt, AES-decrypt(input) for decrypt). */
int mbedtls_aes_crypt_ecb(
    mbedtls_aes_context* ctx,
    int mode,
    const uint8_t input[16],
    uint8_t output[16]);
