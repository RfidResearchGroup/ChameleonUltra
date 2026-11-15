#include "sl1_profile.h"
#include "nfc_mf1.h"
#include "sl1/sl1_cardauth.h"
#include "fds_ids.h"
#include "fds_util.h"
#include "tag_persistence.h"
#include "app_status.h"
#include "rfid_main.h"
#include "nrf_crypto_hash.h"
#include <stddef.h>
#include <string.h>

#define PROFILE_RECORD_SIZE 64U
#define PROFILE_PREFIX_SIZE 36U
#define PROFILE_VERSION 1U

static struct {
    bool present;
    uint8_t slot;
    uint8_t scenario;
    uint8_t enabled;
    uint16_t type;
    uint16_t key_number;
    uint8_t key[16];
    uint8_t coverage[32];
    uint16_t received;
    nfc_tag_mf1_information_t image;
} m_stage;
static nfc_tag_mf1_information_t m_scratch;
static uint8_t m_last_status[8];
static uint8_t m_last_stage[8];
static bool m_sense_pending;

static uint16_t read16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static void write16(uint8_t *p, uint16_t value) { p[0] = value >> 8; p[1] = value; }
static uint32_t read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write32(uint8_t *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static uint32_t record_crc(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0);
    }
    return ~crc;
}
static unsigned blocks(uint16_t type) {
    static const uint16_t counts[] = {20, 64, 128, 256};
    return type >= 1000 && type <= 1003 ? counts[type - 1000] : 0;
}
static uint16_t image_length(uint16_t type) {
    return offsetof(nfc_tag_mf1_information_t, memory) + blocks(type) * 16U;
}
static bool request(const uint8_t *data, uint16_t length, uint16_t expected) {
    return data && length == expected && data[0] == PROFILE_VERSION && data[1] < TAG_MAX_SLOT_NUM;
}
static bool idle_rf(void) {
    return get_device_mode() != DEVICE_MODE_READER && !nfc_tag_mf1_sl1_field_present();
}
static uint8_t result(uint8_t slot, uint8_t status) {
    m_last_status[slot] = status;
    return status;
}
static bool ats_valid(const nfc_14a_ats_t *ats) {
    return ats->length <= 254 && (!ats->length ||
           (ats->length >= 2 && ats->data[0] == ats->length));
}
static bool access_valid(const nfc_tag_mf1_information_t *image, uint16_t type) {
    unsigned count = blocks(type);
    for (unsigned block = 3; block < count; block += block < 127 ? 4 : 16) {
        const uint8_t *a = image->memory[block] + 6;
        if (((a[0] & 15U) ^ (a[1] >> 4)) != 15U ||
                ((a[0] >> 4) ^ (a[2] & 15U)) != 15U ||
                ((a[1] & 15U) ^ (a[2] >> 4)) != 15U) return false;
    }
    return true;
}

/* Canonical binding excludes compiler padding and local configuration flags. */
static bool digest(uint16_t type, const nfc_tag_mf1_information_t *image, uint8_t out[32]) {
    if (!blocks(type) || (image->res_coll.size != 4 && image->res_coll.size != 7) ||
            !ats_valid(&image->res_coll.ats)) return false;
    uint8_t prefix[3]; write16(prefix, type); prefix[2] = image->res_coll.size;
    const uint8_t *uid = image->res_coll.uid;
    const uint8_t *atqa = image->res_coll.atqa;
    const uint8_t *sak = image->res_coll.sak;
    if (image->config.use_mf1_coll_res && prefix[2] == 4) {
        uid = image->memory[0]; sak = image->memory[0] + 5; atqa = image->memory[0] + 6;
    }
    nrf_crypto_hash_context_t context;
    ret_code_t rc = nrf_crypto_hash_init(&context, &g_nrf_crypto_hash_sha256_info);
    if (rc != NRF_SUCCESS) return false;
    const uint8_t *parts[] = {prefix, uid, atqa, sak, &image->res_coll.ats.length,
                              image->res_coll.ats.data, &image->memory[0][0]};
    const size_t sizes[] = {3, prefix[2], 2, 1, 1, image->res_coll.ats.length, blocks(type) * 16U};
    for (unsigned i = 0; i < 7 && rc == NRF_SUCCESS; ++i)
        if (sizes[i]) rc = nrf_crypto_hash_update(&context, parts[i], sizes[i]);
    size_t length = 32;
    if (rc == NRF_SUCCESS) rc = nrf_crypto_hash_finalize(&context, out, &length);
    sl1_secure_zero(&context, sizeof(context));
    return rc == NRF_SUCCESS && length == 32;
}

