#ifndef NFC_SEOS_H
#define NFC_SEOS_H

#include "nfc_14a.h"
#include "tag_emulation.h"

#define NFC_TAG_SEOS_DATA_MAX 255
#define NFC_TAG_SEOS_OID_MAX 32
#define NFC_TAG_SEOS_DATA_TAG_MAX 2
#define NFC_TAG_SEOS_DIVERSIFIER_MAX 16

/**
 * Per-slot persistent data layout stored in FDS flash.
 */
typedef struct __attribute__((packed)) {
    nfc_tag_14a_coll_res_entity_t res_coll;

    uint8_t data[NFC_TAG_SEOS_DATA_MAX];
    uint8_t data_len;
    
    uint8_t oid[NFC_TAG_SEOS_OID_MAX];
    uint8_t oid_len;

    uint8_t data_tag[NFC_TAG_SEOS_DATA_TAG_MAX];
    uint8_t data_tag_len;

    uint8_t diversifier[NFC_TAG_SEOS_DIVERSIFIER_MAX];
    uint8_t diversifier_len;

    uint8_t hash_alg;
    uint8_t encr_alg;

    // Keys
    uint8_t authkey[16];
    uint8_t privenc[16];
    uint8_t privmac[16];
}
nfc_tag_seos_information_t;

/* Anti-collision resource — used by get_coll_res_data in app_cmd.c */
nfc_tag_14a_coll_res_reference_t *nfc_tag_seos_get_coll_res(void);

/* tag_base_map callbacks */
int  nfc_tag_seos_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int  nfc_tag_seos_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_seos_data_factory(uint8_t slot, tag_specific_type_t tag_type);

#endif