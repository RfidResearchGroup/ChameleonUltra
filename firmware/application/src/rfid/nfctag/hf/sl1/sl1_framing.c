#include "sl1_framing.h"

#include <string.h>

static bool crc_a_valid(const uint8_t *data, size_t size) {
    if (size < 3U) return false;
    uint16_t crc = 0x6363U;
    for (size_t i = 0; i < size - 2U; ++i) {
        uint8_t value = data[i] ^ (uint8_t)crc;
        value ^= (uint8_t)(value << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)value << 8) ^
                         ((uint16_t)value << 3) ^ (value >> 4));
    }
    return data[size - 2U] == (uint8_t)crc &&
           data[size - 1U] == (uint8_t)(crc >> 8);
}

void cu_sl1_adapter_init(CuSl1Adapter *adapter) {
    if (!adapter) return;
    sl1_secure_zero(adapter, sizeof(*adapter));
    sl1_core_init(&adapter->core);
}

void cu_sl1_adapter_reset(CuSl1Adapter *adapter) {
    if (adapter) sl1_core_reset_session(&adapter->core);
}

void cu_sl1_adapter_disable(CuSl1Adapter *adapter) {
    if (!adapter) return;
    sl1_core_clear_secrets(&adapter->core);
    adapter->enabled = false;
}

bool cu_sl1_adapter_configure(CuSl1Adapter *adapter, const uint8_t *key, size_t key_size) {
    if (!adapter) return false;
    /* Copy before init also permits replacing a key from this same context. */
    uint8_t next_key[CU_SL1_KEY_SIZE] = {0};
    const bool valid = key && key_size == sizeof(next_key);
    if (valid) memcpy(next_key, key, sizeof(next_key));
    cu_sl1_adapter_init(adapter);
    if (valid) adapter->enabled = sl1_core_set_key(&adapter->core, next_key);
    sl1_secure_zero(next_key, sizeof(next_key));
    return adapter->enabled;
}

bool cu_sl1_adapter_enabled(const CuSl1Adapter *adapter) {
    return adapter && adapter->enabled && adapter->core.key_set;
}

bool cu_sl1_adapter_receive(
    CuSl1Adapter *adapter,
    const uint8_t *rx,
    size_t rx_size,
    size_t rx_bits,
    bool plaintext_allowed,
    const uint8_t *entropy,
    size_t entropy_size,
    CuSl1Reply *reply) {
    if (reply) memset(reply, 0, sizeof(*reply));
    if (!adapter) return false;
    if (!reply || !cu_sl1_adapter_enabled(adapter) || !plaintext_allowed ||
       !rx || !rx_size) {
        cu_sl1_adapter_reset(adapter);
        return false;
    }
    const uint8_t opcode = rx[0];
    if (opcode != 0x76U && opcode != 0x72U) {
        cu_sl1_adapter_reset(adapter);
        return false;
    }
    const size_t expected_size = opcode == 0x76U ? 5U : CU_SL1_MAX_RX_SIZE;
    /* Bound the byte count before multiplication or CRC traversal. */
    if (rx_size != expected_size || rx_bits != expected_size * 8U ||
       !crc_a_valid(rx, rx_size)) {
        cu_sl1_adapter_reset(adapter);
        return true;
    }
    if (opcode == 0x76U && (rx[1] != 0x04U || rx[2] != 0x90U)) {
        cu_sl1_adapter_reset(adapter);
        return false;
    }
    const uint8_t *fresh_entropy =
        entropy && entropy_size == CU_SL1_KEY_SIZE ? entropy : NULL;
    Sl1CoreReply core_reply;
    sl1_core_receive(&adapter->core, rx, rx_size - 2U, fresh_entropy, &core_reply);
    if (core_reply.bits == 136U && core_reply.size == CU_SL1_MAX_REPLY_SIZE) {
        memcpy(reply->data, core_reply.data, core_reply.size);
        reply->size = core_reply.size;
        reply->bits = core_reply.bits;
        reply->append_crc = true;
    } else if (core_reply.bits == 4U && core_reply.size == 1U) {
        reply->data[0] = core_reply.data[0] & 0x0fU;
        reply->size = 1U;
        reply->bits = 4U;
    } else if (core_reply.bits || core_reply.size) {
        /* This adapter never asks the core to process RATS/ISO4 or other modes. */
        cu_sl1_adapter_reset(adapter);
    }
    sl1_secure_zero(&core_reply, sizeof(core_reply));
    return true;
}
