#include <stdlib.h>

#include "nfc_mf0_ntag.h"
#include "nfc_14a.h"
#include "fds_util.h"
#include "tag_persistence.h"

#define NRF_LOG_MODULE_NAME tag_mf0_ntag
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define VERSION_FIXED_HEADER            0x00
#define VERSION_VENDOR_ID               0x04
#define MF0ULx1_VERSION_PRODUCT_TYPE    0x03
#define NTAG_VERSION_PRODUCT_TYPE       0x04
#define VERSION_PRODUCT_SUBTYPE_17pF    0x01
#define VERSION_PRODUCT_SUBTYPE_50pF    0x02
#define VERSION_MAJOR_PRODUCT           0x01
#define VERSION_MINOR_PRODUCT           0x00
#define MF0UL11_VERSION_STORAGE_SIZE    0x0B
#define MF0UL21_VERSION_STORAGE_SIZE    0x0E
#define NTAG213_VERSION_STORAGE_SIZE    0x0F
#define NTAG215_VERSION_STORAGE_SIZE    0x11
#define NTAG216_VERSION_STORAGE_SIZE    0x13
#define VERSION_PROTOCOL_TYPE           0x03

// MF0 and NTAG COMMANDS
#define CMD_GET_VERSION                 0x60
#define CMD_READ                        0x30
#define CMD_FAST_READ                   0x3A
#define CMD_WRITE                       0xA2
#define CMD_COMPAT_WRITE                0xA0
#define CMD_READ_CNT                    0x39
#define CMD_INCR_CNT                    0x3A
#define CMD_PWD_AUTH                    0x1B
#define CMD_READ_SIG                    0x3C
#define CMD_CHECK_TEARING_EVENT         0x3E
#define CMD_VCSL                        0x4B

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
#define MF0UL11_FIRST_CFG_PAGE   0x10
#define MF0UL21_FIRST_CFG_PAGE   0x25
#define NTAG213_FIRST_CFG_PAGE   0x29
#define NTAG215_FIRST_CFG_PAGE   0x83
#define NTAG216_FIRST_CFG_PAGE   0xE3
#define CONFIG_AREA_SIZE            8

// CONFIG offsets, relative to config start address
#define CONF_AUTH0_BYTE          0x03
#define CONF_ACCESS_OFFSET       0x04
#define CONF_PWD_PAGE_OFFSET        2
#define CONF_PACK_PAGE_OFFSET       3

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
static nfc_tag_mf0_ntag_information_t *m_tag_information = NULL;
// Define and use shadow anti -collision resources
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;
//Define and use MF0/NTAG special communication buffer
static nfc_tag_mf0_ntag_tx_buffer_t m_tag_tx_buffer;
// Save the specific type of MF0/NTAG currently being simulated
static tag_specific_type_t m_tag_type;
static bool m_tag_authenticated = false;

static uint8_t get_nr_pages_by_tag_type(tag_specific_type_t tag_type) {
    uint8_t nr_pages;

    switch (tag_type) {
        case TAG_TYPE_MF0ICU1:
            nr_pages = MF0ICU1_PAGES;
            break;
        case TAG_TYPE_MF0ICU2:
            nr_pages = MF0ICU2_PAGES;
            break;
        case TAG_TYPE_MF0UL11:
            nr_pages = MF0UL11_PAGES;
            break;
        case TAG_TYPE_MF0UL21:
            nr_pages = MF0UL21_PAGES;
            break;
        case TAG_TYPE_NTAG_213:
            nr_pages = NTAG213_PAGES;
            break;
        case TAG_TYPE_NTAG_215:
            nr_pages = NTAG215_PAGES;
            break;
        case TAG_TYPE_NTAG_216:
            nr_pages = NTAG216_PAGES;
            break;
        default:
            nr_pages = 0;
            break;
    }

    return nr_pages;
}

static uint8_t get_first_cfg_page_by_tag_type(tag_specific_type_t tag_type) {
    uint8_t page;

    switch (tag_type) {
        case TAG_TYPE_MF0UL11:
            page = MF0UL11_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_MF0UL21:
            page = MF0UL21_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_NTAG_213:
            page = NTAG213_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_NTAG_215:
            page = NTAG215_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_NTAG_216:
            page = NTAG216_FIRST_CFG_PAGE;
            break;
        default:
            page = 0;
            break;
    }

    return page;
}

