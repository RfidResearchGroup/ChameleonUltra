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
#define NTAG210_VERSION_STORAGE_SIZE    0x0B
#define NTAG212_VERSION_STORAGE_SIZE    0x0E
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
#define CMD_INCR_CNT                    0xA5
#define CMD_PWD_AUTH                    0x1B
#define CMD_READ_SIG                    0x3C
#define CMD_CHECK_TEARING_EVENT         0x3E
#define CMD_VCSL                        0x4B

// MEMORY LAYOUT STUFF, addresses and sizes in bytes
// UID stuff
#define UID_CL1_ADDRESS                 0x00
#define UID_CL1_SIZE                       3
#define UID_BCC1_ADDRESS                0x03
#define UID_CL2_ADDRESS                 0x04
#define UID_CL2_SIZE                       4
#define UID_BCC2_ADDRESS                0x08
// LockBytes stuff
#define STATIC_LOCKBYTE_0_ADDRESS       0x0A
#define STATIC_LOCKBYTE_1_ADDRESS       0x0B
// CONFIG stuff
#define MF0ICU2_USER_MEMORY_END         0x28
#define MF0ICU2_CNT_PAGE                0x29
#define MF0ICU2_FIRST_KEY_PAGE          0x2C
#define MF0UL11_FIRST_CFG_PAGE          0x10
#define MF0UL11_USER_MEMORY_END         (MF0UL11_FIRST_CFG_PAGE)
#define MF0UL21_FIRST_CFG_PAGE          0x25
#define MF0UL21_USER_MEMORY_END         0x24
#define NTAG210_FIRST_CFG_PAGE          0x10
#define NTAG210_USER_MEMORY_END         (NTAG210_FIRST_CFG_PAGE)
#define NTAG212_FIRST_CFG_PAGE          0x25
#define NTAG212_USER_MEMORY_END         0x24
#define NTAG213_FIRST_CFG_PAGE          0x29
#define NTAG213_USER_MEMORY_END         0x28
#define NTAG215_FIRST_CFG_PAGE          0x83
#define NTAG215_USER_MEMORY_END         0x82
#define NTAG216_FIRST_CFG_PAGE          0xE3
#define NTAG216_USER_MEMORY_END         0xE2
#define CONFIG_AREA_SIZE                   8

// CONFIG offsets, relative to config start address
#define CONF_MIRROR_BYTE                   0
#define CONF_MIRROR_PAGE_BYTE              2
#define CONF_ACCESS_PAGE_OFFSET            1
#define CONF_ACCESS_BYTE                   0
#define CONF_AUTH0_BYTE                 0x03
#define CONF_PWD_PAGE_OFFSET               2
#define CONF_PACK_PAGE_OFFSET              3
#define CONF_VCTID_PAGE_OFFSET             1
#define CONF_VCTID_PAGE_BYTE               1

#define MIRROR_BYTE_BYTE_MASK           0x30
#define MIRROR_BYTE_BYTE_SHIFT             4
#define MIRROR_BYTE_CONF_MASK           0xC0
#define MIRROR_BYTE_CONF_SHIFT             6

// WRITE STUFF
#define BYTES_PER_WRITE                    4
#define PAGE_WRITE_MIN                  0x02

// CONFIG masks to check individual needed bits
#define CONF_ACCESS_AUTHLIM_MASK        0x07
#define CONF_ACCESS_NFC_CNT_EN          0x10
#define CONF_ACCESS_NFC_CNT_PWD_PROT    0x04
#define CONF_ACCESS_CFGLCK              0x40
#define CONF_ACCESS_PROT                0x80

#define VERSION_INFO_LENGTH                8 //8 bytes info length + crc

#define BYTES_PER_READ                    16

// SIGNATURE Length
#define SIGNATURE_LENGTH                  32
#define PAGES_PER_VERSION               (NFC_TAG_MF0_NTAG_VER_SIZE / NFC_TAG_MF0_NTAG_DATA_SIZE)

// Values for MIRROR_CONF
#define MIRROR_CONF_DISABLED               0
#define MIRROR_CONF_UID                    1
#define MIRROR_CONF_CNT                    2
#define MIRROR_CONF_UID_CNT                3

#define MIRROR_UID_SIZE                   14
#define MIRROR_CNT_SIZE                    6
#define MIRROR_UID_CNT_SIZE               21

// NTAG215_Version[7] mean:
// 0x0F ntag213
// 0x11 ntag215
// 0x13 ntag216
const uint8_t ntagVersion[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};

// Data structure pointer to the label information
static nfc_tag_mf0_ntag_information_t *m_tag_information = NULL;
// Define and use shadow anti -collision resources
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;
//Define and use MF0/NTAG special communication buffer
static nfc_tag_mf0_ntag_tx_buffer_t m_tag_tx_buffer;
// Save the specific type of MF0/NTAG currently being simulated
static tag_specific_type_t m_tag_type;
static bool m_tag_authenticated = false;
static bool m_did_first_read = false;

int nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(tag_specific_type_t tag_type) {
    int nr_pages = -1;

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
        case TAG_TYPE_NTAG_210:
            nr_pages = NTAG210_PAGES;
            break;
        case TAG_TYPE_NTAG_212:
            nr_pages = NTAG212_PAGES;
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
            nr_pages = -1;
            break;
    }

    return nr_pages;
}

static int get_nr_pages_by_tag_type(tag_specific_type_t tag_type) {
    int nr_pages = nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(tag_type);
    ASSERT(nr_pages > 0);
    return nr_pages;
}

