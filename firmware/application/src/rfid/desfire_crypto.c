#include "desfire_crypto.h"

#include "nrf_crypto.h"
#include "nrf_crypto_aes.h"
#include "nrf_drv_rng.h"
#include "nrf_log.h"

#include <string.h>
#include <stdbool.h>

/* =======================================================================
 * AES — hardware CC310
 * ===================================================================== */

static ret_code_t aes_ecb(bool encrypt, const uint8_t key[16],
                            const uint8_t in[16], uint8_t out[16])
{
    nrf_crypto_aes_context_t ctx;
    size_t out_len = 16;
    ret_code_t err;

    err = nrf_crypto_aes_init(&ctx, &g_nrf_crypto_aes_ecb_128_info,
                               encrypt ? NRF_CRYPTO_ENCRYPT : NRF_CRYPTO_DECRYPT);
    if (err) return err;
    err = nrf_crypto_aes_key_set(&ctx, (uint8_t *)key);
    if (!err) err = nrf_crypto_aes_finalize(&ctx, (uint8_t *)in, 16, out, &out_len);
    nrf_crypto_aes_uninit(&ctx);
    return err;
}

ret_code_t desfire_aes_ecb_encrypt(const uint8_t key[16],
                                    const uint8_t in[16], uint8_t out[16])
{ return aes_ecb(true,  key, in, out); }

ret_code_t desfire_aes_ecb_decrypt(const uint8_t key[16],
                                    const uint8_t in[16], uint8_t out[16])
{ return aes_ecb(false, key, in, out); }


static ret_code_t aes_cbc(bool encrypt, const uint8_t key[16], uint8_t iv[16],
                            const uint8_t *in, uint8_t *out, size_t len)
{
    if (!len || (len & 0xF)) return NRF_ERROR_INVALID_LENGTH;

    nrf_crypto_aes_context_t ctx;
    size_t out_len = len;
    ret_code_t err;

    err = nrf_crypto_aes_init(&ctx, &g_nrf_crypto_aes_cbc_128_info,
                               encrypt ? NRF_CRYPTO_ENCRYPT : NRF_CRYPTO_DECRYPT);
    if (err) return err;
    err = nrf_crypto_aes_key_set(&ctx, (uint8_t *)key);
    if (!err) err = nrf_crypto_aes_iv_set(&ctx, iv);
    if (!err) err = nrf_crypto_aes_finalize(&ctx, (uint8_t *)in, len, out, &out_len);
    if (!err) {
        /* Update IV to last ciphertext block for CBC chaining */
        memcpy(iv, (encrypt ? out : in) + len - 16, 16);
    }
    nrf_crypto_aes_uninit(&ctx);
    return err;
}

ret_code_t desfire_aes_cbc_encrypt(const uint8_t key[16], uint8_t iv[16],
                                    const uint8_t *in, uint8_t *out, size_t len)
{ return aes_cbc(true,  key, iv, in, out, len); }

ret_code_t desfire_aes_cbc_decrypt(const uint8_t key[16], uint8_t iv[16],
                                    const uint8_t *in, uint8_t *out, size_t len)
{ return aes_cbc(false, key, iv, in, out, len); }


ret_code_t desfire_aes_cmac(const uint8_t *key, const uint8_t *msg,
                              size_t len, uint8_t mac_out[16])
{
    nrf_crypto_aes_context_t ctx;
    size_t mac_len = 16;
    ret_code_t err;

    err = nrf_crypto_aes_init(&ctx, &g_nrf_crypto_aes_cmac_128_info,
                               NRF_CRYPTO_MAC_CALCULATE);
    if (err) return err;
    err = nrf_crypto_aes_key_set(&ctx, (uint8_t *)key);
    if (!err) err = nrf_crypto_aes_finalize(&ctx, (uint8_t *)msg, len,
                                              mac_out, &mac_len);
    nrf_crypto_aes_uninit(&ctx);
    return err;
}