static void record_init(uint8_t record[64], uint8_t slot) {
    memset(record, 0, 64);
    memcpy(record, "SL1P", 4); record[4] = 1; record[5] = slot;
}
static bool record_readback(uint8_t slot, uint16_t key, const uint8_t expected[64]);
/* Revocation/full replacement canonicalizes all prior records. A toggle of a
 * proven unique credential uses the SDK atomic update to retain that key through
 * failure. Neither path acknowledges an unverified or ambiguous replacement. */
static uint8_t record_write(uint8_t slot, uint8_t record[64], bool canonicalize) {
    uint16_t id = FDS_SL1_PROFILE_FILE_ID_BASE + slot;
    int count = fds_record_count_sync(id, FDS_SL1_PROFILE_RECORD_KEY);
    if (count < 0) return STATUS_FLASH_WRITE_FAIL;
    if (canonicalize) {
        if (count > 0 && fds_delete_sync(id, FDS_SL1_PROFILE_RECORD_KEY) <= 0) return STATUS_FLASH_WRITE_FAIL;
        if (fds_record_lookup_sync(id, FDS_SL1_PROFILE_RECORD_KEY) != FDS_LOOKUP_ABSENT)
            return STATUS_FLASH_WRITE_FAIL;
    } else if (count != 1) {
        return STATUS_FLASH_WRITE_FAIL;
    }
    write32(record + 60, record_crc(record, 60));
    if (!fds_write_sync(id, FDS_SL1_PROFILE_RECORD_KEY, 64, record)) return STATUS_FLASH_WRITE_FAIL;
    return record_readback(slot, FDS_SL1_PROFILE_RECORD_KEY, record) ? STATUS_SUCCESS : STATUS_FLASH_READ_FAIL;
}
static bool record_valid(uint8_t slot, const uint8_t record[64]) {
    if (memcmp(record, "SL1P", 4) || record[4] != 1 || record[5] != slot || record[6] > 1 ||
            read32(record + 60) != record_crc(record, 60)) return false;
    if (!record[7]) {
        for (unsigned i = 6; i < 60; ++i) if (record[i]) return false;
        return true;
    }
    return record[7] == 1 && blocks(read16(record + 8)) && read16(record + 10) == 0x9004;
}
enum {PROFILE_UNKNOWN = -2, PROFILE_CORRUPT = -1, PROFILE_ABSENT = 0, PROFILE_VALID = 1};
static int record_read(uint8_t slot, uint8_t record[64]) {
    uint16_t id = FDS_SL1_PROFILE_FILE_ID_BASE + slot;
    int count = fds_record_count_sync(id, FDS_SL1_PROFILE_RECORD_KEY);
    if (count < 0) return PROFILE_UNKNOWN;
    if (!count) return PROFILE_ABSENT;
    if (count != 1) return PROFILE_CORRUPT;
    uint16_t length = 64;
    if (!fds_read_sync(id, FDS_SL1_PROFILE_RECORD_KEY, &length, record)) {
        count = fds_record_count_sync(id, FDS_SL1_PROFILE_RECORD_KEY);
        if (count < 0) return PROFILE_UNKNOWN;
        return !count ? PROFILE_ABSENT : PROFILE_CORRUPT;
    }
    return length == 64 && record_valid(slot, record) ? PROFILE_VALID : PROFILE_CORRUPT;
}
bool sl1_profile_hf_incomplete(uint8_t slot) {
    return slot >= TAG_MAX_SLOT_NUM || fds_record_lookup_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot,
                                                               FDS_SL1_TRANSACTION_RECORD_KEY) != FDS_LOOKUP_ABSENT;
}