static int get_total_pages_by_tag_type(tag_specific_type_t tag_type) {
    int nr_pages = 0;

    switch (tag_type) {
        case TAG_TYPE_MF0ICU1:
            nr_pages = MF0ICU1_PAGES;
            break;
        case TAG_TYPE_MF0ICU2:
            nr_pages = MF0ICU2_PAGES;
            break;
        case TAG_TYPE_MF0UL11:
            nr_pages = MF0UL11_TOTAL_PAGES;
            break;
        case TAG_TYPE_MF0UL21:
            nr_pages = MF0UL21_TOTAL_PAGES;
            break;
        case TAG_TYPE_NTAG_210:
            nr_pages = NTAG210_TOTAL_PAGES;
            break;
        case TAG_TYPE_NTAG_212:
            nr_pages = NTAG212_TOTAL_PAGES;
            break;
        case TAG_TYPE_NTAG_213:
            nr_pages = NTAG213_TOTAL_PAGES;
            break;
        case TAG_TYPE_NTAG_215:
            nr_pages = NTAG215_TOTAL_PAGES;
            break;
        case TAG_TYPE_NTAG_216:
            nr_pages = NTAG216_TOTAL_PAGES;
            break;
        default:
            ASSERT(false);
            break;
    }

    return nr_pages;
}

static int get_first_cfg_page_by_tag_type(tag_specific_type_t tag_type) {
    int page;

    switch (tag_type) {
        case TAG_TYPE_MF0UL11:
            page = MF0UL11_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_MF0UL21:
            page = MF0UL21_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_NTAG_210:
            page = NTAG210_FIRST_CFG_PAGE;
            break;
        case TAG_TYPE_NTAG_212:
            page = NTAG212_FIRST_CFG_PAGE;
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

static int get_block_max_by_tag_type(tag_specific_type_t tag_type, bool read) {
    int max_pages = get_nr_pages_by_tag_type(tag_type);
    int first_cfg_page = get_first_cfg_page_by_tag_type(tag_type);

    if (first_cfg_page == 0 || m_tag_authenticated || m_tag_information->config.mode_uid_magic) return max_pages;
    
    uint8_t auth0 = m_tag_information->memory[first_cfg_page][CONF_AUTH0_BYTE];
    uint8_t access = m_tag_information->memory[first_cfg_page + 1][0];

    NRF_LOG_INFO("auth0 %02x access %02x max_pages %02x first_cfg_page %02x authenticated %i", auth0, access, max_pages, first_cfg_page, m_tag_authenticated);

    if (!read || ((access & CONF_ACCESS_PROT) != 0)) return (max_pages > auth0) ? auth0 : max_pages;
    else return max_pages;
}

static bool is_ntag() {
    switch (m_tag_type) {
        case TAG_TYPE_NTAG_210:
        case TAG_TYPE_NTAG_212:
        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216:
            return true;
        default:
            return false;
    }
}

int get_version_page_by_tag_type(tag_specific_type_t tag_type) {
    int version_page_off;

    switch (tag_type) {
        case TAG_TYPE_MF0UL11:
            version_page_off = MF0UL11_PAGES + MF0ULx1_NUM_CTRS;
            break;
        case TAG_TYPE_MF0UL21:
            version_page_off = MF0UL21_PAGES + MF0ULx1_NUM_CTRS;
            break;
        // NTAG 210 and 212 don't have a counter, but we still allocate space for one to
        // record unsuccessful auth attempts
        case TAG_TYPE_NTAG_210:
            version_page_off = NTAG210_PAGES + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_212:
            version_page_off = NTAG212_PAGES + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_213:
            version_page_off = NTAG213_PAGES + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_215:
            version_page_off = NTAG215_PAGES + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_216:
            version_page_off = NTAG216_PAGES + NTAG_NUM_CTRS;
            break;
        default:
            version_page_off = -1;
            break;
    }

    return version_page_off;
}

int get_signature_page_by_tag_type(tag_specific_type_t tag_type) {
    int version_page_off;

    switch (m_tag_type) {
        case TAG_TYPE_MF0UL11:
            version_page_off = MF0UL11_PAGES + MF0ULx1_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_MF0UL21:
            version_page_off = MF0UL21_PAGES + MF0ULx1_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_NTAG_210:
            version_page_off = NTAG210_PAGES + NTAG_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_NTAG_212:
            version_page_off = NTAG212_PAGES + NTAG_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_NTAG_213:
            version_page_off = NTAG213_PAGES + NTAG_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_NTAG_215:
            version_page_off = NTAG215_PAGES + NTAG_NUM_CTRS + PAGES_PER_VERSION;
            break;
        case TAG_TYPE_NTAG_216:
            version_page_off = NTAG216_PAGES + NTAG_NUM_CTRS + PAGES_PER_VERSION;
            break;
        default:
            version_page_off = -1;
            break;
    }

    return version_page_off;
}

uint8_t *nfc_tag_mf0_ntag_get_version_data() {
    int version_page = get_version_page_by_tag_type(m_tag_type);

    if (version_page > 0) return &m_tag_information->memory[version_page][0];
    else return NULL;
}

uint8_t *nfc_tag_mf0_ntag_get_signature_data() {
    int signature_page = get_signature_page_by_tag_type(m_tag_type);

    if (signature_page > 0) return &m_tag_information->memory[signature_page][0];
    else return NULL;
}

static void handle_get_version_command() {
    int version_page = get_version_page_by_tag_type(m_tag_type);

    if (version_page > 0) {
        memcpy(m_tag_tx_buffer.tx_buffer, &m_tag_information->memory[version_page][0], NFC_TAG_MF0_NTAG_VER_SIZE);
        nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, NFC_TAG_MF0_NTAG_VER_SIZE, true);
    } else {
        NRF_LOG_WARNING("current card type does not support GET_VERSION");
        // MF0ICU1 and MF0ICU2 do not support GET_VERSION
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
    }
}

static void handle_read_sig_command() {
    int signature_page = get_signature_page_by_tag_type(m_tag_type);

    if (signature_page > 0) {
        memcpy(m_tag_tx_buffer.tx_buffer, &m_tag_information->memory[signature_page][0], NFC_TAG_MF0_NTAG_SIG_SIZE);
        nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, NFC_TAG_MF0_NTAG_SIG_SIZE, true);
    } else {
        NRF_LOG_WARNING("current card type does not support READ_SIG");
        // MF0ICU1 and MF0ICU2 do not support READ_SIG
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
    }
}