static uint8_t get_block_max_by_tag_type(tag_specific_type_t tag_type) {
    uint8_t max_pages = get_nr_pages_by_tag_type(tag_type);
    uint8_t first_cfg_page = get_first_cfg_page_by_tag_type(tag_type);

    if (first_cfg_page == 0 || m_tag_authenticated || m_tag_information->config.mode_uid_magic) return max_pages;
    
    uint8_t auth0 = m_tag_information->memory[first_cfg_page][CONF_AUTH0_BYTE];
    return (max_pages > auth0) ? auth0 : max_pages;
}

static bool is_ntag() {
    switch (m_tag_type) {
        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216:
            return true;
        default:
            return false;
    }
}

static void handle_get_version_command() {
    switch (m_tag_type) {
        case TAG_TYPE_MF0UL11:
            m_tag_tx_buffer.tx_buffer[6] = MF0UL11_VERSION_STORAGE_SIZE;
            m_tag_tx_buffer.tx_buffer[2] = MF0ULx1_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_MF0UL21:
            m_tag_tx_buffer.tx_buffer[6] = MF0UL21_VERSION_STORAGE_SIZE;
            m_tag_tx_buffer.tx_buffer[2] = MF0ULx1_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_213:
            m_tag_tx_buffer.tx_buffer[6] = NTAG213_VERSION_STORAGE_SIZE;
            m_tag_tx_buffer.tx_buffer[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_215:
            m_tag_tx_buffer.tx_buffer[6] = NTAG215_VERSION_STORAGE_SIZE;
            m_tag_tx_buffer.tx_buffer[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_216:
            m_tag_tx_buffer.tx_buffer[6] = NTAG216_VERSION_STORAGE_SIZE;
            m_tag_tx_buffer.tx_buffer[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        default:
            // MF0ICU1 and MF0ICU2 do not support GET_VERSION
            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
            return;
    }

    m_tag_tx_buffer.tx_buffer[0] = VERSION_FIXED_HEADER;
    m_tag_tx_buffer.tx_buffer[1] = VERSION_VENDOR_ID;
    m_tag_tx_buffer.tx_buffer[3] = VERSION_PRODUCT_SUBTYPE_50pF; // TODO: make configurable for MF0ULx1
    m_tag_tx_buffer.tx_buffer[4] = VERSION_MAJOR_PRODUCT;
    m_tag_tx_buffer.tx_buffer[5] = VERSION_MINOR_PRODUCT;
    m_tag_tx_buffer.tx_buffer[7] = VERSION_PROTOCOL_TYPE;

    nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, 8, true);
}

static void handle_read_command(uint8_t block_num) {
    int block_max = get_block_max_by_tag_type(m_tag_type);

    if (block_num >= block_max) {
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
        return;
    }

    uint8_t pwd_page = get_first_cfg_page_by_tag_type(m_tag_type);
    if (pwd_page != 0) pwd_page += CONF_PWD_PAGE_OFFSET;

    for (int block = 0; block < 4; block++) {
    // In case PWD or PACK pages are read we need to write zero to the output buffer. In UID magic mode we don't care.
        if (m_tag_information->config.mode_uid_magic || (pwd_page == 0) || ((block - pwd_page) >= 2)) {
            memcpy(m_tag_tx_buffer.tx_buffer + block * 4, m_tag_information->memory[(block_num + block) % block_max], NFC_TAG_MF0_NTAG_DATA_SIZE);
        } else {
            memset(m_tag_tx_buffer.tx_buffer + block * 4, 0, NFC_TAG_MF0_NTAG_DATA_SIZE);
        }
    }
    nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, BYTES_PER_READ, true);
}

static bool check_ro_lock_on_page(int block_num) {
    if (block_num < 3) return true;
    else if (block_num == 3) return (m_tag_information->memory[2][2] & 1) == 1;
    else if (block_num <= MF0ICU1_PAGES) {
        bool locked = false;

        // check block locking bits
        if (block_num <= 9) locked |= (m_tag_information->memory[2][2] & 2) == 2;
        else locked |= (m_tag_information->memory[2][2] & 4) == 4;

        locked |= (((*(uint16_t *)&m_tag_information->memory[2][2]) >> block_num) & 1) == 1;

        return locked;
    } else {
        // too large block number
        return true;
    }
}

static int handle_write_command(uint8_t block_num, uint8_t *p_data) {
    int block_max = get_block_max_by_tag_type(m_tag_type);

    if (block_num >= block_max) {
        return NAK_INVALID_OPERATION_TBIV;
    }

    if (m_tag_information->config.mode_uid_magic) {
        // anything can be written in this mode
        memcpy(m_tag_information->memory[block_num], p_data, NFC_TAG_MF0_NTAG_DATA_SIZE);
        return ACK_VALUE;
    }

    switch (block_num) {
        case 0:
        case 1:
            return NAK_INVALID_OPERATION_TBIV;
        case 2:
            // Page 2 contains lock bytes for pages 3-15. These are OR'ed when not in the UID
            // magic mode. First two bytes are ignored.
            m_tag_information->memory[2][2] |= p_data[2];
            m_tag_information->memory[2][3] |= p_data[3];
            break;
        case 3:
            // Page 3 contains what's called OTP bits for Ultralight tags and CC bits for NTAG
            // cards, these work in the same way.
            if (!check_ro_lock_on_page(block_num)) {
                // lock bit for OTP page is not set
                for (int i = 0; i < NFC_TAG_MF0_NTAG_DATA_SIZE; i++) {
                    m_tag_information->memory[3][i] |= p_data[i];
                }
            }
            break;
        default:
            if (!check_ro_lock_on_page(block_num)) {
                memcpy(m_tag_information->memory[block_num], p_data, NFC_TAG_MF0_NTAG_DATA_SIZE);
            }
            break;
    }

    return ACK_VALUE;
}

static void nfc_tag_mf0_ntag_state_handler(uint8_t *p_data, uint16_t szDataBits) {
    uint8_t command = p_data[0];
    uint8_t block_num = p_data[1];

    switch (command) {
        case CMD_GET_VERSION:
            handle_get_version_command();
            break;
        case CMD_READ: {
            handle_read_command(block_num);
            break;
        }
        case CMD_FAST_READ: {
            uint8_t end_block_num = p_data[2];
            // TODO: support ultralight
            if (!is_ntag() || (block_num > end_block_num) || (block_num >= get_block_max_by_tag_type(m_tag_type)) || (end_block_num >= get_block_max_by_tag_type(m_tag_type))) {
                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
                break;
            }
            for (int block = block_num; block <= end_block_num; block++) {
                memcpy(m_tag_tx_buffer.tx_buffer + (block - block_num) * 4, m_tag_information->memory[block], NFC_TAG_MF0_NTAG_DATA_SIZE);
            }
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, (end_block_num - block_num + 1) * NFC_TAG_MF0_NTAG_DATA_SIZE, true);
            break;
        }
        case CMD_WRITE:
        case CMD_COMPAT_WRITE: {
            int resp = handle_write_command(block_num, &p_data[2]);
            nfc_tag_14a_tx_nbit(resp, 4);
            break;
        }
        /*case CMD_PWD_AUTH: {
            // TODO: IMPLEMENT COUNTER AUTHLIM
            uint8_t Password[4];
            memcpy(Password, m_tag_information->memory[get_block_cfg_by_tag_type(m_tag_type) + CONF_PASSWORD_OFFSET], 4);
            if (Password[0] != p_data[1] || Password[1] != p_data[2] || Password[2] != p_data[3] || Password[3] != p_data[4]) {
                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
                break;
            }
            // Authenticate the user
            //RESET AUTHLIM COUNTER, CURRENTLY NOT IMPLEMENTED
            // TODO
            // Send the PACK value back
            if (m_tag_information->config.mode_uid_magic) {
                nfc_tag_14a_tx_bytes(ntagPwdOK, 2, true);
            } else {
                nfc_tag_14a_tx_bytes(m_tag_information->memory[get_block_cfg_by_tag_type(m_tag_type) + CONF_PASSWORD_OFFSET], 2, true);
            }
            break;
        }*/
        case CMD_READ_SIG:
            memset(m_tag_tx_buffer.tx_buffer, 0xCA, SIGNATURE_LENGTH);
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, SIGNATURE_LENGTH, true);
            break;
    }
    return;
}

