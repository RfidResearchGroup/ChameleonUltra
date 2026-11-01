/* Exercise the actual Classic state machine with encrypted command frames.
 * Seed an authenticated session directly; RF activation and authentication
 * timing are outside this host test. */
#include "../application/src/rfid/nfctag/hf/nfc_mf1.c"

static nfc_tag_mf1_information_t tag;
static struct Crypto1State reader;
static uint8_t reply;
static unsigned replies;
static nfc_tag_14a_state_t rf_state;

void nfc_tag_14a_set_state(nfc_tag_14a_state_t state) {
    rf_state = state;
}

void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits) {
    assert(bits == 4);
    reply = data;
    replies++;
}

void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool append_crc) {
    assert(!"Unexpected byte response");
}

void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits) {
    assert(!"Unexpected bit response");
}

uint8_t nfc_tag_14a_wrap_frame(const uint8_t *data, size_t bits, const uint8_t *parity, uint8_t *frame) {
    assert(!"Unexpected wrapped response");
    return 0;
}

void nfc_tag_14a_append_crc(uint8_t *data, size_t length) {
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < length; i++) {
        uint8_t value = data[i] ^ (uint8_t)crc;
        value ^= value << 4;
        crc = (crc >> 8) ^ ((uint16_t)value << 8) ^ ((uint16_t)value << 3) ^ (value >> 4);
    }
    data[length] = crc;
    data[length + 1] = crc >> 8;
}

bool nfc_tag_14a_checks_crc(uint8_t *data, size_t length) {
    uint8_t copy[NFC_TAG_MF1_FRAME_SIZE];
    assert(length >= 2 && length <= sizeof(copy));
    memcpy(copy, data, length);
    nfc_tag_14a_append_crc(copy, length - 2);
    return memcmp(copy, data, length) == 0;
}

static void setup(bool gen2, nfc_tag_mf1_write_mode_t mode, uint8_t sector, uint8_t key) {
    memset(&tag, 0, sizeof(tag));
    memset(tag.memory[0], 0xA5, 16);
    tag.config.mode_gen2_magic = gen2;
    tag.config.mode_block_write = mode;
    m_tag_information = &tag;
    m_tag_type = TAG_TYPE_MIFARE_1024;
    AuthenticatedSector = sector;
    KeyInUse = key;
    m_tag_trailer_info = (void *)tag.memory[sectorToBlock(sector + 1) - 1];
    memcpy(m_tag_trailer_info->acs, (uint8_t[]){0xFF, 0x07, 0x80, 0x69}, 4);
    m_mf1_state = MF1_STATE_AUTHENTICATED;
    m_gen1a_state = GEN1A_STATE_DISABLE;
    crypto1_init(pcs, 0xFFFFFFFFFFFF);
    reader = *pcs;
    memset(m_data_block_buffer, 0, sizeof(m_data_block_buffer));
    rf_state = NFC_TAG_STATE_14A_ACTIVE;
    replies = 0;
}

static void send_frame(uint8_t *data, size_t length, bool expect_reply, uint8_t expected) {
    unsigned before = replies;
    nfc_tag_14a_append_crc(data, length);
    for (size_t i = 0; i < length + 2; i++) {
        data[i] ^= crypto1_byte(&reader, 0, 0);
    }
    nfc_tag_mf1_state_handler(data, (length + 2) * 8);
    assert(replies == before + expect_reply);
    if (expect_reply) {
        assert(mf_crypto1_encrypt4bit(&reader, reply) == expected);
    }
}

static void command(uint8_t op, uint8_t block, uint8_t expected) {
    uint8_t data[4] = {op, block};
    send_frame(data, 2, true, expected);
}

static void restore(uint8_t block) {
    command(CMD_RESTORE, block, ACK_VALUE);
    uint8_t operand[6] = {0};
    send_frame(operand, 4, false, 0);
    assert(m_mf1_state == MF1_STATE_AUTHENTICATED);
}

static void value_block(uint8_t block) {
    uint8_t *data = tag.memory[block];
    ValueToBlock(data, 123);
    data[12] = data[14] = block;
    data[13] = data[15] = (uint8_t)~block;
}

static void test_transfer(bool gen2, nfc_tag_mf1_write_mode_t mode, uint8_t target, uint8_t sector, uint8_t key) {
    setup(gen2, mode, sector, key);
    uint8_t source = sectorToBlock(sector) + 1;
    value_block(source);
    restore(source);
    uint8_t original[16];
    memcpy(original, tag.memory[target], 16);
    bool denied = (target == 0 && !gen2) || mode == NFC_TAG_MF1_WRITE_DENIED;
    command(CMD_TRANSFER, target, denied ? NAK_INVALID_OPERATION_TBIV : ACK_VALUE);
    if (denied || mode == NFC_TAG_MF1_WRITE_DECEIVE) {
        assert(memcmp(original, tag.memory[target], 16) == 0);
    } else {
        assert(memcmp(tag.memory[source], tag.memory[target], 16) == 0);
    }
    assert(memcmp(m_tag_trailer_info->acs, (uint8_t[]){0xFF, 0x07, 0x80, 0x69}, 4) == 0);
    assert(m_mf1_state == (denied ? MF1_STATE_UNAUTHENTICATED : MF1_STATE_AUTHENTICATED));
    assert(rf_state == (denied ? NFC_TAG_STATE_14A_IDLE : NFC_TAG_STATE_14A_ACTIVE));
}