static bool image_read(uint8_t slot, uint16_t type) {
    uint16_t length = sizeof(m_scratch);
    /* FDS reports complete words, including padding after the image. */
    uint16_t stored_length = (image_length(type) + 3U) & ~3U;
    sl1_secure_zero(&m_scratch, sizeof(m_scratch));
    return blocks(type) && fds_read_sync(FDS_SLOT_TAG_DUMP_FILE_ID_BASE + slot,
        TAG_SENSE_HF, &length, (uint8_t *)&m_scratch) && length == stored_length;
}
static bool bound(uint8_t slot, const uint8_t record[64]) {
    tag_slot_specific_type_t types;
    tag_emulation_get_specific_types_by_slot(slot, &types);
    uint8_t actual[32];
    bool valid = record[7] == 1 && types.tag_hf == read16(record + 8) &&
                 image_read(slot, types.tag_hf) && digest(types.tag_hf, &m_scratch, actual) &&
                 !memcmp(actual, record + 28, 32) && access_valid(&m_scratch, types.tag_hf);
    sl1_secure_zero(actual, sizeof(actual));
    return valid;
}
/* Readback is an exact ownership/coherence check, not just a callback result.
 * The key-1 buffer may contain a credential and is wiped on every exit. */
static bool record_readback(uint8_t slot, uint16_t key, const uint8_t expected[64]) {
    uint32_t aligned[16]; uint8_t *actual = (uint8_t *)aligned;
    uint16_t length = 64;
    bool verified = fds_record_lookup_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot, key) == FDS_LOOKUP_PRESENT &&
                    fds_read_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot, key, &length, actual) &&
                    length == 64 && !memcmp(actual, expected, 64) &&
                    read32(actual + 60) == record_crc(actual, 60);
    if (verified && key == FDS_SL1_PROFILE_RECORD_KEY)
        verified = fds_record_count_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot, key) == 1 &&
                   record_valid(slot, actual) && (!actual[7] || bound(slot, actual));
    sl1_secure_zero(actual, 64);
    sl1_secure_zero(&m_scratch, sizeof(m_scratch));
    return verified;
}

/* Control operations and normal activation share the same live-image test. */
static bool live_bound(uint8_t slot, const uint8_t record[64]) {
    tag_slot_specific_type_t types;
    tag_emulation_get_specific_types_by_slot(slot, &types);
    uint16_t type = read16(record + 8);
    if (tag_emulation_profile_hf_quarantined(slot) || record[7] != 1 || !blocks(type) || types.tag_hf != type) return false;
    tag_data_buffer_t *live = get_buffer_by_tag_type(type);
    const nfc_tag_mf1_information_t *image = live ? (const nfc_tag_mf1_information_t *)live->buffer : NULL;
    uint8_t actual[32];
    bool matches = image && live->length >= image_length(type) && image->res_coll.ats.length >= 2 &&
                   access_valid(image, type) && digest(type, image, actual) &&
                   !memcmp(actual, record + 28, sizeof(actual));
    sl1_secure_zero(actual, sizeof(actual));
    return matches;
}

static bool control_can_run(uint8_t slot) {
    if (tag_emulation_profile_hf_quarantined(slot) || slot != tag_emulation_get_slot() || get_device_mode() != DEVICE_MODE_TAG ||
            !is_slot_enabled(slot, TAG_SENSE_HF)) return false;
    tag_slot_specific_type_t types;
    tag_emulation_get_specific_types_by_slot(slot, &types);
    return blocks(types.tag_hf) != 0;
}

static void control_resume_ordinary(uint8_t slot, bool run_intent) {
    if (run_intent && control_can_run(slot) && !nfc_tag_14a_sensing_enabled())
        nfc_tag_14a_sense_switch(true);
}

static void cancel_stage(void) { sl1_secure_zero(&m_stage, sizeof(m_stage)); }
static void stop_active(uint8_t slot) {
    if (slot == tag_emulation_get_slot()) {
        m_sense_pending = false;
        nfc_tag_14a_sense_switch(false);
    }
}

