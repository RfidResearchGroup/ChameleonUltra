#ifndef NFC_NTAG_H
#define NFC_NTAG_H

#include "nfc_14a.h"

#define NFC_TAG_MF0_NTAG_DATA_SIZE   4
#define NFC_TAG_MF0_NTAG_SIG_SIZE   32
#define NFC_TAG_MF0_NTAG_VER_SIZE    8
#define NFC_TAG_MF0_NTAG_SIG_PAGES   (NFC_TAG_MF0_NTAG_SIG_SIZE / NFC_TAG_MF0_NTAG_DATA_SIZE)
#define NFC_TAG_MF0_NTAG_VER_PAGES   (NFC_TAG_MF0_NTAG_VER_SIZE / NFC_TAG_MF0_NTAG_DATA_SIZE)

#define NFC_TAG_MF0_FRAME_SIZE (16 + NFC_TAG_14A_CRC_LENGTH)
#define NFC_TAG_MF0_BLOCK_MAX   41

#define MF0ULx1_NUM_CTRS 3 // number of Ultralight EV1 one-way counters
#define NTAG_NUM_CTRS 1 // number of NTAG one-way counters

#define MF0ULx1_EXTRA_PAGES (MF0ULx1_NUM_CTRS + NFC_TAG_MF0_NTAG_VER_PAGES + NFC_TAG_MF0_NTAG_SIG_PAGES)
#define NTAG_EXTRA_PAGES (NTAG_NUM_CTRS + NFC_TAG_MF0_NTAG_VER_PAGES + NFC_TAG_MF0_NTAG_SIG_PAGES)

#define NTAG210_PAGES 20 //20 pages total for ntag210, from 0 to 44
#define NTAG210_TOTAL_PAGES (NTAG210_PAGES + NTAG_EXTRA_PAGES) // 1 more page for the counter
#define NTAG212_PAGES 41 //41 pages total for ntag212, from 0 to 44
#define NTAG212_TOTAL_PAGES (NTAG212_PAGES + NTAG_EXTRA_PAGES) // 1 more page for the counter
#define NTAG213_PAGES 45 //45 pages total for ntag213, from 0 to 44
#define NTAG213_TOTAL_PAGES (NTAG213_PAGES + NTAG_EXTRA_PAGES) // 1 more page for the counter
#define NTAG215_PAGES 135 //135 pages total for ntag215, from 0 to 134
#define NTAG215_TOTAL_PAGES (NTAG215_PAGES + NTAG_EXTRA_PAGES) // 1 more page for the counter
#define NTAG216_PAGES 231 //231 pages total for ntag216, from 0 to 230
#define NTAG216_TOTAL_PAGES (NTAG216_PAGES + NTAG_EXTRA_PAGES) // 1 more page for the counter

#define MF0ICU1_PAGES 16 //16 pages total for MF0ICU1 (the original UL), from 0 to 15
#define MF0ICU2_PAGES 36 //16 pages total for MF0ICU2 (UL C), from 0 to 35
#define MF0UL11_PAGES 20 //20 pages total for MF0UL11 (UL EV1), from 0 to 19
#define MF0UL11_TOTAL_PAGES (MF0UL11_PAGES + MF0ULx1_EXTRA_PAGES) // 3 more pages for 3 one way counters
#define MF0UL21_PAGES 41 //231 pages total for MF0UL21 (UL EV1), from 0 to 40
#define MF0UL21_TOTAL_PAGES (MF0UL21_PAGES + MF0ULx1_EXTRA_PAGES) // 3 more pages for 3 one way counters

#define NFC_TAG_NTAG_FRAME_SIZE 64
#define NFC_TAG_NTAG_BLOCK_MAX   NTAG216_TOTAL_PAGES

// Since all counters are 24-bit and each currently supported tag that supports counters
// has password authentication we store the auth attempts counter in the last bit of the
// first counter. AUTHLIM is only 3 bits though so we reserve 4 bits just to be sure and
// use the top bit for tearing event flag.
#define MF0_NTAG_AUTHLIM_OFF_IN_CTR                 3
#define MF0_NTAG_AUTHLIM_MASK_IN_CTR              0xF
#define MF0_NTAG_TEARING_MASK_IN_AUTHLIM         0x80

//MF0/NTAG label writing mode
typedef enum {
    NFC_TAG_MF0_NTAG_WRITE_NORMAL     =   0u,
    NFC_TAG_MF0_NTAG_WRITE_DENIED     =   1u,
    NFC_TAG_MF0_NTAG_WRITE_DECEIVE    =   2u,
    NFC_TAG_MF0_NTAG_WRITE_SHADOW     =   3u,
    NFC_TAG_MF0_NTAG_WRITE_SHADOW_REQ =   4u,
} nfc_tag_mf0_ntag_write_mode_t;

// M0/NTAG password authentication log for key collection
typedef struct {
    uint8_t pwd[4];     // Password used in authentication
} PACKED nfc_tag_mf0_ntag_auth_log_t;

typedef struct {
    uint8_t mode_uid_magic: 1;
    // New field for write mode
    nfc_tag_mf0_ntag_write_mode_t mode_block_write: 3;
    // Enable key detection (password logging)
    uint8_t detection_enable: 1;
    // reserve remaining bits
    uint8_t reserved1: 3;
    uint8_t reserved2;
    uint8_t reserved3;
} nfc_tag_mf0_ntag_configure_t;

typedef struct __attribute__((aligned(4))) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    nfc_tag_mf0_ntag_configure_t config;
    uint8_t memory[][NFC_TAG_MF0_NTAG_DATA_SIZE];
}
nfc_tag_mf0_ntag_information_t;

typedef struct {
    // TX buffer must fit the largest possible frame size.
    // TODO: This size should be decreased as the maximum allowed frame size is 257 (see 6.14.13.36 in datasheet).
    uint8_t tx_buffer[NFC_TAG_NTAG_BLOCK_MAX * NFC_TAG_MF0_NTAG_DATA_SIZE];
} nfc_tag_mf0_ntag_tx_buffer_t;

int nfc_tag_mf0_ntag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int nfc_tag_mf0_ntag_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_mf0_ntag_data_factory(uint8_t slot, tag_specific_type_t tag_type);
int nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(tag_specific_type_t tag_type);
uint8_t *nfc_tag_mf0_ntag_get_counter_data_by_index(uint8_t index);
uint8_t *nfc_tag_mf0_ntag_get_version_data(void);
uint8_t *nfc_tag_mf0_ntag_get_signature_data(void);
nfc_tag_14a_coll_res_reference_t *nfc_tag_mf0_ntag_get_coll_res(void);

int nfc_tag_mf0_ntag_get_uid_mode(void);
bool nfc_tag_mf0_ntag_set_uid_mode(bool enabled);
void nfc_tag_mf0_ntag_set_write_mode(nfc_tag_mf0_ntag_write_mode_t write_mode);
nfc_tag_mf0_ntag_write_mode_t nfc_tag_mf0_ntag_get_write_mode(void);

nfc_tag_mf0_ntag_auth_log_t *mf0_get_auth_log(uint32_t *count);
void nfc_tag_mf0_ntag_set_detection_enable(bool enable);
bool nfc_tag_mf0_ntag_is_detection_enable(void);
void nfc_tag_mf0_ntag_detection_log_clear(void);
uint32_t nfc_tag_mf0_ntag_detection_log_count(void);

#endif