static nfc_tag_14a_coll_res_reference_t *get_coll_res() {
    // Use a separate anti -conflict information instead of using the information in the sector
    m_shadow_coll_res.sak = m_tag_information->res_coll.sak;
    m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
    m_shadow_coll_res.uid = m_tag_information->res_coll.uid;
    m_shadow_coll_res.size = &(m_tag_information->res_coll.size);
    m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);
    // Finally, a shadow data structure pointer with only reference, no physical shadow,
    return &m_shadow_coll_res;
}

static void nfc_tag_mf0_ntag_reset_handler() {

}

static int get_information_size_by_tag_type(tag_specific_type_t type) {
    return sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_mf0_ntag_configure_t) + (get_nr_pages_by_tag_type(type) * NFC_TAG_MF0_NTAG_DATA_SIZE);
}

/** @brief MF0/NTAG callback before saving data
 * @param type detailed label type
 * @param buffer data buffer
 * @return to be saved, the length of the data that needs to be saved, it means not saved when 0
 */
int nfc_tag_mf0_ntag_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (m_tag_type != TAG_TYPE_UNDEFINED && m_tag_information != NULL) {
        // Save the corresponding size data according to the current label type
        return get_information_size_by_tag_type(type);
    } else {
        return 0;
    }
}

int nfc_tag_mf0_ntag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    int info_size = get_information_size_by_tag_type(type);
    if (buffer->length >= info_size) {
        // Convert the data buffer to MF0/NTAG structure type
        m_tag_information = (nfc_tag_mf0_ntag_information_t *)buffer->buffer;
        // The specific type of MF0/NTAG tag that is simulated by the cache
        m_tag_type = type;
        // Register 14A communication management interface
        nfc_tag_14a_handler_t handler_for_14a = {
            .get_coll_res = get_coll_res,
            .cb_state = nfc_tag_mf0_ntag_state_handler,
            .cb_reset = nfc_tag_mf0_ntag_reset_handler,
        };
        nfc_tag_14a_set_handler(&handler_for_14a);
        NRF_LOG_INFO("HF ntag data load finish.");
    } else {
        NRF_LOG_ERROR("nfc_tag_mf0_ntag_information_t too big.");
    }
    return info_size;
}