bool sl1_profile_invalidate(uint8_t slot) {
    if (slot >= 8 || sl1_profile_hf_incomplete(slot)) return false;
    uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned;
    int state = record_read(slot, record);
    if (state == PROFILE_UNKNOWN) {
        if (slot == tag_emulation_get_slot() && idle_rf()) {
            bool run_intent = m_sense_pending;
            if (nfc_tag_mf1_sl1_is_enabled()) (void)nfc_tag_mf1_sl1_disable();
            m_sense_pending = false;
            control_resume_ordinary(slot, run_intent);
        }
        sl1_secure_zero(record, 64);
        return false;
    }
    /* Ordinary legacy slots need no persistent invalidation or RF lifecycle
     * change. This also keeps repeated command/owner guards idempotent. */
    if (state == 0 || (state == 1 && !record[7])) {
        if (m_stage.present && m_stage.slot == slot) cancel_stage();
        sl1_secure_zero(record, 64);
        return true;
    }
    if (!idle_rf()) {
        sl1_secure_zero(record, 64);
        return false;
    }
    bool resume_ordinary = slot == tag_emulation_get_slot() && m_sense_pending;
    if (slot == tag_emulation_get_slot()) {
        if (!nfc_tag_mf1_sl1_disable()) {
            sl1_secure_zero(record, 64);
            return false;
        }
        m_sense_pending = false;
    }
    if (m_stage.present && m_stage.slot == slot) cancel_stage();
    /* Disable only the extension: native Classic sensing remains available. */
    record_init(record, slot);
    bool saved = record_write(slot, record, true) == STATUS_SUCCESS;
    sl1_secure_zero(record, 64);
    if (resume_ordinary && !tag_emulation_profile_hf_quarantined(slot) && get_device_mode() == DEVICE_MODE_TAG &&
            is_slot_enabled(slot, TAG_SENSE_HF)) {
        tag_slot_specific_type_t types;
        tag_emulation_get_specific_types_by_slot(slot, &types);
        if (types.tag_hf >= TAG_TYPE_MIFARE_Mini && types.tag_hf <= TAG_TYPE_MIFARE_4096)
            nfc_tag_14a_sense_switch(true);
    }
    return saved;
}

uint8_t sl1_profile_begin(const uint8_t *data, uint16_t length) {
    if (!data || length < 36 || data[0] != 1 || data[1] >= 8) return STATUS_PAR_ERR;
    uint16_t type = read16(data + 2);
    uint8_t uid_length = data[4], ats_length = data[15], scenario = data[16];
    if (!blocks(type) || (uid_length != 4 && uid_length != 7) || length != 36U + ats_length ||
            ats_length > 254 || scenario > 1 || data[17] > 1) return STATUS_PAR_ERR;
    for (unsigned i = uid_length; i < 7; ++i) if (data[5 + i]) return STATUS_PAR_ERR;
    if ((ats_length && (ats_length < 2 || data[36] != ats_length)) ||
            (scenario == 1 && (read16(data + 18) != 0x9004 || (data[17] && ats_length < 2))))
        return STATUS_PAR_ERR;
    if (!scenario) for (unsigned i = 17; i < 36; ++i) if (data[i]) return STATUS_PAR_ERR;
    cancel_stage();
    m_stage.present = true; m_stage.slot = data[1]; m_stage.type = type;
    m_stage.scenario = scenario; m_stage.enabled = data[17]; m_stage.key_number = read16(data + 18);
    memcpy(m_stage.key, data + 20, 16);
    m_stage.image.res_coll.size = uid_length;
    memcpy(m_stage.image.res_coll.uid, data + 5, uid_length);
    memcpy(m_stage.image.res_coll.atqa, data + 12, 2);
    m_stage.image.res_coll.sak[0] = data[14];
    m_stage.image.res_coll.ats.length = ats_length;
    memcpy(m_stage.image.res_coll.ats.data, data + 36, ats_length);
    m_stage.image.config.mode_block_write = NFC_TAG_MF1_WRITE_NORMAL;
    m_stage.image.config.prng_type = 1;
    m_last_stage[data[1]] = 1;
    return result(data[1], STATUS_SUCCESS);
}

uint8_t sl1_profile_write(const uint8_t *data, uint16_t length) {
    if (!data || length < 20 || data[0] != 1 || data[1] >= 8 || (length - 4U) % 16U ||
            length > 132) return STATUS_PAR_ERR;
    if (!m_stage.present || m_stage.slot != data[1]) return STATUS_CMD_ERR;
    unsigned first = read16(data + 2), count = (length - 4U) / 16U;
    if (first + count > blocks(m_stage.type)) return STATUS_PAR_ERR;
    memcpy(m_stage.image.memory[first], data + 4, count * 16U);
    for (unsigned i = first; i < first + count; ++i) {
        uint8_t bit = 1U << (i % 8);
        if (!(m_stage.coverage[i / 8] & bit)) { m_stage.coverage[i / 8] |= bit; ++m_stage.received; }
    }
    return result(data[1], STATUS_SUCCESS);
}

