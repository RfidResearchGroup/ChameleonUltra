#pragma once
/**
 * desfire_crypto.h — DESFire crypto foundation
 *
 * Hardware (CC310 via nrf_crypto):
 *   AES-128-ECB, AES-128-CBC, AES-128-CMAC, AES session key derivation
 *
 * Software (compact Feistel DES):
 *   DES-ECB, 2K3DES-CBC  (legacy DESFire / DES-only cards)
 *
 * Utilities:
 *   CRC32 (Ethernet / ZIP polynomial — used by DESFire for data integrity)
 *   RNG   (hardware via nrf_drv_rng)
 *
 * Requires nrf_crypto_init() called at boot (done in app_main.c).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sdk_errors.h"

/* -----------------------------------------------------------------------
 * AES — hardware CC310
 * --------------------------------------------------------------------- */

/** AES-128-ECB encrypt single 16-byte block (DESFire auth challenge). */
ret_code_t desfire_aes_ecb_encrypt(const uint8_t key[16],
                                    const uint8_t in[16],
                                    uint8_t       out[16]);

/** AES-128-ECB decrypt single 16-byte block. */
ret_code_t desfire_aes_ecb_decrypt(const uint8_t key[16],
                                    const uint8_t in[16],
                                    uint8_t       out[16]);

/** AES-128-CBC encrypt. IV updated in-place to last ciphertext block.
 *  len must be a multiple of 16. */
ret_code_t desfire_aes_cbc_encrypt(const uint8_t key[16],
                                    uint8_t       iv[16],
                                    const uint8_t *in,
                                    uint8_t       *out,
                                    size_t         len);

/** AES-128-CBC decrypt. IV updated in-place. */
ret_code_t desfire_aes_cbc_decrypt(const uint8_t key[16],
                                    uint8_t       iv[16],
                                    const uint8_t *in,
                                    uint8_t       *out,
                                    size_t         len);

/** AES-128-CMAC over arbitrary-length message. mac_out = 16 bytes. */
ret_code_t desfire_aes_cmac(const uint8_t *key,
                              const uint8_t *msg,
                              size_t         len,
                              uint8_t        mac_out[16]);

/** DESFire EV1/EV2/EV3 AES session key derivation (NXP AN10922 §4.1).
 *  SK = AES-CMAC(key, SV) where SV is built from RndA and RndB. */
ret_code_t desfire_derive_session_key(const uint8_t key[16],
                                       const uint8_t rnd_a[16],
                                       const uint8_t rnd_b[16],
                                       uint8_t       session_key_out[16]);

/* -----------------------------------------------------------------------
 * DES / 2K3DES — software (CC310 has no DES support)
 * --------------------------------------------------------------------- */

/** DES-ECB encrypt/decrypt single 8-byte block.
 *  key = 8 bytes, in/out = 8 bytes. */
void desfire_des_ecb(const uint8_t key[8],
                     const uint8_t in[8],
                     uint8_t       out[8],
                     bool          encrypt);

/** 2K3DES-CBC encrypt.
 *  key = 16 bytes (K1||K2, expanded to K1||K2||K1 internally).
 *  iv  = 8 bytes, updated in-place.
 *  len must be a multiple of 8. */
void desfire_2k3des_cbc_encrypt(const uint8_t key[16],
                                 uint8_t       iv[8],
                                 const uint8_t *in,
                                 uint8_t       *out,
                                 size_t         len);

/** 2K3DES-CBC decrypt. IV updated in-place. */
void desfire_2k3des_cbc_decrypt(const uint8_t key[16],
                                 uint8_t       iv[8],
                                 const uint8_t *in,
                                 uint8_t       *out,
                                 size_t         len);

/* -----------------------------------------------------------------------
 * CRC32 — Ethernet/ZIP polynomial (used by DESFire data integrity)
 * --------------------------------------------------------------------- */

/** Compute CRC32 over data. Initial value 0xFFFFFFFF, output inverted.
 *  DESFire appends the 4-byte result LSB-first before encryption. */
uint32_t desfire_crc32(const uint8_t *data, size_t len);

/** Append 4-byte CRC32 to buf (buf must have len+4 capacity).
 *  Returns total length including CRC. */
size_t desfire_crc32_append(uint8_t *buf, size_t len);

/* -----------------------------------------------------------------------
 * RNG — hardware nrf_drv_rng
 * --------------------------------------------------------------------- */

/** Fill buf with len cryptographically random bytes.
 *  Blocks briefly until the hardware pool has enough entropy. */
ret_code_t desfire_rng(uint8_t *buf, size_t len);