static int mirror_size_for_mode(uint8_t mirror_mode) {
    switch (mirror_mode) {
    case MIRROR_CONF_UID:
        return MIRROR_UID_SIZE;
    case MIRROR_CONF_CNT:
        return MIRROR_CNT_SIZE;
    case MIRROR_CONF_UID_CNT:
        return MIRROR_UID_CNT_SIZE;
    default:
        ASSERT(false);
        return 0;
    }
}

static int get_user_data_end_by_tag_type(tag_specific_type_t type) {
    int nr_pages = 0;

    switch (type) {
    case TAG_TYPE_MF0ICU1:
        nr_pages = MF0ICU1_PAGES;
        break;
    case TAG_TYPE_MF0ICU2:
        nr_pages = MF0ICU2_USER_MEMORY_END;
        break;
    case TAG_TYPE_MF0UL11:
        nr_pages = MF0UL11_USER_MEMORY_END;
        break;
    case TAG_TYPE_MF0UL21:
        nr_pages = MF0UL21_USER_MEMORY_END;
        break;
    case TAG_TYPE_NTAG_210:
        nr_pages = NTAG210_USER_MEMORY_END;
        break;
    case TAG_TYPE_NTAG_212:
        nr_pages = NTAG212_USER_MEMORY_END;
        break;
    case TAG_TYPE_NTAG_213:
        nr_pages = NTAG213_USER_MEMORY_END;
        break;
    case TAG_TYPE_NTAG_215:
        nr_pages = NTAG215_USER_MEMORY_END;
        break;
    case TAG_TYPE_NTAG_216:
        nr_pages = NTAG216_USER_MEMORY_END;
        break;
    default:
        ASSERT(false);
        break;
    }

    return nr_pages;
}

static uint8_t *get_counter_data_by_index(uint8_t index, bool external) {
    uint8_t ctr_page_off;
    uint8_t ctr_page_end;
    uint8_t first_index = 0; // NTAG cards have one counter that is at address 2 so we have to adjust logic

    switch (m_tag_type) {
        case TAG_TYPE_MF0UL11:
            ctr_page_off = MF0UL11_PAGES;
            ctr_page_end = ctr_page_off + MF0ULx1_NUM_CTRS;
            break;
        case TAG_TYPE_MF0UL21:
            ctr_page_off = MF0UL21_PAGES;
            ctr_page_end = ctr_page_off + MF0ULx1_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_210:
            if (external) return NULL; // NTAG 210 tags don't really have a counter
            ctr_page_off = NTAG210_PAGES;
            ctr_page_end = ctr_page_off + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_212:
            if (external) return NULL; // NTAG 212 tags don't really have a counter
            ctr_page_off = NTAG212_PAGES;
            ctr_page_end = ctr_page_off + NTAG_NUM_CTRS;
            break;
        case TAG_TYPE_NTAG_213:
            ctr_page_off = NTAG213_PAGES;
            ctr_page_end = ctr_page_off + NTAG_NUM_CTRS;
            first_index = 2;
            break;
        case TAG_TYPE_NTAG_215:
            ctr_page_off = NTAG215_PAGES;
            ctr_page_end = ctr_page_off + NTAG_NUM_CTRS;
            first_index = 2;
            break;
        case TAG_TYPE_NTAG_216:
            ctr_page_off = NTAG216_PAGES;
            ctr_page_end = ctr_page_off + NTAG_NUM_CTRS;
            first_index = 2;
            break;
        default:
            return NULL;
    }

    if (!external) first_index = 0;

    // check that counter index is in bounds
    if ((index < first_index) || ((index - first_index) >= (ctr_page_end - ctr_page_off))) return NULL;

    return m_tag_information->memory[ctr_page_off + index - first_index];
}

uint8_t *nfc_tag_mf0_ntag_get_counter_data_by_index(uint8_t index) {
    // from the point of chameleon this is an internal access since only NFC accesses are considered external accesses
    return get_counter_data_by_index(index, false);
}

static char hex_digit(int n) {
    if (n < 10) return '0' + n;
    else return 'A' + n - 10;
}

static void bytes2hex(const uint8_t *bytes, char *hex, size_t len) {
    for (size_t i = 0; i < len; i++) {
        *hex++ = hex_digit(bytes[i] >> 4);
        *hex++ = hex_digit(bytes[i] & 0x0F);
    }
}

