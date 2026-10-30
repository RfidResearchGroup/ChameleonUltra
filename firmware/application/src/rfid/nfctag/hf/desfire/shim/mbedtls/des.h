/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 CinderSocket
 *
 * Minimal mbedTLS DES/3DES API implemented over tiny-DES-c, so the DESFire
 * engine can keep its upstream `#include <mbedtls/des.h>` without carrying an
 * mbedTLS dependency into the firmware.
 *
 * DIRECTION CONVENTION -- read before extending.
 *
 * mbedTLS and tiny-DES-c express cipher direction differently:
 *
 *   mbedTLS      direction lives entirely in the key schedule. crypt_ecb takes
 *                no mode; crypt_cbc's `mode` selects only the XOR chaining
 *                order, not the cipher direction.
 *   tiny-DES-c   direction lives in the function name (DES_ECB_encrypt vs
 *                DES_ECB_decrypt); the schedule is always encrypt-order.
 *
 * These are mutually exclusive. Mixing them -- reversing the schedule in
 * setkey_dec *and* dispatching on a direction elsewhere -- double-reverses and
 * silently produces garbage. This shim commits to the tiny-DES-c convention
 * throughout: nothing ever reverses a schedule, setkey_* records the intended
 * direction in `dir`, crypt_ecb dispatches on `dir`, and crypt_cbc dispatches
 * on `mode`. Callers must keep setkey direction and crypt_cbc mode consistent,
 * which is what mbedTLS requires of them anyway.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <des.h>

#define MBEDTLS_DES_ENCRYPT 1
#define MBEDTLS_DES_DECRYPT 0

/* Value matches upstream mbedTLS so callers comparing against it still work. */
#define MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH -0x0032

typedef struct {
    struct DES_ctx k;
    uint8_t dir; /* MBEDTLS_DES_ENCRYPT | MBEDTLS_DES_DECRYPT */
} mbedtls_des_context;

typedef struct {
    struct DES3_ctx k;
    uint8_t dir;
} mbedtls_des3_context;

void mbedtls_des_init(mbedtls_des_context* ctx);
void mbedtls_des_free(mbedtls_des_context* ctx);
int mbedtls_des_setkey_enc(mbedtls_des_context* ctx, const uint8_t key[8]);
int mbedtls_des_setkey_dec(mbedtls_des_context* ctx, const uint8_t key[8]);
int mbedtls_des_crypt_ecb(mbedtls_des_context* ctx, const uint8_t input[8], uint8_t output[8]);

/* `input` and `output` must be identical or non-overlapping. `length` must be
 * a multiple of 8; otherwise MBEDTLS_ERR_DES_INVALID_INPUT_LENGTH is returned
 * and no output is written. `iv` is updated in place to the trailing
 * ciphertext block, matching mbedTLS. */
int mbedtls_des_crypt_cbc(
    mbedtls_des_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[8],
    const uint8_t* input,
    uint8_t* output);

void mbedtls_des3_init(mbedtls_des3_context* ctx);
void mbedtls_des3_free(mbedtls_des3_context* ctx);
int mbedtls_des3_set2key_enc(mbedtls_des3_context* ctx, const uint8_t key[16]);
int mbedtls_des3_set2key_dec(mbedtls_des3_context* ctx, const uint8_t key[16]);
int mbedtls_des3_set3key_enc(mbedtls_des3_context* ctx, const uint8_t key[24]);
int mbedtls_des3_set3key_dec(mbedtls_des3_context* ctx, const uint8_t key[24]);
int mbedtls_des3_crypt_ecb(mbedtls_des3_context* ctx, const uint8_t input[8], uint8_t output[8]);
int mbedtls_des3_crypt_cbc(
    mbedtls_des3_context* ctx,
    int mode,
    size_t length,
    uint8_t iv[8],
    const uint8_t* input,
    uint8_t* output);
