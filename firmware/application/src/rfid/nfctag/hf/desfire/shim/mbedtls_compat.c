/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * mbedTLS DES/3DES/AES compatibility layer over tiny-DES-c and tiny-AES-c.
 * See shim/mbedtls/des.h for the direction convention. In short: no key
 * schedule is ever reversed here. ECB direction comes from `dir` recorded at
 * setkey time; CBC direction comes from the `mode` argument.
 */

#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/des.h"

/* ---------------------------------------------------------------- DES ---- */

void mbedtls_des_init(mbedtls_des_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_des_free(mbedtls_des_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_des_setkey_enc(mbedtls_des_context* ctx, const uint8_t key[8]) {
    DES_init_ctx(&ctx->k, key);
    ctx->dir = MBEDTLS_DES_ENCRYPT;
    return 0;
}

int mbedtls_des_setkey_dec(mbedtls_des_context* ctx, const uint8_t key[8]) {
    /* Same schedule as encrypt -- direction is expressed by `dir`, not by
       reordering subkeys. */
    DES_init_ctx(&ctx->k, key);
    ctx->dir = MBEDTLS_DES_DECRYPT;
    return 0;
}

int mbedtls_des_crypt_ecb(mbedtls_des_context* ctx, const uint8_t input[8], uint8_t output[8]) {
    if(output != input) memcpy(output, input, DES_BLOCKLEN);
    if(ctx->dir == MBEDTLS_DES_ENCRYPT) {
        DES_ECB_encrypt(&ctx->k, output);
    } else {
        DES_ECB_decrypt(&ctx->k, output);
    }
    return 0;
}

int mbedtls_des_crypt_cbc(
    mbedtls_des_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[8],
    const uint8_t* input,
    uint8_t* output) {
    if(length % DES_BLOCKLEN != 0) return MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH;

    DES_ctx_set_iv(&ctx->k, iv);
    if(output != input) memcpy(output, input, length);

    if(mode == MBEDTLS_DES_ENCRYPT) {
        DES_CBC_encrypt(&ctx->k, output, length);
    } else {
        DES_CBC_decrypt(&ctx->k, output, length);
    }

    /* tiny-DES-c leaves the trailing ciphertext block in ctx->Iv for both
       directions, which is exactly what mbedTLS writes back. */
    memcpy(iv, ctx->k.Iv, DES_BLOCKLEN);
    return 0;
}

/* --------------------------------------------------------------- 3DES ---- */

void mbedtls_des3_init(mbedtls_des3_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_des3_free(mbedtls_des3_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_des3_set2key_enc(mbedtls_des3_context* ctx, const uint8_t key[16]) {
    DES3_init_ctx(&ctx->k, key, DES3_KEYLEN_2KEY);
    ctx->dir = MBEDTLS_DES_ENCRYPT;
    return 0;
}

int mbedtls_des3_set2key_dec(mbedtls_des3_context* ctx, const uint8_t key[16]) {
    DES3_init_ctx(&ctx->k, key, DES3_KEYLEN_2KEY);
    ctx->dir = MBEDTLS_DES_DECRYPT;
    return 0;
}

int mbedtls_des3_set3key_enc(mbedtls_des3_context* ctx, const uint8_t key[24]) {
    DES3_init_ctx(&ctx->k, key, DES3_KEYLEN_3KEY);
    ctx->dir = MBEDTLS_DES_ENCRYPT;
    return 0;
}

int mbedtls_des3_set3key_dec(mbedtls_des3_context* ctx, const uint8_t key[24]) {
    DES3_init_ctx(&ctx->k, key, DES3_KEYLEN_3KEY);
    ctx->dir = MBEDTLS_DES_DECRYPT;
    return 0;
}

int mbedtls_des3_crypt_ecb(mbedtls_des3_context* ctx, const uint8_t input[8], uint8_t output[8]) {
    if(output != input) memcpy(output, input, DES_BLOCKLEN);
    if(ctx->dir == MBEDTLS_DES_ENCRYPT) {
        DES3_ECB_encrypt(&ctx->k, output);
    } else {
        DES3_ECB_decrypt(&ctx->k, output);
    }
    return 0;
}

int mbedtls_des3_crypt_cbc(
    mbedtls_des3_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[8],
    const uint8_t* input,
    uint8_t* output) {
    if(length % DES_BLOCKLEN != 0) return MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH;

    DES3_ctx_set_iv(&ctx->k, iv);
    if(output != input) memcpy(output, input, length);

    if(mode == MBEDTLS_DES_ENCRYPT) {
        DES3_CBC_encrypt(&ctx->k, output, length);
    } else {
        DES3_CBC_decrypt(&ctx->k, output, length);
    }

    memcpy(iv, ctx->k.Iv, DES_BLOCKLEN);
    return 0;
}

/* ---------------------------------------------------------------- AES ---- */

void mbedtls_aes_init(mbedtls_aes_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_aes_free(mbedtls_aes_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

static int aes_setkey(mbedtls_aes_context* ctx, const uint8_t* key, unsigned int keybits) {
    if(keybits != 128) return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;
    AES_init_ctx(&ctx->k, key);
    ctx->has_key = 1;
    return 0;
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const uint8_t* key, unsigned int keybits) {
    return aes_setkey(ctx, key, keybits);
}

int mbedtls_aes_setkey_dec(mbedtls_aes_context* ctx, const uint8_t* key, unsigned int keybits) {
    /* tiny-AES-c derives the inverse schedule inside InvCipher, so decrypt
       setup is identical to encrypt setup. */
    return aes_setkey(ctx, key, keybits);
}

int mbedtls_aes_crypt_cbc(
    mbedtls_aes_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[16],
    const uint8_t* input,
    uint8_t* output) {
    if(length % AES_BLOCKLEN != 0) return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    if(!ctx->has_key) return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;

    AES_ctx_set_iv(&ctx->k, iv);
    if(output != input) memcpy(output, input, length);

    if(mode == MBEDTLS_AES_ENCRYPT) {
        AES_CBC_encrypt(&ctx->k, output, length);
    } else {
        AES_CBC_decrypt(&ctx->k, output, length);
    }

    memcpy(iv, ctx->k.Iv, AES_BLOCKLEN);
    return 0;
}
