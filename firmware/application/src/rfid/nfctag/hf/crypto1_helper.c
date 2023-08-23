#include "crypto1_helper.h"

// crypto1 helpers
void mf_crypto1_decryptEx(struct Crypto1State *pcs, uint8_t *data_in, int len, uint8_t *data_out) {
    if (len != 1) {
        for (int i = 0; i < len; i++)
            data_out[i] = crypto1_byte(pcs, 0x00, 0) ^ data_in[i];
    } else {
        uint8_t bt = 0;
        bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data_in[0], 0)) << 0;
        bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data_in[0], 1)) << 1;
        bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data_in[0], 2)) << 2;
        bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data_in[0], 3)) << 3;
        data_out[0] = bt;
    }
    return;
}

void mf_crypto1_decrypt(struct Crypto1State *pcs, uint8_t *data, int len) {
    mf_crypto1_decryptEx(pcs, data, len, data);
}

void mf_crypto1_encryptEx(struct Crypto1State *pcs, uint8_t *data_in, uint8_t *keystream, uint8_t *data_out, uint16_t len, uint8_t *par) {
    int i;
    for (i = 0; i < len; i++) {
        uint8_t bt = data_in[i];
        // Encrypted bytes
        data_out[i] = crypto1_byte(pcs, keystream ? keystream[i] : 0x00, 0) ^ data_in[i];
        // Generate strange school inspection
        par[i] = filter(pcs->odd) ^ oddparity8(bt);
    }
}

void mf_crypto1_encrypt(struct Crypto1State *pcs, uint8_t *data, uint16_t len, uint8_t *par) {
    mf_crypto1_encryptEx(pcs, data, NULL, data, len, par);
}

uint8_t mf_crypto1_encrypt4bit(struct Crypto1State *pcs, uint8_t data) {
    uint8_t bt = 0;
    bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data, 0)) << 0;
    bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data, 1)) << 1;
    bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data, 2)) << 2;
    bt |= (crypto1_bit(pcs, 0, 0) ^ BIT(data, 3)) << 3;
    return bt;
}