static void handle_any_read(uint8_t block_num, uint8_t block_cnt, uint8_t block_max) {
    ASSERT(block_cnt <= block_max);
    ASSERT((block_max - block_cnt) >= block_num);

    uint8_t first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);

    // password pages are present on all tags that have config pages
    uint8_t pwd_page = 0;
    if (first_cfg_page != 0) pwd_page = first_cfg_page + CONF_PWD_PAGE_OFFSET;

    // extract mirroring config
    int mirror_page_off = 0;
    int mirror_page_end = 0;
    int mirror_byte_off = 0;
    int mirror_mode = 0;
    int mirror_size = 0;
    uint8_t mirror_buf[MIRROR_UID_CNT_SIZE];
    if (is_ntag()) {
        uint8_t mirror = m_tag_information->memory[first_cfg_page][CONF_MIRROR_BYTE];
        mirror_page_off = m_tag_information->memory[first_cfg_page][CONF_MIRROR_PAGE_BYTE];
        mirror_mode = (mirror & MIRROR_BYTE_CONF_MASK) >> MIRROR_BYTE_CONF_SHIFT;
        mirror_byte_off = (mirror & MIRROR_BYTE_BYTE_MASK) >> MIRROR_BYTE_BYTE_SHIFT;

        // NTAG 210/212 don't have a counter thus no mirror mode
        switch (m_tag_type) {
        case TAG_TYPE_NTAG_210:
        case TAG_TYPE_NTAG_212:
            mirror_mode = MIRROR_CONF_UID;
            break;
        default:
            break;
        }

        if ((mirror_page_off > 3) && (mirror_mode != MIRROR_CONF_DISABLED)) {
            mirror_size = mirror_size_for_mode(mirror_mode);
            int user_data_end = get_user_data_end_by_tag_type(m_tag_type);
            int pages_needed = 
                (mirror_byte_off + mirror_size + (NFC_TAG_MF0_NTAG_DATA_SIZE - 1)) / NFC_TAG_MF0_NTAG_DATA_SIZE;

            if ((pages_needed >= user_data_end) || ((user_data_end - pages_needed) < mirror_page_off)) {
                NRF_LOG_ERROR("invalid mirror config %02x %02x %02x", mirror_page_off, mirror_byte_off, mirror_mode);
                mirror_page_off = 0;
            } else {
                mirror_page_end = mirror_page_off + pages_needed;

                switch (mirror_mode) {
                case MIRROR_CONF_UID:
                    bytes2hex(m_tag_information->res_coll.uid, (char *)mirror_buf, 7);
                    break;
                case MIRROR_CONF_CNT:
                    bytes2hex(get_counter_data_by_index(0, false), (char *)mirror_buf, 3);
                    break;
                case MIRROR_CONF_UID_CNT:
                    bytes2hex(m_tag_information->res_coll.uid, (char *)mirror_buf, 7);
                    mirror_buf[7] = 'x';
                    bytes2hex(get_counter_data_by_index(0, false), (char *)&mirror_buf[8], 3);
                    break;
                }
            }
        }
    }

    for (uint8_t block = 0; block < block_cnt; block++) {
        uint8_t block_to_read = (block_num + block) % block_max;
        uint8_t *tx_buf_ptr = m_tag_tx_buffer.tx_buffer + block * NFC_TAG_MF0_NTAG_DATA_SIZE;

        // In case PWD or PACK pages are read we need to write zero to the output buffer. In UID magic mode we don't care.
        if (m_tag_information->config.mode_uid_magic || (pwd_page == 0) || (block_to_read < pwd_page) || (block_to_read > (pwd_page + 1))) {
            memcpy(tx_buf_ptr, m_tag_information->memory[block_to_read], NFC_TAG_MF0_NTAG_DATA_SIZE);
        } else {
            memset(tx_buf_ptr, 0, NFC_TAG_MF0_NTAG_DATA_SIZE);
        }

        // apply mirroring if needed
        if ((mirror_page_off > 0) && (mirror_size > 0) && (block_to_read >= mirror_page_off) && (block_to_read < mirror_page_end)) {
            // When accessing the first page that includes mirrored data the offset into the mirror buffer is 
            // definitely zero. Later pages need to account for the offset in the first page. Offset in the
            // destination page chunk will be zero however.
            int mirror_buf_off = (block_to_read - mirror_page_off) * NFC_TAG_MF0_NTAG_DATA_SIZE;
            int offset_in_cur_block = mirror_byte_off;
            if (mirror_buf_off != 0) {
                mirror_buf_off -= mirror_byte_off;
                offset_in_cur_block = 0;
            }

            int mirror_copy_size = mirror_size - mirror_buf_off;
            if (mirror_copy_size > NFC_TAG_MF0_NTAG_DATA_SIZE) mirror_copy_size = NFC_TAG_MF0_NTAG_DATA_SIZE;

            // Ensure we don't corrupt memory here.
            ASSERT(offset_in_cur_block < NFC_TAG_MF0_NTAG_DATA_SIZE);
            ASSERT(mirror_buf_off <= sizeof(mirror_buf));
            ASSERT(mirror_copy_size <= (sizeof(mirror_buf) - mirror_buf_off));

            memcpy(&tx_buf_ptr[offset_in_cur_block], &mirror_buf[mirror_buf_off], mirror_copy_size);
        }
    }

    NRF_LOG_DEBUG("READ handled %02x %02x %02x", block_num, block_cnt, block_max);

    // update counter for NTAG cards if needed
    switch (m_tag_type) {
    case TAG_TYPE_NTAG_213:
    case TAG_TYPE_NTAG_215:
    case TAG_TYPE_NTAG_216: if (!m_did_first_read) {
        m_did_first_read = true;

        int first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);
        int access = m_tag_information->memory[first_cfg_page + CONF_ACCESS_PAGE_OFFSET][CONF_ACCESS_BYTE];

        if ((access & CONF_ACCESS_NFC_CNT_EN) != 0) {
            uint8_t *ctr = get_counter_data_by_index(0, false);
            uint32_t counter = (((uint32_t)ctr[0]) << 16) | (((uint32_t)ctr[1]) << 8) | ((uint32_t)ctr[2]);
            if (counter < 0xFFFFFF) counter += 1;
            ctr[0] = (uint8_t)(counter >> 16);
            ctr[1] = (uint8_t)(counter >> 8);
            ctr[2] = (uint8_t)(counter);
        }
        break;
    }
    default:
        break;
    }

    nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, ((int)block_cnt) * NFC_TAG_MF0_NTAG_DATA_SIZE, true);
}

