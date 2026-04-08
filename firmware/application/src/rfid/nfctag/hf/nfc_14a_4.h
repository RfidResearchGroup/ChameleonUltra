/**
 * @file nfc_14a_4.h
 * @brief ISO14443-4 T=CL emulation for ChameleonUltra
 *
 * Implements a full ISO14443-4 tag emulator:
 *   - I-blocks (information, chaining, CID)
 *   - R-blocks (ACK/NAK retransmit)
 *   - S-blocks (WTX to keep reader alive, DESELECT)
 *   - Static APDU response table (pre-loaded before field, no USB needed
 *     during field exchange)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NFC_14A_4_H
#define NFC_14A_4_H

#include "nfc_14a.h"
#include "tag_emulation.h"

/* Maximum APDU size (FSCI=8 → FSC=256, minus PCB+CRC = 253) */
#define NFC_14A_4_MAX_APDU  260  /* max APDU in RAM; flash entries capped at 253 */

/* Static APDU response table — up to 12 pre-configured command/response pairs.
 * Loaded before field activation; firmware responds autonomously without USB. */
#define NFC_14A_4_MAX_STATIC_RESPONSES  12
#define NFC_14A_4_MAX_LARGE_RESPONSES   4    /* RAM-only, for resp > 253 bytes   */
#define NFC_14A_4_MAX_LARGE_RESP_LEN    260  /* max large response size          */
#define NFC_14A_4_MAX_STATIC_CMD_LEN    16
#define NFC_14A_4_MAX_STATIC_RESP_LEN   253  /* max bytes in flash-backed slot    */

typedef struct __attribute__((packed)) {
    uint8_t cmd_len;
    uint8_t cmd[NFC_14A_4_MAX_STATIC_CMD_LEN];
    uint8_t resp_len;
    uint8_t resp[NFC_14A_4_MAX_STATIC_RESP_LEN];
} nfc_tag_14a_4_static_response_t;

/**
 * Per-slot persistent data layout stored in FDS flash.
 * Anti-collision response (UID/ATQA/SAK/ATS) plus the static response table.
 */
typedef struct __attribute__((packed)) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    uint8_t                       static_resp_count;
    nfc_tag_14a_4_static_response_t static_resp[NFC_14A_4_MAX_STATIC_RESPONSES];
} nfc_tag_14a_4_information_t;

/* Anti-collision resource — used by get_coll_res_data in app_cmd.c */
nfc_tag_14a_coll_res_reference_t *nfc_tag_14a_4_get_coll_res(void);

/* tag_base_map callbacks */
int  nfc_tag_14a_4_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int  nfc_tag_14a_4_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_14a_4_data_factory(uint8_t slot, tag_specific_type_t tag_type);

/* Static response table management (called before hw mode -e) */
void nfc_tag_14a_4_add_static_response(const uint8_t *cmd,  uint8_t cmd_len,
                                        const uint8_t *resp, uint16_t resp_len);
void nfc_tag_14a_4_clear_static_responses(void);

/* APDU relay — host-driven responses */
bool nfc_tag_14a_4_get_pending_apdu(uint8_t *buf, uint16_t *length);
void nfc_tag_14a_4_set_response(const uint8_t *data, uint16_t length);

/* Reset handler */
void nfc_tag_14a_4_reset_handler(void);

#endif /* NFC_14A_4_H */

void nfc_tag_14a_4_get_debug_counters(uint8_t *rx, uint8_t *tx, uint8_t *last_pcb, uint8_t *last_match);
