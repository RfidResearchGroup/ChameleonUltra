#include "aes_cmac.h"
#include "dfc_crypto_internal.h"

#define BLOCK_SIZE 16

#define TAG "AESCMAC"

static const uint8_t zeroes[] =
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t Rb[] =
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87};

static void aes_cmac_wipe(void* value, size_t length) {
    volatile uint8_t* p = value;
    while(length--) *p++ = 0;
}

static void aes_cmac_padBlock(uint8_t* block, size_t len) {
    block[len] = 0x80;
}

static void aes_cmac_bitShiftLeft(uint8_t* input, uint8_t* output, size_t len) {
    size_t last = len - 1;
    for(size_t i = 0; i < last; i++) {
        output[i] = input[i] << 1;
        if(input[i + 1] & 0x80) {
            output[i] += 0x01;
        }
    }
    output[last] = input[last] << 1;
}

// x = a ^ b
static void aes_cmac_xor(const uint8_t* a, const uint8_t* b, uint8_t* x, size_t len) {
    for(size_t i = 0; i < len; i++) {
        x[i] = a[i] ^ b[i];
    }
}

static bool aes_cmac_generateSubkeys(
    DfcCryptoAesBlock* cipher, uint8_t* subkey1, uint8_t* subkey2) {
    uint8_t l[BLOCK_SIZE] = {0};
    if(!dfc_crypto_aes_block_encrypt(cipher, zeroes, l)) {
        aes_cmac_wipe(l, sizeof(l));
        return false;
    }

    aes_cmac_bitShiftLeft(l, subkey1, BLOCK_SIZE);
    if(l[0] & 0x80) {
        aes_cmac_xor(subkey1, Rb, subkey1, BLOCK_SIZE);
    }

    aes_cmac_bitShiftLeft(subkey1, subkey2, BLOCK_SIZE);
    if(subkey1[0] & 0x80) {
        aes_cmac_xor(subkey2, Rb, subkey2, BLOCK_SIZE);
    }

    aes_cmac_wipe(l, sizeof(l));
    return true;
}

bool aes_cmac_with_iv(
    uint8_t* key,
    size_t key_len,
    uint8_t* message,
    size_t message_len,
    uint8_t* iv,
    uint8_t* cmac) {
    AesCmacSegment segment = {.data = message, .length = message_len};
    return aes_cmac_segments_with_iv(key, key_len, &segment, 1, iv, cmac);
}

bool aes_cmac_segments_with_iv(
    uint8_t* key,
    size_t key_len,
    const AesCmacSegment* segments,
    size_t segment_count,
    uint8_t* iv,
    uint8_t* cmac) {
    uint8_t subkey1[BLOCK_SIZE] = {0};
    uint8_t subkey2[BLOCK_SIZE] = {0};
    uint8_t pending[BLOCK_SIZE] = {0};
    size_t pending_len = 0;
    size_t total_len = 0;
    uint8_t x[BLOCK_SIZE] = {0};
    uint8_t y[BLOCK_SIZE] = {0};
    DfcCryptoAesBlock cipher;
    bool success = false;

    if(key_len != BLOCK_SIZE || !iv || !cmac || (segment_count && !segments)) {
        if(cmac) memset(cmac, 0, BLOCK_SIZE);
        return false;
    }
    for(size_t i = 0; i < segment_count; i++) {
        if((segments[i].length && !segments[i].data) ||
           segments[i].length > SIZE_MAX - total_len) {
            memset(cmac, 0, BLOCK_SIZE);
            return false;
        }
        total_len += segments[i].length;
    }
    if(total_len > SIZE_MAX - (BLOCK_SIZE - 1) ||
       !dfc_crypto_aes_block_init(&cipher, key, key_len)) {
        memset(cmac, 0, BLOCK_SIZE);
        return false;
    }
    if(!aes_cmac_generateSubkeys(&cipher, subkey1, subkey2)) goto cleanup;
    memcpy(x, iv, sizeof(x));
    for(size_t segment_index = 0; segment_index < segment_count; segment_index++) {
        const uint8_t* data = segments[segment_index].data;
        size_t remaining = segments[segment_index].length;
        while(remaining) {
            if(pending_len == BLOCK_SIZE) {
                aes_cmac_xor(x, pending, y, BLOCK_SIZE);
                if(!dfc_crypto_aes_block_encrypt(&cipher, y, x)) goto cleanup;
                pending_len = 0;
                memset(pending, 0, sizeof(pending));
            }
            size_t take = BLOCK_SIZE - pending_len;
            if(take > remaining) take = remaining;
            memcpy(pending + pending_len, data, take);
            pending_len += take;
            data += take;
            remaining -= take;
        }
    }
    if(total_len && pending_len == BLOCK_SIZE) {
        aes_cmac_xor(pending, subkey1, pending, BLOCK_SIZE);
    } else {
        aes_cmac_padBlock(pending, pending_len);
        aes_cmac_xor(pending, subkey2, pending, BLOCK_SIZE);
    }
    aes_cmac_xor(x, pending, y, BLOCK_SIZE);
    success = dfc_crypto_aes_block_encrypt(&cipher, y, cmac);
cleanup:
    if(!success) memset(cmac, 0, BLOCK_SIZE);
    aes_cmac_wipe(subkey1, sizeof(subkey1));
    aes_cmac_wipe(subkey2, sizeof(subkey2));
    aes_cmac_wipe(pending, sizeof(pending));
    aes_cmac_wipe(x, sizeof(x));
    aes_cmac_wipe(y, sizeof(y));
    dfc_crypto_aes_block_clear(&cipher);
    return success;
}

bool aes_cmac(uint8_t* key, size_t key_len, uint8_t* message, size_t message_len, uint8_t* cmac) {
    uint8_t iv[BLOCK_SIZE] = {0};
    return aes_cmac_with_iv(key, key_len, message, message_len, iv, cmac);
}