uint8_t sl1_profile_cancel(const uint8_t *data, uint16_t length) {
    if (!request(data, length, 2)) return STATUS_PAR_ERR;
    if (m_stage.present && m_stage.slot != data[1]) return STATUS_CMD_ERR;
    cancel_stage(); m_last_stage[data[1]] = 0;
    return result(data[1], STATUS_SUCCESS);
}

uint8_t sl1_profile_save(const uint8_t *data, uint16_t length) {
    if (!request(data, length, 2)) return STATUS_PAR_ERR;
    uint8_t slot = data[1];
    if (!m_stage.present || m_stage.slot != slot || !idle_rf()) return result(slot, STATUS_CMD_ERR);
    if (m_stage.received != blocks(m_stage.type) || !access_valid(&m_stage.image, m_stage.type))
        return result(slot, STATUS_PAR_ERR);
    uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned;
    record_init(record, slot);
    if (m_stage.scenario) {
        record[6] = m_stage.enabled; record[7] = 1;
        write16(record + 8, m_stage.type); write16(record + 10, m_stage.key_number);
        memcpy(record + 12, m_stage.key, 16);
        if (!digest(m_stage.type, &m_stage.image, record + 28)) {
            sl1_secure_zero(record, 64); return result(slot, STATUS_CMD_ERR);
        }
    }
    uint32_t marker_words[16]; uint8_t *marker = (uint8_t *)marker_words;
    record_init(marker, slot); memcpy(marker, "SL1T", 4); write16(marker + 8, m_stage.type);
    if (!digest(m_stage.type, &m_stage.image, marker + 28)) {
        sl1_secure_zero(marker, 64); sl1_secure_zero(record, 64);
        return result(slot, STATUS_CMD_ERR);
    }
    stop_active(slot);
    write32(marker + 60, record_crc(marker, 60));
    m_last_stage[slot] = 2;
    bool ok = fds_write_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot,
                             FDS_SL1_TRANSACTION_RECORD_KEY, 64, marker);
    if (!ok) {
        sl1_secure_zero(marker, 64); sl1_secure_zero(record, 64);
        return result(slot, STATUS_FLASH_WRITE_FAIL);
    }
    bool marked = record_readback(slot, FDS_SL1_TRANSACTION_RECORD_KEY, marker);
    sl1_secure_zero(marker, 64);
    if (!marked) { sl1_secure_zero(record, 64); return result(slot, STATUS_FLASH_READ_FAIL); }
    if (ok) {
        m_last_stage[slot] = 3;
        ok = fds_write_sync(FDS_SLOT_TAG_DUMP_FILE_ID_BASE + slot, TAG_SENSE_HF,
                            image_length(m_stage.type), &m_stage.image);
    }
    if (ok) {
        m_last_stage[slot] = 4;
        ok = tag_emulation_profile_save_config(slot, m_stage.type);
    }
    if (!ok) { sl1_secure_zero(record, 64); return result(slot, STATUS_FLASH_WRITE_FAIL); }
    m_last_stage[slot] = 6;
    uint8_t expected_digest[32], actual_digest[32];
    bool coherent = digest(m_stage.type, &m_stage.image, expected_digest) && image_read(slot, m_stage.type) &&
                    access_valid(&m_scratch, m_stage.type) && digest(m_stage.type, &m_scratch, actual_digest) &&
                    !memcmp(expected_digest, actual_digest, 32);
    sl1_secure_zero(&m_scratch, sizeof(m_scratch));
    if (coherent && slot == tag_emulation_get_slot()) {
        coherent = tag_emulation_profile_reload_active();
        tag_data_buffer_t *live = get_buffer_by_tag_type(m_stage.type);
        coherent = coherent && live && live->buffer && live->length >= image_length(m_stage.type) &&
                   digest(m_stage.type, (const nfc_tag_mf1_information_t *)live->buffer, actual_digest) &&
                   !memcmp(expected_digest, actual_digest, 32);
    }
    sl1_secure_zero(expected_digest, 32); sl1_secure_zero(actual_digest, 32);
    if (!coherent) { sl1_secure_zero(record, 64); return result(slot, STATUS_FLASH_READ_FAIL); }
    m_last_stage[slot] = 5;
    uint8_t final_status = record_write(slot, record, true);
    sl1_secure_zero(record, 64);
    if (final_status != STATUS_SUCCESS) return result(slot, final_status);
    if (fds_delete_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot, FDS_SL1_TRANSACTION_RECORD_KEY) <= 0 ||
            fds_record_lookup_sync(FDS_SL1_PROFILE_FILE_ID_BASE + slot,
                                   FDS_SL1_TRANSACTION_RECORD_KEY) != FDS_LOOKUP_ABSENT)
        return result(slot, STATUS_FLASH_WRITE_FAIL);
    bool runtime_expected = m_stage.scenario && m_stage.enabled;
    cancel_stage();
    if (slot == tag_emulation_get_slot()) {
        m_last_stage[slot] = 6;
        sl1_profile_load_active();
        if (runtime_expected && !nfc_tag_mf1_sl1_is_enabled()) return result(slot, STATUS_CMD_ERR);
        tag_emulation_sense_switch(TAG_SENSE_HF, true);
    }
    m_last_stage[slot] = 7;
    return result(slot, STATUS_SUCCESS);
}

