/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * Minimal mbedTLS AES API implemented over tiny-AES-c. Only what the DESFire
 * engine uses: AES-128 CBC in both directions. See mbedtls/des.h for the
 * direction convention this shim follows -- nothing reverses a key schedule;
 * crypt_cbc dispatches on `mode`.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <aes.h>

#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0

#define MBEDTLS_ERR_AES_INVALID_KEY_LENGTH -0x0020
#define MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH -0x0022

typedef struct {
    struct AES_ctx k; /* round key expanded once at setkey time */
    uint8_t has_key;
} mbedtls_aes_context;

void mbedtls_aes_init(mbedtls_aes_context* ctx);
void mbedtls_aes_free(mbedtls_aes_context* ctx);

/* DESFire uses AES-128 only. `keybits` other than 128 is rejected with
 * MBEDTLS_ERR_AES_INVALID_KEY_LENGTH rather than silently truncating.
 * setkey_enc and setkey_dec are deliberately identical: tiny-AES-c derives the
 * inverse schedule internally, so there is no decrypt-specific setup. */
int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const uint8_t* key, unsigned int keybits);
int mbedtls_aes_setkey_dec(mbedtls_aes_context* ctx, const uint8_t* key, unsigned int keybits);

/* `input` and `output` must be identical or non-overlapping. `length` must be
 * a multiple of 16. `iv` is updated in place to the trailing ciphertext block. */
int mbedtls_aes_crypt_cbc(
    mbedtls_aes_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[16],
    const uint8_t* input,
    uint8_t* output);
