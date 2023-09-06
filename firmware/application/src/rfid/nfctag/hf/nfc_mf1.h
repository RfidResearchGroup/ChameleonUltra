#ifndef NFC_MF1_H
#define NFC_MF1_H

#include "nfc_14a.h"

// Exchange space for time.
// Fast simulate enable(Implement By ChameleonMini Repo)
// #define NFC_MF1_FAST_SIM

#define NFC_TAG_MF1_DATA_SIZE   16
#define NFC_TAG_MF1_FRAME_SIZE  (NFC_TAG_MF1_DATA_SIZE + NFC_TAG_14A_CRC_LENGTH)
#define NFC_TAG_MF1_BLOCK_MAX   256


//MF1 label writing mode
typedef enum {
    NFC_TAG_MF1_WRITE_NORMAL    = 0u,
    NFC_TAG_MF1_WRITE_DENIED    = 1u,
    NFC_TAG_MF1_WRITE_DECEIVE   = 2u,
    NFC_TAG_MF1_WRITE_SHADOW    = 3u,
} nfc_tag_mf1_write_mode_t;

// MF1 tag Gen1a mode state machine
typedef enum {
    GEN1A_STATE_DISABLE,
    GEN1A_STATE_UNLOCKING,
    GEN1A_STATE_UNLOCKED_RW_WAIT,
    GEN1A_STATE_WRITING,
} nfc_tag_mf1_gen1a_state_machine_t;

// MF1 label standard mode state machine
typedef enum {
    // Verification state machine
    MF1_STATE_UNAUTHENTICATED,
    MF1_STATE_AUTHENTICATING,
    MF1_STATE_AUTHENTICATED,

    // Operating state machine
    MF1_STATE_WRITE,
    MF1_STATE_INCREMENT,
    MF1_STATE_DECREMENT,
    MF1_STATE_RESTORE
} nfc_tag_mf1_std_state_machine_t;

// MF1 configuration
typedef struct {
    /**
     * Normal write mode (write normally according to the current state, affected by the control bit and the back door card)
     * Deny write mode (similar to control bit lock, directly reject any write, return nack)
     * Fraudulent writing mode (on the surface, returning ack indicates that the writing is successful, but in fact, even RAM is not written)
     * Shadow write mode (write to RAM, and return ack to indicate success, but not save to flash)
     *  @see nfc_tag_mf1_write_mode_t
     */
    nfc_tag_mf1_write_mode_t mode_block_write;
    /**
     * In communication mode, if the interoperability mode is enabled, some information in the M1 sector data will be used
     * Otherwise, the information defined by the anti -collision phase will be used alone. This setting is limited to 4 bytes NFC_TAG_14A_UID_SINGLE_SIZE.
     * Unless there are any documents that indicate 0 blocks of 7 -bytes and 10 -byte cards with relevant SAK specifications
     */
    uint8_t use_mf1_coll_res: 1;
    /**
     * Chinese Gen1A back door card mode, the highest permissions of this mode
     * After turning on, the response of the back door card operation instruction, and all operations are released directly, not affected by the Mode_block_write and control bit
     */
    uint8_t mode_gen1a_magic: 1;
    /**
     * Make detection, it will automatically record the verification log of MF1
     */
    uint8_t detection_enable: 1;
    // Allow to write block 0 (CUID/gen2 mode)
    uint8_t mode_gen2_magic: 1;
    // reserve
    uint8_t reserved1: 4;
    uint8_t reserved2;
    uint8_t reserved3;
} nfc_tag_mf1_configure_t;

/*
 * MF1 label information structure, keep in mind the 4 -byte alignment
 * If the byte alignment is not performed, the abnormalities of visiting the cross -border will occur when the structure is directly stated and saved directly to the Flash
 */
typedef struct __attribute__((aligned(4))) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    nfc_tag_mf1_configure_t config;
    uint8_t memory[NFC_TAG_MF1_BLOCK_MAX][NFC_TAG_MF1_DATA_SIZE];
}
nfc_tag_mf1_information_t;

// Manufacturer block structure
typedef struct {
    // For example:
    // 30928E04 28 08 0400 0177A2CC35AFA51D
    uint8_t uid[4];
    uint8_t bcc[1];
    uint8_t sak[1];
    uint8_t atqa[2];
    uint8_t manufacturer[8];
} nfc_tag_mf1_factory_info_t;

// General MF1 sector rear block data structure
typedef struct {
    uint8_t key_a[6];    // Secret A
    uint8_t acs[4];     // Control position
    uint8_t key_b[6];    // Secret B
} nfc_tag_mf1_trailer_info_t;

// Send buffer dedicated to miFare communication
typedef struct {
    // Primitive buffer, used to carry any unblocked instructions
    uint8_t tx_raw_buffer[NFC_TAG_MF1_FRAME_SIZE];
    // After Crypto1 encrypted, each byte of the puppet test is
    uint8_t tx_bit_parity[NFC_TAG_MF1_FRAME_SIZE];
    // Used to carry data after Crypto1 encrypted data and Parity merged data
    // The maximum frame length is 163 bits (16 data bytes + 2 CRC bytes = 16 * 9 + 2 * 9 + 1 start bit).
    uint8_t tx_warp_frame[21];
    // The length of the data after packing, according to the above news, it can be seen that the maximum number of BITs of MF1 communication does
    // Therefore, a byte is sufficient to store the length value
    uint8_t tx_frame_bit_size;
} nfc_tag_mf1_tx_buffer_t;

// MF1 label verification history
typedef struct {
    // Basic information of verification
    struct {
        uint8_t block;
        uint8_t is_key_b: 1;
        uint8_t is_nested: 1;
        // Airspace, occupying positions
        uint8_t : 6;
    } cmd;
    // MFKEY32 necessary parameters
    uint8_t uid[4];
    uint8_t nt[4];
    uint8_t nr[4];
    uint8_t ar[4];
} nfc_tag_mf1_auth_log_t;


nfc_tag_mf1_auth_log_t *get_mf1_auth_log(uint32_t *count);
int nfc_tag_mf1_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int nfc_tag_mf1_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_mf1_data_factory(uint8_t slot, tag_specific_type_t tag_type);
void nfc_tag_mf1_set_detection_enable(bool enable);
bool nfc_tag_mf1_is_detection_enable(void);
void nfc_tag_mf1_detection_log_clear(void);
uint32_t nfc_tag_mf1_detection_log_count(void);
nfc_tag_14a_coll_res_reference_t *get_mifare_coll_res(void);
nfc_tag_14a_coll_res_reference_t *get_saved_mifare_coll_res(void);
void nfc_tag_mf1_set_gen1a_magic_mode(bool enable);
bool nfc_tag_mf1_is_gen1a_magic_mode(void);
void nfc_tag_mf1_set_gen2_magic_mode(bool enable);
bool nfc_tag_mf1_is_gen2_magic_mode(void);
void nfc_tag_mf1_set_use_mf1_coll_res(bool enable);
bool nfc_tag_mf1_is_use_mf1_coll_res(void);
void nfc_tag_mf1_set_write_mode(nfc_tag_mf1_write_mode_t write_mode);
nfc_tag_mf1_write_mode_t nfc_tag_mf1_get_write_mode(void);


#endif