uint8_t sl1_profile_clear(const uint8_t *data, uint16_t length) {
    if (!request(data, length, 2)) return STATUS_PAR_ERR;
    if (!idle_rf() || sl1_profile_hf_incomplete(data[1])) return result(data[1], STATUS_CMD_ERR);
    m_last_stage[data[1]] = 2;
    bool ok = sl1_profile_invalidate(data[1]);
    if (ok) m_last_stage[data[1]] = 7;
    return result(data[1], ok ? STATUS_SUCCESS : STATUS_FLASH_WRITE_FAIL);
}
uint8_t sl1_profile_set_enabled(const uint8_t *data, uint16_t length) {
    if (!request(data, length, 3) || data[2] > 1) return STATUS_PAR_ERR;
    uint8_t slot = data[1];
    if (!idle_rf() || m_stage.present || sl1_profile_hf_incomplete(slot)) return result(slot, STATUS_CMD_ERR);
    bool active = slot == tag_emulation_get_slot();
    bool run_intent = active && (m_sense_pending || nfc_tag_14a_sensing_enabled());
    if (active) {
        if (nfc_tag_mf1_sl1_is_enabled() && !nfc_tag_mf1_sl1_disable())
            return result(slot, STATUS_CMD_ERR);
        m_sense_pending = false;
    }
    uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned;
    if (record_read(slot, record) != 1 || !bound(slot, record) ||
            (data[2] && (m_scratch.res_coll.ats.length < 2 || (active && !live_bound(slot, record))))) {
        sl1_secure_zero(record, 64);
        control_resume_ordinary(slot, run_intent);
        return result(slot, STATUS_CMD_ERR);
    }
    record[6] = data[2];
    uint32_t disabled[16]; memcpy(disabled, record, sizeof(disabled));
    ((uint8_t *)disabled)[6] = 0;
    m_last_stage[slot] = 2;
    uint8_t commit_status = record_write(slot, (uint8_t *)disabled, false);
    sl1_secure_zero(disabled, sizeof(disabled));
    if (commit_status == STATUS_SUCCESS) { m_last_stage[slot] = 5; commit_status = record_write(slot, record, false); }
    sl1_secure_zero(record, 64);
    if (commit_status != STATUS_SUCCESS) {
        control_resume_ordinary(slot, run_intent);
        return result(slot, commit_status);
    }
    if (data[2] && run_intent && control_can_run(slot)) {
        m_last_stage[slot] = 6;
        // Rebind the same RAM image after sensing teardown. CRC and LF stay
        // untouched; only explicit profile SAVE may reload a persisted image.
        stop_active(slot);
        tag_slot_specific_type_t types;
        tag_emulation_get_specific_types_by_slot(slot, &types);
        bool loaded = tag_emulation_load_by_buffer(types.tag_hf, false);
        if (loaded) sl1_profile_load_active();
        if (!loaded || !nfc_tag_mf1_sl1_is_enabled()) {
            control_resume_ordinary(slot, run_intent);
            return result(slot, STATUS_CMD_ERR);
        }
        sl1_profile_sense_request(true);
    } else {
        control_resume_ordinary(slot, run_intent);
    }
    m_last_stage[slot] = 7;
    return result(slot, STATUS_SUCCESS);
}

