#ifndef NFC_NTAG_H
#define NFC_NTAG_H

#include "nfc_14a.h"

#define NFC_TAG_MF0_NTAG_DATA_SIZE   4

#define NFC_TAG_NTAG_FRAME_SIZE 64
#define NFC_TAG_NTAG_BLOCK_MAX   231

#define NFC_TAG_MF0_FRAME_SIZE (16 + NFC_TAG_14A_CRC_LENGTH)
#define NFC_TAG_MF0_BLOCK_MAX   41

#define MF0ULx1_NUM_CTRS 3 // number of Ultralight EV1 one-way counters

#define NTAG213_PAGES 45 //45 pages total for ntag213, from 0 to 44
#define NTAG213_PAGES_WITH_CTR (NTAG213_PAGES + 1) // 1 more page for the counter
#define NTAG215_PAGES 135 //135 pages total for ntag215, from 0 to 134
#define NTAG215_PAGES_WITH_CTR (NTAG215_PAGES + 1) // 1 more page for the counter
#define NTAG216_PAGES 231 //231 pages total for ntag216, from 0 to 230
#define NTAG216_PAGES_WITH_CTR (NTAG216_PAGES + 1) // 1 more page for the counter

#define MF0ICU1_PAGES 16 //16 pages total for MF0ICU1 (the original UL), from 0 to 15
#define MF0ICU2_PAGES 36 //16 pages total for MF0ICU2 (UL C), from 0 to 35
#define MF0UL11_PAGES 20 //20 pages total for MF0UL11 (UL EV1), from 0 to 19
#define MF0UL11_PAGES_WITH_CTRS (MF0UL11_PAGES + MF0ULx1_NUM_CTRS) // 3 more pages for 3 one way counters
#define MF0UL21_PAGES 41 //231 pages total for MF0UL21 (UL EV1), from 0 to 40
#define MF0UL21_PAGES_WITH_CTRS (MF0UL21_PAGES + MF0ULx1_NUM_CTRS) // 3 more pages for 3 one way counters


typedef struct {
    uint8_t mode_uid_magic: 1;
    // reserve
    uint8_t reserved1: 7;
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

int nfc_tag_mf0_ntag_get_uid_mode(void);
bool nfc_tag_mf0_ntag_set_uid_mode(bool enabled);

#endif