ret_code_t desfire_derive_session_key(const uint8_t key[16],
                                       const uint8_t rnd_a[16],
                                       const uint8_t rnd_b[16],
                                       uint8_t       sk_out[16])
{
    /* NXP AN10922 §4.1 — AES session value (24 bytes):
     *   SV = RndA[0..3] || RndB[0..3]
     *      || RndA[4..7]  ^ RndB[0..3]
     *      || RndA[8..11] ^ RndB[4..7]
     *      || RndA[12..15]^ RndB[8..11]
     *      || RndB[12..15]
     *  SK = AES-CMAC(key, SV)
     */
    uint8_t sv[24];
    memcpy(sv,      rnd_a,       4);
    memcpy(sv +  4, rnd_b,       4);
    for (int i = 0; i < 12; i++) sv[8 + i] = rnd_a[4 + i] ^ rnd_b[i];
    memcpy(sv + 20, rnd_b + 12,  4);
    return desfire_aes_cmac(key, sv, sizeof(sv), sk_out);
}


/* =======================================================================
 * DES / 2K3DES — compact software Feistel implementation
 * No CC310 support for DES; software is fast enough on Cortex-M4.
 * =======================================================================
 *
 * Tables follow FIPS PUB 46-3.
 */

/* Initial / final permutation combined as bit-index lookup (1-based) */
static const uint8_t IP[64] = {
    58,50,42,34,26,18,10, 2, 60,52,44,36,28,20,12, 4,
    62,54,46,38,30,22,14, 6, 64,56,48,40,32,24,16, 8,
    57,49,41,33,25,17, 9, 1, 59,51,43,35,27,19,11, 3,
    61,53,45,37,29,21,13, 5, 63,55,47,39,31,23,15, 7
};
static const uint8_t FP[64] = {
    40, 8,48,16,56,24,64,32, 39, 7,47,15,55,23,63,31,
    38, 6,46,14,54,22,62,30, 37, 5,45,13,53,21,61,29,
    36, 4,44,12,52,20,60,28, 35, 3,43,11,51,19,59,27,
    34, 2,42,10,50,18,58,26, 33, 1,41, 9,49,17,57,25
};
/* Permuted-choice 1 (64→56 key bits) */
static const uint8_t PC1[56] = {
    57,49,41,33,25,17, 9,  1,58,50,42,34,26,18,
    10, 2,59,51,43,35, 27,19,11, 3,60,52,44,36,
    63,55,47,39,31,23, 15, 7,62,54,46,38,30,22,
    14, 6,61,53,45,37, 29,21,13, 5,28,20,12, 4
};
/* Permuted-choice 2 (56→48 subkey bits) */
static const uint8_t PC2[48] = {
    14,17,11,24, 1, 5,  3,28,15, 6,21,10,
    23,19,12, 4,26, 8, 16, 7,27,20,13, 2,
    41,52,31,37,47,55, 30,40,51,45,33,48,
    44,49,39,56,34,53, 46,42,50,36,29,32
};
/* Left-shift schedule */
static const uint8_t SHIFTS[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
/* E expansion (32→48) */
static const uint8_t E[48] = {
    32, 1, 2, 3, 4, 5,  4, 5, 6, 7, 8, 9,
     8, 9,10,11,12,13, 12,13,14,15,16,17,
    16,17,18,19,20,21, 20,21,22,23,24,25,
    24,25,26,27,28,29, 28,29,30,31,32, 1
};
/* P permutation (32→32) */
static const uint8_t P_BOX[32] = {
    16, 7,20,21, 29,12,28,17, 1,15,23,26, 5,18,31,10,
     2, 8,24,14, 32,27, 3, 9, 19,13,30, 6, 22,11, 4,25
};
/* S-boxes */
static const uint8_t SBOX[8][64] = {
  {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,
    0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
    4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,
    15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
  {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,
    3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
    0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,
    13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
  {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,
    13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
    13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,
    1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
  {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,
    13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
    10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,
    3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
  {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,
    14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
    4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,
    11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
  {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,
    10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
    9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,
    4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
  {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,
    13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
    1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,
    6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
  {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,
    1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
    7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,
    2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};

/* Bit-manipulation helpers (1-based bit indexing, MSB=bit 1) */
static inline int  getbit(const uint8_t *v, int bit) {
    bit--;
    return (v[bit>>3] >> (7-(bit&7))) & 1;
}
static inline void setbit(uint8_t *v, int bit, int val) {
    bit--;
    if (val) v[bit>>3] |=  (1 << (7-(bit&7)));
    else     v[bit>>3] &= ~(1 << (7-(bit&7)));
}

static void des_permute(const uint8_t *in, uint8_t *out,
                         const uint8_t *tbl, int n)
{
    uint8_t tmp[8] = {0};
    for (int i = 0; i < n; i++) setbit(tmp, i+1, getbit(in, tbl[i]));
    memcpy(out, tmp, (n+7)/8);
}

static void des_key_schedule(const uint8_t key[8], uint8_t sub[16][6])
{
    uint8_t kp[7] = {0};   /* 56 bits after PC1 */
    des_permute(key, kp, PC1, 56);

    uint8_t C[4] = {0}, D[4] = {0};
    for (int i = 0; i < 28; i++) setbit(C, i+1, getbit(kp, i+1));
    for (int i = 0; i < 28; i++) setbit(D, i+1, getbit(kp, 28+i+1));

    for (int r = 0; r < 16; r++) {
        /* Circular left-shift C and D */
        for (int s = 0; s < SHIFTS[r]; s++) {
            int cb = getbit(C, 1), db = getbit(D, 1);
            for (int i = 1; i < 28; i++) setbit(C, i, getbit(C, i+1));
            setbit(C, 28, cb);
            for (int i = 1; i < 28; i++) setbit(D, i, getbit(D, i+1));
            setbit(D, 28, db);
        }
        /* Merge C||D into 56-bit value, apply PC2 */
        uint8_t cd[7] = {0};
        for (int i = 0; i < 28; i++) setbit(cd, i+1,    getbit(C, i+1));
        for (int i = 0; i < 28; i++) setbit(cd, 28+i+1, getbit(D, i+1));
        memset(sub[r], 0, 6);
        des_permute(cd, sub[r], PC2, 48);
    }
}

static void des_block(const uint8_t key[8], const uint8_t in[8],
                       uint8_t out[8], bool encrypt)
{
    uint8_t sub[16][6];
    des_key_schedule(key, sub);

    /* IP */
    uint8_t m[8] = {0};
    des_permute(in, m, IP, 64);

    uint8_t L[4] = {0}, R[4] = {0};
    for (int i = 0; i < 32; i++) setbit(L, i+1, getbit(m, i+1));
    for (int i = 0; i < 32; i++) setbit(R, i+1, getbit(m, 32+i+1));

    for (int r = 0; r < 16; r++) {
        int ri = encrypt ? r : (15 - r);
        /* E(R) XOR subkey */
        uint8_t er[6] = {0};
        des_permute(R, er, E, 48);
        for (int i = 0; i < 6; i++) er[i] ^= sub[ri][i];

        /* S-box substitution */
        uint8_t sb[4] = {0};
        for (int s = 0; s < 8; s++) {
            int base = s * 6;
            int row  = (getbit(er, base+1) << 1) | getbit(er, base+6);
            int col  = (getbit(er, base+2) << 3) | (getbit(er, base+3) << 2)
                     | (getbit(er, base+4) << 1) |  getbit(er, base+5);
            int val  = SBOX[s][row*16 + col];
            int sbase = s * 4;
            setbit(sb, sbase+1, (val>>3)&1);
            setbit(sb, sbase+2, (val>>2)&1);
            setbit(sb, sbase+3, (val>>1)&1);
            setbit(sb, sbase+4,  val    &1);
        }

        /* P permutation */
        uint8_t fp[4] = {0};
        des_permute(sb, fp, P_BOX, 32);

        /* f(R, K) XOR L → new R; old R → new L */
        uint8_t newR[4];
        for (int i = 0; i < 4; i++) newR[i] = L[i] ^ fp[i];
        memcpy(L, R, 4);
        memcpy(R, newR, 4);
    }

    /* R16||L16 → FP */
    uint8_t rl[8] = {0};
    for (int i = 0; i < 32; i++) setbit(rl, i+1,    getbit(R, i+1));
    for (int i = 0; i < 32; i++) setbit(rl, 32+i+1, getbit(L, i+1));
    des_permute(rl, out, FP, 64);
}

void desfire_des_ecb(const uint8_t key[8], const uint8_t in[8],
                     uint8_t out[8], bool encrypt)
{
    des_block(key, in, out, encrypt);
}

static void tdes_block(const uint8_t key[24], const uint8_t in[8],
                        uint8_t out[8], bool encrypt)
{
    /* 3DES EDE: encrypt with K1, decrypt with K2, encrypt with K3 */
    uint8_t tmp1[8], tmp2[8];
    if (encrypt) {
        des_block(key,      in,   tmp1, true);
        des_block(key +  8, tmp1, tmp2, false);
        des_block(key + 16, tmp2, out,  true);
    } else {
        des_block(key + 16, in,   tmp1, false);
        des_block(key +  8, tmp1, tmp2, true);
        des_block(key,      tmp2, out,  false);
    }
}

void desfire_2k3des_cbc_encrypt(const uint8_t key16[16], uint8_t iv[8],
                                 const uint8_t *in, uint8_t *out, size_t len)
{
    /* Expand 2K3DES key: K1||K2 → K1||K2||K1 */
    uint8_t k24[24];
    memcpy(k24,      key16,     16);
    memcpy(k24 + 16, key16,      8);

    for (size_t i = 0; i < len; i += 8) {
        uint8_t blk[8];
        for (int j = 0; j < 8; j++) blk[j] = in[i+j] ^ iv[j];
        tdes_block(k24, blk, out + i, true);
        memcpy(iv, out + i, 8);
    }
}

void desfire_2k3des_cbc_decrypt(const uint8_t key16[16], uint8_t iv[8],
                                 const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t k24[24];
    memcpy(k24,      key16,     16);
    memcpy(k24 + 16, key16,      8);

    for (size_t i = 0; i < len; i += 8) {
        uint8_t tmp[8];
        tdes_block(k24, in + i, tmp, false);
        for (int j = 0; j < 8; j++) out[i+j] = tmp[j] ^ iv[j];
        memcpy(iv, in + i, 8);
    }
}


/* =======================================================================
 * CRC32 — Ethernet/ZIP (reflected polynomial 0xEDB88320)
 * ===================================================================== */

static uint32_t crc32_table[256];
static bool     crc32_table_init = false;

static void crc32_init_table(void)
{
    if (crc32_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_init = true;
}

uint32_t desfire_crc32(const uint8_t *data, size_t len)
{
    crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) crc = crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

size_t desfire_crc32_append(uint8_t *buf, size_t len)
{
    uint32_t crc = desfire_crc32(buf, len);
    buf[len+0] = (uint8_t)(crc >>  0);
    buf[len+1] = (uint8_t)(crc >>  8);
    buf[len+2] = (uint8_t)(crc >> 16);
    buf[len+3] = (uint8_t)(crc >> 24);
    return len + 4;
}


/* =======================================================================
 * RNG — hardware nrf_drv_rng
 * ===================================================================== */

ret_code_t desfire_rng(uint8_t *buf, size_t len)
{
    uint8_t avail = 0;
    /* Spin until the hardware pool has enough bytes (returns void) */
    do {
        nrf_drv_rng_bytes_available(&avail);
    } while (avail < (uint8_t)len);
    return nrf_drv_rng_rand(buf, (uint8_t)len);
}
