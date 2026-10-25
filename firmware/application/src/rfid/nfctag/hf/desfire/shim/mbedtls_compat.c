/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * mbedTLS DES/3DES compatibility over tiny-DES-c, and mbedTLS AES over the
 * nRF5 SDK's nrf_crypto (CC310 hardware AES). See shim/mbedtls/des.h for the
 * direction convention. In short: no key schedule is ever reversed here. ECB
 * direction comes from `dir` recorded at setkey time; CBC direction comes from
 * the `mode` argument.
 */

#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/des.h"

#include "nrf_crypto_init.h"
#include "nrf_crypto_aes.h"
#include "sdk_errors.h"

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
    if (output != input) memcpy(output, input, DES_BLOCKLEN);
    if (ctx->dir == MBEDTLS_DES_ENCRYPT) {
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
    const uint8_t *input,
    uint8_t *output) {
    if (length % DES_BLOCKLEN != 0) return MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH;

    DES_ctx_set_iv(&ctx->k, iv);
    if (output != input) memcpy(output, input, length);

    if (mode == MBEDTLS_DES_ENCRYPT) {
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
    if (output != input) memcpy(output, input, DES_BLOCKLEN);
    if (ctx->dir == MBEDTLS_DES_ENCRYPT) {
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
    const uint8_t *input,
    uint8_t *output) {
    if (length % DES_BLOCKLEN != 0) return MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH;

    DES3_ctx_set_iv(&ctx->k, iv);
    if (output != input) memcpy(output, input, length);

    if (mode == MBEDTLS_DES_ENCRYPT) {
        DES3_CBC_encrypt(&ctx->k, output, length);
    } else {
        DES3_CBC_decrypt(&ctx->k, output, length);
    }

    memcpy(iv, ctx->k.Iv, DES_BLOCKLEN);
    return 0;
}

/* ---------------------------------------------------------------- AES ----
 * AES-128 CBC over nrf_crypto (CC310 hardware). The mbedTLS-style API is what
 * the DESFire engine calls; this backend only stores the key at setkey time and
 * runs the one-shot nrf_crypto_aes_crypt per CBC call, mirroring the pattern in
 * nfc_seos.c. nrf_crypto is initialised lazily and idempotently so DESFire does
 * not depend on any particular boot-time init ordering. */

#define AES_SHIM_BLOCK 16u

static nrf_crypto_aes_context_t s_aes_ctx; /* single card emulated at a time */

static void aes_shim_ensure_init(void) {
    static bool ready = false;
    if (ready) return;
    ret_code_t rc = nrf_crypto_init();
    /* Already initialised elsewhere (e.g. SEOS/boot) is fine. */
    if (rc == NRF_SUCCESS || rc == NRF_ERROR_MODULE_ALREADY_INITIALIZED) {
        ready = true;
    }
}

void mbedtls_aes_init(mbedtls_aes_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_aes_free(mbedtls_aes_context* ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

static int aes_setkey(mbedtls_aes_context* ctx, const uint8_t *key, unsigned int keybits) {
    if (keybits != 128) return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;
    aes_shim_ensure_init();
    memcpy(ctx->key, key, 16);
    ctx->has_key = 1;
    return 0;
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const uint8_t *key, unsigned int keybits) {
    return aes_setkey(ctx, key, keybits);
}

int mbedtls_aes_setkey_dec(mbedtls_aes_context* ctx, const uint8_t *key, unsigned int keybits) {
    /* Direction is chosen per call in crypt_cbc, so decrypt setup is identical
       to encrypt setup: the key is only stored here. */
    return aes_setkey(ctx, key, keybits);
}

int mbedtls_aes_crypt_cbc(
    mbedtls_aes_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[16],
    const uint8_t *input,
    uint8_t *output) {
    if (length % AES_SHIM_BLOCK != 0) return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    if (!ctx->has_key) return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;
    if (length == 0) return 0;

    /* CC310's AES DMA requires non-overlapping input/output (nfc_seos.c always
       passes distinct buffers). dfc-core may call this in place (input == output),
       so never hand nrf_crypto an aliased pair: stage each block-aligned chunk
       through separate local buffers, carrying the CBC IV forward between chunks.
       Chunked CBC with a running IV is identical to a single-shot CBC. */
    uint8_t inbuf[64];   /* up to 4 AES blocks per nrf_crypto call */
    uint8_t outbuf[64];
    uint8_t iv_run[16];
    memcpy(iv_run, iv, 16);

    for (size_t off = 0; off < length; off += sizeof(inbuf)) {
        size_t chunk = length - off;
        if (chunk > sizeof(inbuf)) chunk = sizeof(inbuf);

        memcpy(inbuf, input + off, chunk);  /* read first: input may alias output */
        uint8_t next_iv[16];
        if (mode == MBEDTLS_AES_DECRYPT) {
            memcpy(next_iv, inbuf + chunk - AES_SHIM_BLOCK, AES_SHIM_BLOCK);
        }

        size_t out_len = chunk;
        ret_code_t rc = nrf_crypto_aes_crypt(
                            &s_aes_ctx, &g_nrf_crypto_aes_cbc_128_info,
                            (mode == MBEDTLS_AES_ENCRYPT) ? NRF_CRYPTO_ENCRYPT : NRF_CRYPTO_DECRYPT,
                            ctx->key, iv_run,
                            inbuf, chunk,
                            outbuf, &out_len);
        if (rc != NRF_SUCCESS) return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;

        memcpy(output + off, outbuf, chunk);
        if (mode == MBEDTLS_AES_ENCRYPT) {
            memcpy(iv_run, outbuf + chunk - AES_SHIM_BLOCK, AES_SHIM_BLOCK);
        } else {
            memcpy(iv_run, next_iv, AES_SHIM_BLOCK);
        }
    }

    memcpy(iv, iv_run, AES_SHIM_BLOCK);  /* mbedTLS: iv -> trailing ciphertext block */
    return 0;
}

int mbedtls_aes_crypt_ecb(
    mbedtls_aes_context* ctx, int mode, const uint8_t input[16], uint8_t output[16]) {
    uint8_t iv[AES_SHIM_BLOCK] = {0};
    return mbedtls_aes_crypt_cbc(ctx, mode, AES_SHIM_BLOCK, iv, input, output);
}
