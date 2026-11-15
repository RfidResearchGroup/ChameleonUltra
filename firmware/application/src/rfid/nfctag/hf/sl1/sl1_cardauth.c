#include "sl1_cardauth.h"
#include "vendor/aes.h"
#include <string.h>

void sl1_secure_zero(void *data, size_t size) {
    if (!data) return;
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}

static void rotate_left(uint8_t out[16], const uint8_t in[16]) {
    memcpy(out, in + 1, 15);
    out[15] = in[0];
}

void sl1_core_reset_session(Sl1Core *core) {
    core->pending = false;
    sl1_secure_zero(core->rnd_b, sizeof(core->rnd_b));
    sl1_secure_zero(core->challenge, sizeof(core->challenge));
}

void sl1_core_clear_secrets(Sl1Core *core) {
    sl1_core_reset_session(core);
    sl1_secure_zero(core->aes_key, sizeof(core->aes_key));
    core->key_set = false;
}

void sl1_core_init(Sl1Core *core) {
    sl1_secure_zero(core, sizeof(*core));
}

bool sl1_core_set_key(Sl1Core *core, const uint8_t key[16]) {
    if (!core || !key) return false;
    sl1_core_reset_session(core);
    memcpy(core->aes_key, key, sizeof(core->aes_key));
    core->key_set = true;
    return true;
}

static void reject(Sl1Core *core, Sl1CoreReply *reply) {
    sl1_core_reset_session(core);
    ++core->rejected;
    reply->data[0] = 0x04;
    reply->size = 1;
    reply->bits = 4;
}

void sl1_core_receive(Sl1Core *core, const uint8_t *data, size_t size,
                      const uint8_t entropy[16], Sl1CoreReply *reply) {
    memset(reply, 0, sizeof(*reply));
    if (!data || !size) return;
    if (size == 3 && data[0] == 0x76 && data[1] == 0x04 && data[2] == 0x90) {
        sl1_core_reset_session(core);
        if (!entropy || !core->key_set) return;
        ++core->starts;
        memcpy(core->rnd_b, entropy, 16);
        memcpy(core->challenge, entropy, 16);
        struct AES_ctx aes;
        AES_init_ctx(&aes, core->aes_key);
        AES_ECB_encrypt(&aes, core->challenge);
        sl1_secure_zero(&aes, sizeof(aes));
        reply->data[0] = 0x90;
        memcpy(reply->data + 1, core->challenge, 16);
        reply->size = 17;
        reply->bits = 136;
        core->pending = true;
        return;
    }
    if (data[0] == 0x72) {
        if (!core->key_set || !core->pending || size != 33) {
            reject(core, reply);
            return;
        }
        core->pending = false;
        ++core->tokens;
        uint8_t plain[32], expected[16];
        const uint8_t iv[16] = {0};
        struct AES_ctx aes;
        memcpy(plain, data + 1, sizeof(plain));
        AES_init_ctx_iv(&aes, core->aes_key, iv);
        AES_CBC_decrypt_buffer(&aes, plain, sizeof(plain));
        rotate_left(expected, core->rnd_b);
        uint8_t diff = 0;
        for (size_t i = 0; i < 16; ++i) diff |= expected[i] ^ plain[i + 16];
        if (diff) {
            reject(core, reply);
        } else {
            ++core->verified;
            reply->data[0] = 0x90;
            rotate_left(reply->data + 1, plain);
            AES_ECB_encrypt(&aes, reply->data + 1);
            reply->size = 17;
            reply->bits = 136;
            sl1_core_reset_session(core);
        }
        sl1_secure_zero(plain, sizeof(plain));
        sl1_secure_zero(expected, sizeof(expected));
        sl1_secure_zero(&aes, sizeof(aes));
        return;
    }
    sl1_core_reset_session(core);
}
