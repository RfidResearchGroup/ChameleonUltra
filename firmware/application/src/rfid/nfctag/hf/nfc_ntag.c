#include <stdlib.h>

#include "nfc_ntag.h"
#include "nfc_14a.h"
#include "fds_util.h"
#include "tag_persistence.h"

#define NRF_LOG_MODULE_NAME tag_ntag
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define NTAG213_VERSION 0x0F
#define NTAG215_VERSION 0x11
#define NTAG216_VERSION 0x13

// NTAG COMMANDS
#define CMD_GET_VERSION             0x60
#define CMD_READ                    0x30
#define CMD_FAST_READ               0x3A
#define CMD_WRITE                   0xA2
#define CMD_COMPAT_WRITE            0xA0
#define CMD_READ_CNT                0x39
#define CMD_PWD_AUTH                0x1B
#define CMD_READ_SIG                0x3C

// MEMORY LAYOUT STUFF, addresses and sizes in bytes
// UID stuff
#define UID_CL1_ADDRESS             0x00
#define UID_CL1_SIZE                3
#define UID_BCC1_ADDRESS            0x03
#define UID_CL2_ADDRESS             0x04
#define UID_CL2_SIZE                4
#define UID_BCC2_ADDRESS            0x08
// LockBytes stuff
#define STATIC_LOCKBYTE_0_ADDRESS   0x0A
#define STATIC_LOCKBYTE_1_ADDRESS   0x0B
// CONFIG stuff
#define NTAG213_CONFIG_AREA_START_ADDRESS   0xA4  // 4 * 0x29
#define NTAG215_CONFIG_AREA_START_ADDRESS   0x20C // 4 * 0x83
#define NTAG216_CONFIG_AREA_START_ADDRESS   0x38C // 4 * 0xE3
#define CONFIG_AREA_SIZE            8
// CONFIG offsets, relative to config start address
#define CONF_AUTH0_OFFSET           0x03
#define CONF_ACCESS_OFFSET          0x04
#define CONF_PASSWORD_OFFSET        0x08
#define CONF_PACK_OFFSET            0x0C

// WRITE STUFF
#define BYTES_PER_WRITE             4
#define PAGE_WRITE_MIN              0x02

// CONFIG masks to check individual needed bits
#define CONF_ACCESS_PROT            0x80

#define VERSION_INFO_LENGTH         8 //8 bytes info length + crc

#define BYTES_PER_READ              16

// SIGNATURE Length
#define SIGNATURE_LENGTH            32