void sl1_profile_load_active(void) {
    m_sense_pending = false;
    if (!idle_rf() || !nfc_tag_mf1_sl1_disable()) return;
    uint8_t slot = tag_emulation_get_slot();
    uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned;
    if (get_device_mode() != DEVICE_MODE_READER && record_read(slot, record) == 1 && record[6] &&
            is_slot_enabled(slot, TAG_SENSE_HF) && live_bound(slot, record)) {
        nfc_tag_14a_coll_res_reference_t *coll = get_mifare_coll_res();
        if (coll && coll->size && coll->uid)
            (void)nfc_tag_mf1_sl1_configure(slot, coll->uid, *coll->size, record + 12);
    }
    sl1_secure_zero(record, 64); sl1_secure_zero(&m_scratch, sizeof(m_scratch));
}
void sl1_profile_sense_request(bool enable) {
    m_sense_pending = false;
    if (!enable) { nfc_tag_14a_sense_switch(false); return; }
    if (nfc_tag_mf1_sl1_is_enabled() && !nfc_tag_mf1_sl1_ready()) { m_sense_pending = true; return; }
    nfc_tag_14a_sense_switch(true);
}
void sl1_profile_poll(void) {
    if (!m_sense_pending) return;
    if (get_device_mode() == DEVICE_MODE_READER || !nfc_tag_mf1_sl1_is_enabled()) {
        m_sense_pending = false; return;
    }
    if (nfc_tag_mf1_sl1_ready()) { m_sense_pending = false; nfc_tag_14a_sense_switch(true); }
}

bool sl1_profile_before_legacy_save(uint8_t slot, uint16_t type, const void *image) {
    uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned; uint8_t actual[32];
    int state = record_read(slot, record);
    bool same = state == 0 || (state == 1 && !record[7]);
    if (state == 1 && record[7] && read16(record + 8) == type)
        same = digest(type, image, actual) && !memcmp(actual, record + 28, 32);
    sl1_secure_zero(record, 64); sl1_secure_zero(actual, sizeof(actual));
    return !sl1_profile_hf_incomplete(slot) && (same || sl1_profile_invalidate(slot));
}
uint8_t sl1_profile_status(const uint8_t *data, uint16_t length, uint8_t out[14]) {
    if (!out || !request(data, length, 2)) return STATUS_PAR_ERR;
    uint8_t slot = data[1]; uint32_t aligned[16]; uint8_t *record = (uint8_t *)aligned;
    memset(out, 0, 14); out[0] = 1; out[1] = slot;
    tag_slot_specific_type_t types; tag_emulation_get_specific_types_by_slot(slot, &types);
    write16(out + 3, types.tag_hf);
    int state = record_read(slot, record);
    bool incomplete = sl1_profile_hf_incomplete(slot);
    if (state < 0 || incomplete) out[2] |= 16;
    if (incomplete) out[2] |= 64;
    if (state == 1 && record[7]) {
        out[2] |= 1; if (record[6]) out[2] |= 2;
        out[5] = record[7]; memcpy(out + 6, record + 10, 2);
        if (!bound(slot, record)) out[2] |= 16;
    }
    if (slot == tag_emulation_get_slot() && nfc_tag_mf1_sl1_is_enabled()) {
        out[2] |= 4; if (nfc_tag_mf1_sl1_ready()) out[2] |= 32;
    }
    if (m_stage.present && m_stage.slot == slot) {
        out[2] |= 8; write16(out + 3, m_stage.type); out[5] = m_stage.scenario;
        write16(out + 6, m_stage.key_number); write16(out + 8, m_stage.received);
        write16(out + 10, blocks(m_stage.type));
    } else write16(out + 10, blocks(types.tag_hf));
    out[12] = m_last_status[slot]; out[13] = m_last_stage[slot];
    sl1_secure_zero(record, 64); sl1_secure_zero(&m_scratch, sizeof(m_scratch));
    return STATUS_SUCCESS;
}