static void handle_read_command(uint8_t block_num) {
    int block_max = get_block_max_by_tag_type(m_tag_type, true);

    NRF_LOG_DEBUG("handling READ %02x %02x", block_num, block_max);

    if (block_num >= block_max) {
        NRF_LOG_WARNING("too large block num %02x >= %02x", block_num, block_max);

        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    handle_any_read(block_num, 4, block_max);
}

static void handle_fast_read_command(uint8_t block_num, uint8_t end_block_num) {
    switch (m_tag_type)
    {
    case TAG_TYPE_MF0UL11:
    case TAG_TYPE_MF0UL21:
    case TAG_TYPE_NTAG_210:
    case TAG_TYPE_NTAG_212:
    case TAG_TYPE_NTAG_213:
    case TAG_TYPE_NTAG_215:
    case TAG_TYPE_NTAG_216:
        // command is supported
        break;
    default:
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    int block_max = get_block_max_by_tag_type(m_tag_type, true);

    if (block_num >= end_block_num || end_block_num >= block_max) {
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    NRF_LOG_INFO("HANDLING FAST READ %02x %02x", block_num, end_block_num);

    handle_any_read(block_num, end_block_num - block_num, block_max);
}

static bool check_ro_lock_on_page(int block_num) {
    if (block_num < 3) return true;
    else if (block_num == 3) return (m_tag_information->memory[2][2] & 9) != 0; // bits 0 and 3
    else if (block_num <= MF0ICU1_PAGES) {
        bool locked = false;

        // check block locking bits
        if (block_num <= 9) locked |= (m_tag_information->memory[2][2] & 2) == 2;
        else locked |= (m_tag_information->memory[2][2] & 4) == 4;

        locked |= (((*(uint16_t *)&m_tag_information->memory[2][2]) >> block_num) & 1) == 1;

        return locked;
    } else {
        uint8_t *p_lock_bytes = NULL;
        int user_memory_end = 0;
        int dyn_lock_bit_page_cnt = 0;
        int index = block_num - MF0ICU1_PAGES;

        switch (m_tag_type) {
        case TAG_TYPE_MF0ICU1:
            return true;
        case TAG_TYPE_MF0ICU2: {
            p_lock_bytes = m_tag_information->memory[MF0ICU2_USER_MEMORY_END];

            if (block_num < MF0ICU2_USER_MEMORY_END) {
                uint8_t byte2 = p_lock_bytes[0];

                // Account for block locking bits first.
                bool locked = (byte2 & (0x10 * (block_num >= 28))) != 0;
                locked |= (byte2 >> (1 + (index / 4) + (block_num >= 28)));
                return locked;
            } else if (block_num == MF0ICU2_USER_MEMORY_END) {
                return false;
            } else if (block_num < MF0ICU2_FIRST_KEY_PAGE) {
                uint8_t byte3 = p_lock_bytes[1];
                return ((byte3 >> (block_num - MF0ICU2_CNT_PAGE)) & 1) != 0;
            } else {
                uint8_t byte3 = p_lock_bytes[1];
                return (byte3 & 0x80) != 0;
            }
        }
        // for the next two we reuse the check for CFGLCK bit used for NTAG
        case TAG_TYPE_MF0UL11:
            ASSERT(block_num >= MF0UL11_USER_MEMORY_END);
            user_memory_end = MF0UL11_USER_MEMORY_END;
            break;
        case TAG_TYPE_MF0UL21: {
            user_memory_end = MF0UL11_USER_MEMORY_END;
            if (block_num < user_memory_end) {
                p_lock_bytes = m_tag_information->memory[MF0UL21_USER_MEMORY_END];
                uint16_t lock_word = (((uint16_t)p_lock_bytes[1]) << 8) | (uint16_t)p_lock_bytes[0];
                bool locked = ((lock_word >> (index / 2)) & 1) != 0;
                locked |= ((p_lock_bytes[2] >> (index / 4)) & 1) != 0;
                return locked;
            }
            break;
        }
        case TAG_TYPE_NTAG_210:
            user_memory_end = NTAG210_USER_MEMORY_END;
            dyn_lock_bit_page_cnt = 0; // NTAG 210 doesn't have dynamic lock bits
            break;
        case TAG_TYPE_NTAG_212:
            user_memory_end = NTAG212_USER_MEMORY_END;
            dyn_lock_bit_page_cnt = 2;
            break;
        case TAG_TYPE_NTAG_213:
            user_memory_end = NTAG213_USER_MEMORY_END;
            dyn_lock_bit_page_cnt = 2;
            break;
        case TAG_TYPE_NTAG_215:
            user_memory_end = NTAG215_USER_MEMORY_END;
            dyn_lock_bit_page_cnt = 16;
            break;
        case TAG_TYPE_NTAG_216:
            user_memory_end = NTAG216_USER_MEMORY_END;
            dyn_lock_bit_page_cnt = 16;
            break;
        default:
            ASSERT(false);
            break;
        }

        if (block_num < user_memory_end) {
            ASSERT(dyn_lock_bit_page_cnt > 0);

            p_lock_bytes = m_tag_information->memory[user_memory_end];
            uint16_t lock_word = (((uint16_t)p_lock_bytes[1]) << 8) | (uint16_t)p_lock_bytes[0];

            bool locked_small_range = ((lock_word >> (index / dyn_lock_bit_page_cnt)) & 1) != 0;
            bool locked_large_range = ((p_lock_bytes[2] >> (index / dyn_lock_bit_page_cnt / 2)) & 1) != 0;

            return locked_small_range | locked_large_range;
        } else {
            // check CFGLCK bit
            int first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);
            uint8_t access = m_tag_information->memory[first_cfg_page + CONF_ACCESS_PAGE_OFFSET][CONF_ACCESS_BYTE];
            if ((access & CONF_ACCESS_CFGLCK) != 0)
                return (block_num >= first_cfg_page) && ((block_num - first_cfg_page) <= 1);
            else
                return false;
        }
    }
}

static int handle_write_command(uint8_t block_num, uint8_t *p_data) {
    int block_max = get_block_max_by_tag_type(m_tag_type, false);

    if (block_num >= block_max) {
        NRF_LOG_ERROR("Write failed: block_num %08x >= block_max %08x", block_num, block_max);
        return NAK_INVALID_OPERATION_TBV;
    }

    // Handle writing based on the current write mode
    if (m_tag_information->config.mode_block_write == NFC_TAG_MF0_NTAG_WRITE_DENIED) {
        // In this mode, reject all write operations
        NRF_LOG_INFO("Write denied due to WRITE_DENIED mode");
        return NAK_INVALID_OPERATION_TBV;
    } 
    else if (m_tag_information->config.mode_block_write == NFC_TAG_MF0_NTAG_WRITE_DECEIVE) {
        // In this mode, pretend to accept the write but don't actually write anything
        NRF_LOG_INFO("Write deceived in WRITE_DECEIVE mode");
        return ACK_VALUE;
    }
    // For NORMAL, SHADOW, and SHADOW_REQ modes, proceed with the write operation

    if (m_tag_information->config.mode_uid_magic) {
        // anything can be written in this mode
        memcpy(m_tag_information->memory[block_num], p_data, NFC_TAG_MF0_NTAG_DATA_SIZE);
        return ACK_VALUE;
    }

    switch (block_num) {
        case 0:
        case 1:
            return NAK_INVALID_OPERATION_TBV;
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
            } else return NAK_INVALID_OPERATION_TBV;
            break;
        default:
            if (!check_ro_lock_on_page(block_num)) {
                memcpy(m_tag_information->memory[block_num], p_data, NFC_TAG_MF0_NTAG_DATA_SIZE);
            } else return NAK_INVALID_OPERATION_TBV;
            break;
    }

    return ACK_VALUE;
}

static void handle_read_cnt_command(uint8_t index) {
    // first check if the counter even exists for external commands
    uint8_t *cnt_data = get_counter_data_by_index(index, true);
    if (cnt_data == NULL) {
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    // deny counter reading when counter password protection is enabled and reader is not authenticated
    if (is_ntag() && !m_tag_information->config.mode_uid_magic) {
        int first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);
        int access = m_tag_information->memory[first_cfg_page + CONF_ACCESS_PAGE_OFFSET][CONF_ACCESS_BYTE];

        if ((access & CONF_ACCESS_NFC_CNT_PWD_PROT) != 0 && !m_tag_authenticated) {
            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
            return;
        }
    }

    memcpy(m_tag_tx_buffer.tx_buffer, cnt_data, 3);
    nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, 3, true);
}