typedef struct __attribute__((aligned(4))) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    nfc_tag_mf0_ntag_configure_t config;
    uint8_t memory[NFC_TAG_NTAG_BLOCK_MAX][NFC_TAG_MF0_NTAG_DATA_SIZE];
}
nfc_tag_mf0_ntag_information_max_t;

// Initialized NTAG factory data
bool nfc_tag_mf0_ntag_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default ntag data
    uint8_t default_p0[] = { 0x04, 0x68, 0x95, 0x71 };
    uint8_t default_p1[] = { 0xFA, 0x5C, 0x64, 0x80 };
    uint8_t default_p2[] = { 0x42, 0x48, 0x0F, 0xE0 };

    if (!is_ntag()) {
        default_p2[2] = 0;
        default_p2[3] = 0;
    }

    // default ntag info
    nfc_tag_mf0_ntag_information_max_t ntag_tmp_information;
    nfc_tag_mf0_ntag_information_t *p_ntag_information;
    p_ntag_information = (nfc_tag_mf0_ntag_information_t *)&ntag_tmp_information;
    int block_max = get_nr_pages_by_tag_type(tag_type);
    for (int block = 0; block < block_max; block++) {
        switch (block) {
            case 0:
                memcpy(p_ntag_information->memory[block], default_p0, NFC_TAG_MF0_NTAG_DATA_SIZE);
                break;
            case 1:
                memcpy(p_ntag_information->memory[block], default_p1, NFC_TAG_MF0_NTAG_DATA_SIZE);
                break;
            case 2:
                memcpy(p_ntag_information->memory[block], default_p2, NFC_TAG_MF0_NTAG_DATA_SIZE);
                break;
            default:
                memset(p_ntag_information->memory[block], 0, NFC_TAG_MF0_NTAG_DATA_SIZE);
                break;
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
    p_ntag_information->config.mode_uid_magic = false;

    // save data to flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int info_size = get_information_size_by_tag_type(tag_type);
    NRF_LOG_INFO("MF0/NTAG info size: %d", info_size);
    bool ret = fds_write_sync(map_info.id, map_info.key, info_size, p_ntag_information);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}

int nfc_tag_mf0_ntag_get_uid_mode() {
    if (m_tag_type == TAG_TYPE_UNDEFINED || m_tag_information == NULL) return -1;

    return (int)m_tag_information->config.mode_uid_magic;
}

bool nfc_tag_mf0_ntag_set_uid_mode(bool enabled) {
    if (m_tag_type == TAG_TYPE_UNDEFINED || m_tag_information == NULL) return false;

    m_tag_information->config.mode_uid_magic = enabled;
    return true;
}