// NTAG215_Version[7] mean:
// 0x0F ntag213
// 0x11 ntag215
// 0x13 ntag216
const uint8_t ntagVersion[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
/* pwd auth for amiibo */
uint8_t ntagPwdOK[2] = {0x80, 0x80};

// Data structure pointer to the label information
static nfc_tag_ntag_information_t *m_tag_information = NULL;
// Define and use shadow anti -collision resources
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;
//Define and use NTAG special communication buffer
static nfc_tag_ntag_tx_buffer_t m_tag_tx_buffer;
// Save the specific type of NTAG currently being simulated
static tag_specific_type_t m_tag_type;

static int get_block_max_by_tag_type(tag_specific_type_t tag_type) {
    int block_max;
    switch (tag_type) {
        case TAG_TYPE_NTAG_213:
            block_max = NTAG213_PAGES;
            break;
        default:
        case TAG_TYPE_NTAG_215:
            block_max = NTAG215_PAGES;
            break;
        case TAG_TYPE_NTAG_216:
            block_max = NTAG216_PAGES;
            break;
    }
    return block_max;
}

static int get_block_cfg_by_tag_type(tag_specific_type_t tag_type) {
    int block_max;
    switch (tag_type) {
        case TAG_TYPE_NTAG_213:
            block_max = NTAG213_CONFIG_AREA_START_ADDRESS;
            break;
        default:
        case TAG_TYPE_NTAG_215:
            block_max = NTAG215_CONFIG_AREA_START_ADDRESS;
            break;
        case TAG_TYPE_NTAG_216:
            block_max = NTAG216_CONFIG_AREA_START_ADDRESS;
            break;
    }
    return block_max;
}

void nfc_tag_ntag_state_handler(uint8_t *p_data, uint16_t szDataBits) {
    uint8_t command = p_data[0];
    uint8_t block_num = p_data[1];

    switch (command) {
        case CMD_GET_VERSION:
            memcpy(m_tag_tx_buffer.tx_buffer, ntagVersion, 8);
            switch (m_tag_type) {
                case TAG_TYPE_NTAG_213:
                    m_tag_tx_buffer.tx_buffer[6] = NTAG213_VERSION;
                    break;
                default:
                case TAG_TYPE_NTAG_215:
                    m_tag_tx_buffer.tx_buffer[6] = NTAG215_VERSION;
                    break;
                case TAG_TYPE_NTAG_216:
                    m_tag_tx_buffer.tx_buffer[6] = NTAG216_VERSION;
                    break;
            }
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, 8, true);
            break;
        case CMD_READ:
            if (block_num < get_block_max_by_tag_type(m_tag_type)) {
                for (int block = 0; block < 4; block++) {
                    memcpy(m_tag_tx_buffer.tx_buffer + block * 4, m_tag_information->memory[block_num + block], NFC_TAG_NTAG_DATA_SIZE);
                }
                nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, BYTES_PER_READ, true);
            } else {
                nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
            }
            break;
        case CMD_FAST_READ: {
            uint8_t end_block_num = p_data[2];
            if ((block_num > end_block_num) || (block_num >= get_block_max_by_tag_type(m_tag_type)) || (end_block_num >= get_block_max_by_tag_type(m_tag_type))) {
                nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBV, 4);
                break;
            }
            for (int block = block_num; block <= end_block_num; block++) {
                memcpy(m_tag_tx_buffer.tx_buffer + (block - block_num) * 4, m_tag_information->memory[block], NFC_TAG_NTAG_DATA_SIZE);
            }
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, (end_block_num - block_num + 1) * NFC_TAG_NTAG_DATA_SIZE, true);
            break;
        }
        case CMD_WRITE:
            // TODO
            nfc_tag_14a_tx_nbit_delay_window(ACK_VALUE, 4);
            break;
        case CMD_COMPAT_WRITE:
            // TODO
            break;
        case CMD_PWD_AUTH: {
            /* TODO: IMPLEMENT COUNTER AUTHLIM */
            uint8_t Password[4];
            memcpy(Password, m_tag_information->memory[get_block_cfg_by_tag_type(m_tag_type) + CONF_PASSWORD_OFFSET], 4);
            if (Password[0] != p_data[1] || Password[1] != p_data[2] || Password[2] != p_data[3] || Password[3] != p_data[4]) {
                nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                break;
            }
            /* Authenticate the user */
            //RESET AUTHLIM COUNTER, CURRENTLY NOT IMPLEMENTED
            // TODO
            /* Send the PACK value back */
            if (m_tag_information->config.mode_uid_magic) {
                nfc_tag_14a_tx_bytes(ntagPwdOK, 2, true);
            } else {
                nfc_tag_14a_tx_bytes(m_tag_information->memory[get_block_cfg_by_tag_type(m_tag_type) + CONF_PASSWORD_OFFSET], 2, true);
            }
            break;
        }
        case CMD_READ_SIG:
            memset(m_tag_tx_buffer.tx_buffer, 0xCA, SIGNATURE_LENGTH);
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, SIGNATURE_LENGTH, true);
            break;
    }
    return;
}

nfc_tag_14a_coll_res_reference_t *get_ntag_coll_res() {
    // Use a separate anti -conflict information instead of using the information in the sector
    m_shadow_coll_res.sak = m_tag_information->res_coll.sak;
    m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
    m_shadow_coll_res.uid = m_tag_information->res_coll.uid;
    m_shadow_coll_res.size = &(m_tag_information->res_coll.size);
    m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);
    // Finally, a shadow data structure pointer with only reference, no physical shadow,
    return &m_shadow_coll_res;
}