static void handle_incr_cnt_command(uint8_t block_num, uint8_t *p_data) {
    uint8_t ctr_page_off;
    uint8_t ctr_page_end;
    switch (m_tag_type) {
        case TAG_TYPE_MF0UL11:
            ctr_page_off = MF0UL11_PAGES;
            ctr_page_end = ctr_page_off + MF0ULx1_NUM_CTRS;
            break;
        case TAG_TYPE_MF0UL21:
            ctr_page_off = MF0UL21_PAGES;
            ctr_page_end = ctr_page_off + MF0ULx1_NUM_CTRS;
            break;
        default:
            if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
            return;
    }

    // check that counter index is in bounds
    if (block_num >= (ctr_page_end - ctr_page_off)) {
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    uint8_t *cnt_data = m_tag_information->memory[ctr_page_off + block_num];
    uint32_t incr_value = ((uint32_t)p_data[0] << 16) | ((uint32_t)p_data[1] << 8) | ((uint32_t)p_data[2]);
    uint32_t cnt = ((uint32_t)cnt_data[0] << 16) | ((uint32_t)cnt_data[1] << 8) | ((uint32_t)cnt_data[2]);

    if ((0xFFFFFF - cnt) < incr_value) {
        // set tearing event flag
        cnt_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] |= MF0_NTAG_TEARING_MASK_IN_AUTHLIM;
        
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
    } else {
        cnt += incr_value;

        cnt_data[0] = (uint8_t)(cnt >> 16);
        cnt_data[1] = (uint8_t)(cnt >> 8);
        cnt_data[2] = (uint8_t)(cnt & 0xff);

        nfc_tag_14a_tx_nbit(ACK_VALUE, 4);
    }
}

