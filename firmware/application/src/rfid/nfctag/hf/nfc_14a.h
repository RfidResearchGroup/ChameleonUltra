#ifndef NFC_14A_H
#define NFC_14A_H

#include "tag_emulation.h"

#define MAX_NFC_RX_BUFFER_SIZE  257
#define MAX_NFC_TX_BUFFER_SIZE  64

#define NFC_TAG_14A_CRC_LENGTH  2

// Whether to automatically remove the coupling school test (hardware removed)
#define NFC_TAG_14A_RX_PARITY_AUTO_DEL_ENABLE  0

#define NFC_TAG_14A_CASCADE_CT  0x88

#define NFC_TAG_14A_CMD_REQA    0x26
#define NFC_TAG_14A_CMD_WUPA    0x52
#define NFC_TAG_14A_CMD_HALT    0x50
#define NFC_TAG_14A_CMD_RATS    0xE0

#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_1  0x93
#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_2  0x95
#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_3  0x97


// TBV = Transfer Buffer Valid
// TBIV = Transfer Buffer Invalid
#define ACK_NAK_FRAME_SIZE          4         /* Bits */
#define ACK_VALUE                   0x0A
#define NAK_INVALID_OPERATION_TBV   0x00    //This is not commonly used
#define NAK_CRC_PARITY_ERROR_TBV    0x01    //This is not commonly used
#define NAK_INVALID_OPERATION_TBIV  0x04
#define NAK_CRC_PARITY_ERROR_TBIV   0x05
#define NAK_OTHER_ERROR             0x06    // This is not defined in the manual, it belongs to the color -changing dragon special (may need the sector)


// ISO14443-A Universal state machine
typedef enum {
    NFC_TAG_STATE_14A_IDLE,     // Leisure, you can wait for any instructions
    NFC_TAG_STATE_14A_READY,    // Select card status, currently the standard 14A anti -rushing collision
    NFC_TAG_STATE_14A_ACTIVE,   // Select cards or other instructions to enter the working status, which can receive all data
    NFC_TAG_STATE_14A_HALTED,   // The label stops working status and can only be awakened by Halt or other special instructions (non -labels)
} nfc_tag_14a_state_t;

// UID of the length in the enumeration specification
typedef enum {
    NFC_TAG_14A_UID_SINGLE_SIZE = 4u,   ///< Length of single-size NFCID1.
    NFC_TAG_14A_UID_DOUBLE_SIZE = 7u,   ///< Length of double-size NFCID1.
    NFC_TAG_14A_UID_TRIPLE_SIZE = 10u,  ///< Length of triple-size NFCID1.
} nfc_tag_14a_uid_size;

// Cascade levels
typedef enum {
    NFC_TAG_14A_CASCADE_LEVEL_1,
    NFC_TAG_14A_CASCADE_LEVEL_2,
    NFC_TAG_14A_CASCADE_LEVEL_3,
} nfc_tag_14a_cascade_level_t;

// ATS packaging structure
typedef struct {
    uint8_t data[0xFF];
    uint8_t length;
} nfc_14a_ats_t;

// Bit -based anti -bumps need to use the resource entity that needs to be used, occupying a large space
typedef struct {
    nfc_tag_14a_uid_size size;          // UID length
    uint8_t atqa[2];                    // atqa
    uint8_t sak[1];                     // sak
    uint8_t uid[10];                    // uid,The largest ten bytes
    nfc_14a_ats_t ats;
} nfc_tag_14a_coll_res_entity_t;

// Calculation of anti -conflict resources, pure quoting space occupation is relatively small
typedef struct {
    nfc_tag_14a_uid_size *size;
    uint8_t *atqa;
    uint8_t *sak;
    uint8_t *uid;
    nfc_14a_ats_t *ats;
} nfc_tag_14a_coll_res_reference_t;

// Communication reception function that needs to be implemented
typedef void (*nfc_tag_14a_reset_handler_t)(void);
typedef void (*nfc_tag_14a_state_handler_t)(uint8_t *data, uint16_t szBits);
typedef nfc_tag_14a_coll_res_reference_t *(*nfc_tag_14a_coll_handler_t)(void);

// The interface that 14A communication receiver needs to be implemented
typedef struct {
    nfc_tag_14a_reset_handler_t cb_reset;
    nfc_tag_14a_state_handler_t cb_state;
    nfc_tag_14a_coll_handler_t get_coll_res;
} nfc_tag_14a_handler_t;

// Different or verification code
void nfc_tag_14a_create_bcc(uint8_t *pbtData, size_t szLen, uint8_t *pbtBcc);
void nfc_tag_14a_append_bcc(uint8_t *pbtData, size_t szLen);

// 14A cycle redundant school code
void nfc_tag_14a_append_crc(uint8_t *pbtData, size_t szLen);
bool nfc_tag_14a_checks_crc(uint8_t *pbtData, size_t szLen);

// 14A frame combination
uint8_t nfc_tag_14a_wrap_frame(const uint8_t *pbtTx, const size_t szTxBits, const uint8_t *pbtTxPar, uint8_t *pbtFrame);
uint8_t nfc_tag_14a_unwrap_frame(const uint8_t *pbtFrame, const size_t szFrameBits, uint8_t *pbtRx, uint8_t *pbtRxPar);

// 14A communication control
void nfc_tag_14a_sense_switch(bool enable);
void nfc_tag_14a_set_handler(nfc_tag_14a_handler_t *handler);
void nfc_tag_14a_set_state(nfc_tag_14a_state_t state);
void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool appendCrc);
void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits);
void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits);

// Determine whether it is an effective UID length
bool is_valid_uid_size(uint8_t uid_length);

#endif