void nfc_tag_ntag_reset_handler() {
    // TODO
}

static int get_information_size_by_tag_type(tag_specific_type_t type) {
    return sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_ntag_configure_t) + (get_block_max_by_tag_type(type) * NFC_TAG_NTAG_DATA_SIZE);
}

/** @brief ntag's callback before saving data
 * @param type detailed label type
 * @param buffer data buffer
 * @return to be saved, the length of the data that needs to be saved, it means not saved when 0
 */
int nfc_tag_ntag_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (m_tag_type != TAG_TYPE_UNKNOWN) {
        // Save the corresponding size data according to the current label type
        return get_information_size_by_tag_type(type);
    } else {
        return 0;
    }
}

int nfc_tag_ntag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    int info_size = get_information_size_by_tag_type(type);
    if (buffer->length >= info_size) {
        // Convert the data buffer to NTAG structure type
        m_tag_information = (nfc_tag_ntag_information_t *)buffer->buffer;
        // The specific type of NTAG that is simulated by the cache
        m_tag_type = type;
        // Register 14A communication management interface
        nfc_tag_14a_handler_t handler_for_14a = {
            .get_coll_res = get_ntag_coll_res,
            .cb_state = nfc_tag_ntag_state_handler,
            .cb_reset = nfc_tag_ntag_reset_handler,
        };
        nfc_tag_14a_set_handler(&handler_for_14a);
        NRF_LOG_INFO("HF ntag data load finish.");
    } else {
        NRF_LOG_ERROR("nfc_tag_ntag_information_t too big.");
    }
    return info_size;
}

// Initialized NTAG factory data
bool nfc_tag_ntag_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default ntag data
    uint8_t default_p0[] = { 0x04, 0x68, 0x95, 0x71 };
    uint8_t default_p1[] = { 0xFA, 0x5C, 0x64, 0x80 };
    uint8_t default_p2[] = { 0x42, 0x48, 0x0F, 0xE0 };

    // default ntag info
    nfc_tag_ntag_information_t ntag_tmp_information;
    nfc_tag_ntag_information_t *p_ntag_information;
    p_ntag_information = &ntag_tmp_information;
    int block_max = get_block_max_by_tag_type(tag_type);
    for (int block = 0; block < block_max; block++) {
        if (block == 0) {
            memcpy(p_ntag_information->memory[block], default_p0, NFC_TAG_NTAG_DATA_SIZE);
        }
        if (block == 1) {
            memcpy(p_ntag_information->memory[block], default_p1, NFC_TAG_NTAG_DATA_SIZE);
        }
        if (block == 2) {
            memcpy(p_ntag_information->memory[block], default_p2, NFC_TAG_NTAG_DATA_SIZE);
        }
    }

    // default ntag auto ant-collision res
    p_ntag_information->res_coll.atqa[0] = 0x44;
    p_ntag_information->res_coll.atqa[1] = 0x00;
    p_ntag_information->res_coll.sak[0] = 0x00;
    p_ntag_information->res_coll.uid[0] = 0x04;
    p_ntag_information->res_coll.uid[1] = 0x68;
    p_ntag_information->res_coll.uid[2] = 0x95;
    p_ntag_information->res_coll.uid[3] = 0x71;
    p_ntag_information->res_coll.uid[4] = 0xFA;
    p_ntag_information->res_coll.uid[5] = 0x5C;
    p_ntag_information->res_coll.uid[6] = 0x64;
    p_ntag_information->res_coll.size = NFC_TAG_14A_UID_DOUBLE_SIZE;
    p_ntag_information->res_coll.ats.length = 0;

    // default ntag config
    p_ntag_information->config.mode_uid_magic = true;
    p_ntag_information->config.detection_enable = false;

    // save data to flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int info_size = get_information_size_by_tag_type(tag_type);   // auto 4 byte align.
    NRF_LOG_INFO("NTAG info size: %d", info_size);
    bool ret = fds_write_sync(map_info.id, map_info.key, info_size / 4, p_ntag_information);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}
