#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool pending;
    bool key_set;
    uint8_t aes_key[16];
    uint8_t rnd_b[16];
    uint8_t challenge[16];
    uint32_t starts;
    uint32_t tokens;
    uint32_t verified;
    uint32_t rejected;
} Sl1Core;
typedef struct { uint8_t data[17]; size_t size; size_t bits; } Sl1CoreReply;
void sl1_secure_zero(void *data, size_t size);
void sl1_core_init(Sl1Core *core);
bool sl1_core_set_key(Sl1Core *core, const uint8_t key[16]);
void sl1_core_reset_session(Sl1Core *core);
void sl1_core_clear_secrets(Sl1Core *core);
/* Inputs exclude CRC. A fresh 16-byte nonce is mandatory for each start. */
void sl1_core_receive(Sl1Core *core, const uint8_t *data, size_t size,
                      const uint8_t entropy[16], Sl1CoreReply *reply);