static void test_write(bool gen2, uint8_t target) {
    setup(gen2, NFC_TAG_MF1_WRITE_NORMAL, 0, KEY_A);
    uint8_t original[16];
    memcpy(original, tag.memory[target], 16);
    bool denied = target == 0 && !gen2;
    command(CMD_WRITE, target, denied ? NAK_INVALID_OPERATION_TBIV : ACK_VALUE);
    if (denied) {
        assert(m_mf1_state == MF1_STATE_UNAUTHENTICATED);
        assert(memcmp(original, tag.memory[target], 16) == 0);
        return;
    }
    uint8_t data[18];
    memset(data, 0x5A, 16);
    send_frame(data, 16, true, ACK_VALUE);
    for (unsigned i = 0; i < 16; i++) {
        assert(tag.memory[target][i] == 0x5A);
    }
}

static void test_counter_update(void) {
    setup(false, NFC_TAG_MF1_WRITE_NORMAL, 1, KEY_A);
    value_block(4);
    command(CMD_INCREMENT, 4, ACK_VALUE);
    uint8_t operand[6] = {1};
    send_frame(operand, 4, false, 0);
    command(CMD_TRANSFER, 4, ACK_VALUE);
    uint32_t value;
    ValueFromBlock(&value, tag.memory[4]);
    assert(value == 124);
    assert(CheckValueIntegrity(tag.memory[4]));
    for (unsigned i = 0; i < 16; i++) {
        assert(tag.memory[0][i] == 0xA5);
    }
}

static void test_gen1a_override(void) {
    setup(false, NFC_TAG_MF1_WRITE_DENIED, 0, KEY_A);
    tag.config.mode_gen1a_magic = true;
    nfc_tag_mf1_reset_handler();
    uint8_t unlock = CMD_CHINESE_UNLOCK;
    nfc_tag_mf1_state_handler(&unlock, 7);
    assert(reply == ACK_VALUE);
    unlock = CMD_CHINESE_UNLOCK_RW;
    nfc_tag_mf1_state_handler(&unlock, 8);
    assert(reply == ACK_VALUE);
    assert(m_gen1a_state == GEN1A_STATE_UNLOCKED_RW_WAIT);
    uint8_t write[4] = {CMD_WRITE, 0};
    nfc_tag_14a_append_crc(write, 2);
    nfc_tag_mf1_state_handler(write, 32);
    assert(m_gen1a_state == GEN1A_STATE_WRITING);
    uint8_t data[18];
    memset(data, 0x5A, 16);
    nfc_tag_14a_append_crc(data, 16);
    nfc_tag_mf1_state_handler(data, 144);
    assert(reply == ACK_VALUE);
    assert(memcmp(tag.memory[0], data, 16) == 0);
}

int main(void) {
    for (unsigned gen2 = 0; gen2 < 2; gen2++) {
        for (unsigned mode = NFC_TAG_MF1_WRITE_NORMAL; mode <= NFC_TAG_MF1_WRITE_SHADOW_REQ; mode++) {
            for (unsigned key = KEY_A; key <= KEY_B; key++) {
                test_transfer(gen2, mode, 0, 0, key);
                test_transfer(gen2, mode, 2, 0, key);
                test_transfer(gen2, mode, 4, 1, key);
            }
        }
        test_write(gen2, 0);
        test_write(gen2, 1);
    }
    /* Sector, trailer and access restrictions continue to take precedence. */
    setup(true, NFC_TAG_MF1_WRITE_NORMAL, 0, KEY_A);
    command(CMD_TRANSFER, 4, NAK_INVALID_OPERATION_TBIV);
    setup(true, NFC_TAG_MF1_WRITE_NORMAL, 0, KEY_A);
    command(CMD_TRANSFER, 3, NAK_INVALID_OPERATION_TBIV);
    setup(true, NFC_TAG_MF1_WRITE_NORMAL, 0, KEY_A);
    memset(m_tag_trailer_info->acs, 0, 3); /* Invalid access-bit redundancy. */
    command(CMD_TRANSFER, 0, NAK_INVALID_OPERATION_TBIV);
    test_counter_update();
    test_gen1a_override();
    puts("Classic UID protection: 60 transfers, 4 writes, 3 permission checks, counter update and Gen1a override passed");
    return 0;
}
