#ifndef CRYPTO1_HELPER_H
#define CRYPTO1_HELPER_H

#include "mf1_crapto1.h"
#include "parity.h"

void mf_crypto1_decryptEx(struct Crypto1State *pcs, uint8_t *data_in, int len, uint8_t *data_out);
void mf_crypto1_decrypt(struct Crypto1State *pcs, uint8_t *data, int len);
void mf_crypto1_encryptEx(struct Crypto1State *pcs, uint8_t *data_in, uint8_t *keystream, uint8_t *data_out,
                          uint16_t len, uint8_t *par);
void mf_crypto1_encrypt(struct Crypto1State *pcs, uint8_t *data, uint16_t len, uint8_t *par);
uint8_t mf_crypto1_encrypt4bit(struct Crypto1State *pcs, uint8_t data);

#endif
