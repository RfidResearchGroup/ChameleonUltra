#include "ulc_3des.h"
#include <string.h>
#include <stdbool.h>

static const uint8_t IP[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7
};

static const uint8_t FP[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25
};

static const uint8_t E_BITS[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11,
    12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
    22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1
};

static const uint8_t P_PERM[32] = {
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25
};

static const uint8_t PC1[56] = {
    57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4
};

static const uint8_t PC2[48] = {
    14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32
};

static const uint8_t SHIFTS[16] = {
    1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1
};

static const uint8_t SBOX[8][64] = {
    {
        14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
        0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
        4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
        15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13
    },
    {
        15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
        3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
        0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
        13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9
    },
    {
        10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
        13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
        1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12
    },
    {
        7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
        13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
        10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
        3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14
    },
    {
        2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
        14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
        4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
        11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3
    },
    {
        12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
        10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
        9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
        4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13
    },
    {
        4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
        13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
        1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
        6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12
    },
    {
        13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
        1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
        7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
        2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11
    }
};

typedef struct {
    uint8_t subkeys[16][6];
} des_ctx_t;

static uint64_t des_permute(uint64_t in, const uint8_t *table, int n, int in_bits) {
    uint64_t out = 0;
    for (int i = 0; i < n; i++) {
        out <<= 1;
        out |= (in >> (in_bits - table[i])) & 1ULL;
    }
    return out;
}

static uint32_t SP[8][64];
static bool sp_ready = false;

static void des_init_sp(void) {
    if (sp_ready) {
        return;
    }
    for (int j = 0; j < 8; j++) {
        for (int v = 0; v < 64; v++) {
            uint32_t pre = (uint32_t)SBOX[j][v] << (28 - 4 * j);
            SP[j][v] = (uint32_t)des_permute(pre, P_PERM, 32, 32);
        }
    }
    sp_ready = true;
}

static void des_setkey(des_ctx_t *ctx, const uint8_t key[8]) {
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) {
        k = (k << 8) | key[i];
    }

    uint64_t cd = des_permute(k, PC1, 56, 64);
    uint32_t c = (uint32_t)((cd >> 28) & 0x0FFFFFFFUL);
    uint32_t d = (uint32_t)(cd & 0x0FFFFFFFUL);

    for (int round = 0; round < 16; round++) {
        uint8_t s = SHIFTS[round];
        c = ((c << s) | (c >> (28 - s))) & 0x0FFFFFFFUL;
        d = ((d << s) | (d >> (28 - s))) & 0x0FFFFFFFUL;

        uint64_t cd_round = (((uint64_t)c) << 28) | (uint64_t)d;
        uint64_t sk = des_permute(cd_round, PC2, 48, 56);
        for (int i = 0; i < 6; i++) {
            ctx->subkeys[round][i] = (uint8_t)((sk >> (40 - 8 * i)) & 0xFF);
        }
    }
}

static uint32_t des_feistel(uint32_t r, const uint8_t subkey[6]) {
    uint64_t er = des_permute(r, E_BITS, 48, 32);

    uint64_t k = 0;
    for (int i = 0; i < 6; i++) {
        k = (k << 8) | subkey[i];
    }
    er ^= k;

    uint32_t out = 0;
    for (int j = 0; j < 8; j++) {
        int shift = 42 - 6 * j;
        uint8_t v = (uint8_t)((er >> shift) & 0x3F);
        uint8_t row = (uint8_t)(((v & 0x20) >> 4) | (v & 1));
        uint8_t col = (uint8_t)((v >> 1) & 0x0F);
        out |= SP[j][row * 16 + col];
    }

    return out;
}

static void des_crypt_block(const des_ctx_t *ctx, const uint8_t in[8], uint8_t out[8], int decrypt) {
    uint64_t block = 0;
    for (int i = 0; i < 8; i++) {
        block = (block << 8) | in[i];
    }

    block = des_permute(block, IP, 64, 64);
    uint32_t l = (uint32_t)(block >> 32);
    uint32_t r = (uint32_t)(block & 0xFFFFFFFFUL);

    for (int round = 0; round < 16; round++) {
        int idx = decrypt ? (15 - round) : round;
        uint32_t tmp = r;
        r = l ^ des_feistel(r, ctx->subkeys[idx]);
        l = tmp;
    }

    uint64_t pre = (((uint64_t)r) << 32) | (uint64_t)l;
    uint64_t res = des_permute(pre, FP, 64, 64);

    for (int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(res & 0xFF);
        res >>= 8;
    }
}

typedef struct {
    des_ctx_t k1;
    des_ctx_t k2;
} tdes_ctx_t;

static void tdes_setkey(tdes_ctx_t *ctx, const uint8_t key[16]) {
    des_init_sp();
    des_setkey(&ctx->k1, key);
    des_setkey(&ctx->k2, key + 8);
}

static void tdes_ecb_encrypt(const tdes_ctx_t *ctx, const uint8_t in[8], uint8_t out[8]) {
    uint8_t t[8];
    des_crypt_block(&ctx->k1, in, t, 0);
    des_crypt_block(&ctx->k2, t, out, 1);
    des_crypt_block(&ctx->k1, out, t, 0);
    memcpy(out, t, 8);
}

static void tdes_ecb_decrypt(const tdes_ctx_t *ctx, const uint8_t in[8], uint8_t out[8]) {
    uint8_t t[8];
    des_crypt_block(&ctx->k1, in, t, 1);
    des_crypt_block(&ctx->k2, t, out, 0);
    des_crypt_block(&ctx->k1, out, t, 1);
    memcpy(out, t, 8);
}

void ulc_tdes_enc_cbc(uint8_t *out, const uint8_t *in, size_t len, const uint8_t key[16], uint8_t iv[8]) {
    if (len % 8) {
        return;
    }

    tdes_ctx_t ctx;
    tdes_setkey(&ctx, key);

    for (size_t off = 0; off < len; off += 8) {
        uint8_t blk[8];
        for (int i = 0; i < 8; i++) {
            blk[i] = (uint8_t)(in[off + i] ^ iv[i]);
        }
        tdes_ecb_encrypt(&ctx, blk, out + off);
        memcpy(iv, out + off, 8);
    }
}

void ulc_tdes_dec_cbc(uint8_t *out, const uint8_t *in, size_t len, const uint8_t key[16], uint8_t iv[8]) {
    if (len % 8) {
        return;
    }

    tdes_ctx_t ctx;
    tdes_setkey(&ctx, key);

    for (size_t off = 0; off < len; off += 8) {
        uint8_t cipher[8];
        memcpy(cipher, in + off, 8);
        tdes_ecb_decrypt(&ctx, cipher, out + off);
        for (int i = 0; i < 8; i++) {
            out[off + i] = (uint8_t)(out[off + i] ^ iv[i]);
        }
        memcpy(iv, cipher, 8);
    }
}

void ulc_rol8(uint8_t *data) {
    uint8_t first = data[0];
    for (int i = 0; i < 7; i++) {
        data[i] = data[i + 1];
    }
    data[7] = first;
}

void ulc_3des_init(void) {
    des_init_sp();
}