static void handle_pwd_auth_command(uint8_t *p_data) {
    int first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);
    uint8_t *cnt_data = get_counter_data_by_index(0, false);
    if (first_cfg_page == 0 || cnt_data == NULL) {
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        return;
    }

    // check AUTHLIM counter
    uint8_t auth_cnt = cnt_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] & MF0_NTAG_AUTHLIM_MASK_IN_CTR;
    uint8_t auth_lim = m_tag_information->memory[first_cfg_page + 1][0] & CONF_ACCESS_AUTHLIM_MASK;
    if ((auth_lim > 0) && (auth_lim <= auth_cnt)) {
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
        return;
    }

    uint32_t pwd = *(uint32_t *)m_tag_information->memory[first_cfg_page + CONF_PWD_PAGE_OFFSET];
    uint32_t supplied_pwd = *(uint32_t *)&p_data[1];
    if (pwd != supplied_pwd) {
        if (auth_lim) {
            cnt_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] &= ~MF0_NTAG_AUTHLIM_MASK_IN_CTR;
            cnt_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] |= (auth_cnt + 1) & MF0_NTAG_AUTHLIM_MASK_IN_CTR;
        }
        nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
        return;
    }

    // reset authentication attempts counter and authenticate user
    cnt_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] &= ~MF0_NTAG_AUTHLIM_MASK_IN_CTR;
    m_tag_authenticated = true; // TODO: this should be possible to reset somehow

    // Send the PACK value back
    nfc_tag_14a_tx_bytes(m_tag_information->memory[first_cfg_page + CONF_PACK_PAGE_OFFSET], 2, true);
}

static void handle_check_tearing_event(int index) {
    switch (m_tag_type) {
    case TAG_TYPE_MF0UL11:
    case TAG_TYPE_MF0UL21: {
        uint8_t *ctr_data = get_counter_data_by_index(index, true);

        if (ctr_data) {
            m_tag_tx_buffer.tx_buffer[0] = (ctr_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] & MF0_NTAG_TEARING_MASK_IN_AUTHLIM) == 0 ? 0xBD : 0x00;
            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, 1, true);
        } else {
            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        }

        break;
    }
    default:
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        break;
    }
}

static void handle_vcsl_command(uint16_t szDataBits) {
    switch (m_tag_type) {
    case TAG_TYPE_MF0UL11:
    case TAG_TYPE_MF0UL21:
        if (szDataBits < 168) {
            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
            break;
        }
        break;
    default:
        if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
        break;
    }
    
    int first_cfg_page = get_first_cfg_page_by_tag_type(m_tag_type);
    m_tag_tx_buffer.tx_buffer[0] = m_tag_information->memory[first_cfg_page + CONF_VCTID_PAGE_OFFSET][CONF_VCTID_PAGE_BYTE];

    nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_buffer, 1, true);
}

static void nfc_tag_mf0_ntag_state_handler(uint8_t *p_data, uint16_t szDataBits) {
    uint8_t command = p_data[0];
    uint8_t block_num = p_data[1];

    if (szDataBits < 16) return;

    NRF_LOG_INFO("received mfu command %x of size %u bits", command, szDataBits);

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
            handle_fast_read_command(block_num, end_block_num);
            break;
        }
        case CMD_WRITE:
        case CMD_COMPAT_WRITE: {
            int resp = handle_write_command(block_num, &p_data[2]);
            nfc_tag_14a_tx_nbit(resp, 4);
            break;
        }
        case CMD_PWD_AUTH: {
            handle_pwd_auth_command(p_data);
            break;
        }
        case CMD_READ_SIG:
            handle_read_sig_command();
            break;
        case CMD_READ_CNT:
            handle_read_cnt_command(block_num);
            break;
        case CMD_INCR_CNT:
            handle_incr_cnt_command(block_num, &p_data[2]);
            break;
        case CMD_CHECK_TEARING_EVENT:
            handle_check_tearing_event(block_num);
            break;
        case CMD_VCSL: {
            handle_vcsl_command(szDataBits);
            break;
        }
        default:
            if (is_ntag()) nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBV, 4);
            break;
    }
    return;
}

nfc_tag_14a_coll_res_reference_t *nfc_tag_mf0_ntag_get_coll_res() {
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
    m_tag_authenticated = false;
    m_did_first_read = false;
}

static int get_information_size_by_tag_type(tag_specific_type_t type) {
    return sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_mf0_ntag_configure_t) + (get_total_pages_by_tag_type(type) * NFC_TAG_MF0_NTAG_DATA_SIZE);
}

/** @brief MF0/NTAG callback before saving data
 * @param type detailed label type
 * @param buffer data buffer
 * @return to be saved, the length of the data that needs to be saved, it means not saved when 0
 */
