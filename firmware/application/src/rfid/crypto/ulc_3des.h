#ifndef ULC_3DES_H
#define ULC_3DES_H

#include <stdint.h>
#include <stddef.h>

void ulc_tdes_enc_cbc(uint8_t *out, const uint8_t *in, size_t len, const uint8_t key[16], uint8_t iv[8]);
void ulc_tdes_dec_cbc(uint8_t *out, const uint8_t *in, size_t len, const uint8_t key[16], uint8_t iv[8]);
void ulc_rol8(uint8_t *data);
void ulc_3des_init(void);

#endif // ULC_3DES_H
