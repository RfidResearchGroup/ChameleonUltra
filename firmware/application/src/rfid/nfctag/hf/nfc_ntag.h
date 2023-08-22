#ifndef NFC_NTAG_H
#define NFC_NTAG_H

#include "nfc_14a.h"

#define NFC_TAG_NTAG_DATA_SIZE   4
#define NFC_TAG_NTAG_FRAME_SIZE 64
#define NFC_TAG_NTAG_BLOCK_MAX   231

#define NTAG213_PAGES 45 //45 pages total for ntag213, from 0 to 44
#define NTAG215_PAGES 135 //135 pages total for ntag215, from 0 to 134
#define NTAG216_PAGES 231 //231 pages total for ntag216, from 0 to 230


typedef struct {
    uint8_t mode_uid_magic: 1;
    uint8_t detection_enable: 1;
    // 保留
    uint8_t reserved1: 5;
    uint8_t reserved2;
    uint8_t reserved3;
} nfc_tag_ntag_configure_t;

typedef struct __attribute__((aligned(4))) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    nfc_tag_ntag_configure_t config;
    uint8_t memory[NFC_TAG_NTAG_BLOCK_MAX][NFC_TAG_NTAG_DATA_SIZE];
}
nfc_tag_ntag_information_t;

typedef struct {
    uint8_t tx_buffer[NFC_TAG_NTAG_FRAME_SIZE];
} nfc_tag_ntag_tx_buffer_t;

int nfc_tag_ntag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int nfc_tag_ntag_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_ntag_data_factory(uint8_t slot, tag_specific_type_t tag_type);

#endif