int nfc_tag_mf0_ntag_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (m_tag_type != TAG_TYPE_UNDEFINED && m_tag_information != NULL) {
        // Add shadow mode handling
        if (m_tag_information->config.mode_block_write == NFC_TAG_MF0_NTAG_WRITE_SHADOW) {
            NRF_LOG_INFO("The mf0/ntag is in shadow write mode.");
            return 0;
        }
        if (m_tag_information->config.mode_block_write == NFC_TAG_MF0_NTAG_WRITE_SHADOW_REQ) {
            NRF_LOG_INFO("The mf0/ntag will be set to shadow write mode.");
            m_tag_information->config.mode_block_write = NFC_TAG_MF0_NTAG_WRITE_SHADOW;
        }
        // Save the corresponding size data according to the current label type
        return get_information_size_by_tag_type(type);
    } else {
        ASSERT(false);
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
            .get_coll_res = nfc_tag_mf0_ntag_get_coll_res,
            .cb_state = nfc_tag_mf0_ntag_state_handler,
            .cb_reset = nfc_tag_mf0_ntag_reset_handler,
        };
        nfc_tag_14a_set_handler(&handler_for_14a);
        NRF_LOG_INFO("HF ntag data load finish.");
    } else {
        ASSERT(buffer->length == info_size);
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

    memset(p_ntag_information, 0, sizeof(nfc_tag_mf0_ntag_information_max_t));

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

    int first_cfg_page = get_first_cfg_page_by_tag_type(tag_type);
    if (first_cfg_page != 0) {
        p_ntag_information->memory[first_cfg_page][CONF_AUTH0_BYTE] = 0xFF; // set AUTH to 0xFF
        *(uint32_t *)p_ntag_information->memory[first_cfg_page + CONF_PWD_PAGE_OFFSET] = 0xFFFFFFFF; // set PWD to FFFFFFFF

        switch (tag_type) {
        case TAG_TYPE_MF0UL11:
        case TAG_TYPE_MF0UL21:
            p_ntag_information->memory[first_cfg_page + 1][1] = 0x05; // set VCTID to 0x05
            break;
        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216:
            p_ntag_information->memory[first_cfg_page][0] = 0x04; // set MIRROR to 0x04 (STRG_MOD_EN to 1)
            break;
        default:
            ASSERT(false);
            break;
        }
    }

    int version_page = get_version_page_by_tag_type(tag_type);
    if (version_page > 0) {
        uint8_t *version_data = &p_ntag_information->memory[version_page][0];

        switch (m_tag_type) {
        case TAG_TYPE_MF0UL11:
            version_data[6] = MF0UL11_VERSION_STORAGE_SIZE;
            version_data[2] = MF0ULx1_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_MF0UL21:
            version_data[6] = MF0UL21_VERSION_STORAGE_SIZE;
            version_data[2] = MF0ULx1_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_210:
            version_data[6] = NTAG210_VERSION_STORAGE_SIZE;
            version_data[2] = NTAG_VERSION_PRODUCT_TYPE;
            version_data[3] = VERSION_PRODUCT_SUBTYPE_17pF;
            break;
        case TAG_TYPE_NTAG_212:
            version_data[6] = NTAG212_VERSION_STORAGE_SIZE;
            version_data[2] = NTAG_VERSION_PRODUCT_TYPE;
            version_data[3] = VERSION_PRODUCT_SUBTYPE_17pF;
            break;
        case TAG_TYPE_NTAG_213:
            version_data[6] = NTAG213_VERSION_STORAGE_SIZE;
            version_data[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_215:
            version_data[6] = NTAG215_VERSION_STORAGE_SIZE;
            version_data[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        case TAG_TYPE_NTAG_216:
            version_data[6] = NTAG216_VERSION_STORAGE_SIZE;
            version_data[2] = NTAG_VERSION_PRODUCT_TYPE;
            break;
        default:
            ASSERT(false);
            break;
        }

        version_data[0] = VERSION_FIXED_HEADER;
        version_data[1] = VERSION_VENDOR_ID;
        if (version_data[3] == 0) version_data[3] = VERSION_PRODUCT_SUBTYPE_50pF; // TODO: make configurable for MF0ULx1
        version_data[4] = VERSION_MAJOR_PRODUCT;
        version_data[5] = VERSION_MINOR_PRODUCT;
        version_data[7] = VERSION_PROTOCOL_TYPE;
    }

    int signature_page = get_signature_page_by_tag_type(tag_type);
    if (signature_page > 0) {
        memset(&p_ntag_information->memory[signature_page][0], 0, NFC_TAG_MF0_NTAG_SIG_SIZE);
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
    p_ntag_information->config.mode_block_write = NFC_TAG_MF0_NTAG_WRITE_NORMAL;

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

void nfc_tag_mf0_ntag_set_write_mode(nfc_tag_mf0_ntag_write_mode_t write_mode) {
    if (m_tag_type == TAG_TYPE_UNDEFINED || m_tag_information == NULL) return;
    
    if (write_mode == NFC_TAG_MF0_NTAG_WRITE_SHADOW) {
        write_mode = NFC_TAG_MF0_NTAG_WRITE_SHADOW_REQ;
    }
    m_tag_information->config.mode_block_write = write_mode;
}

nfc_tag_mf0_ntag_write_mode_t nfc_tag_mf0_ntag_get_write_mode(void) {
    if (m_tag_type == TAG_TYPE_UNDEFINED || m_tag_information == NULL) 
        return NFC_TAG_MF0_NTAG_WRITE_NORMAL;
    
    return m_tag_information->config.mode_block_write;
}
