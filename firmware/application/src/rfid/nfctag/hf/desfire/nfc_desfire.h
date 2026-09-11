/**
 * @file nfc_desfire.h
 * @brief DESFire EV1 tag emulation for ChameleonUltra
 *
 * Bridges the tag-emulation slot machinery and the ISO14443-4 transport to the
 * DESFire engine. This file owns T=CL framing (PCB, block numbers, chaining);
 * the engine is handed bare APDUs and knows nothing about the transport.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NFC_DESFIRE_H
#define NFC_DESFIRE_H

#include "dfc_credential.h"
#include "nfc_14a.h"
#include "tag_emulation.h"

/* 'DFR1'. Bumped only when the persisted layout changes incompatibly. */
#define NFC_DESFIRE_MAGIC   0x44465231u
#define NFC_DESFIRE_VERSION 3u

/**
 * Per-slot persisted data.
 *
 * Deliberately NOT packed: DfcCredential contains size_t members and must keep
 * its natural alignment, and FDS writes whole words anyway.
 *
 * The credential lives inline rather than behind a pointer. That is what makes
 * reader-initiated writes durable for free: tag_emulation CRCs this buffer and
 * flushes it to flash when it changes, so a WriteData/Credit/Debit performed by
 * a reader is persisted on field loss with no serialisation step.
 *
 * The limit fields record the capacities the image was built with. They are
 * checked on load so a firmware whose DFC_MAX_* differ falls back to defaults
 * instead of reinterpreting a mismatched struct.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t cred_size;
    uint16_t max_apps;
    uint16_t max_keys;
    uint16_t max_files;
    uint16_t file_pool_size;
    uint16_t key_pool_size;
    nfc_tag_14a_coll_res_entity_t res_coll;
    uint32_t reserved; /* keeps the credential 4-byte aligned */
    DfcCredential credential;
} nfc_tag_desfire_information_t;

/* Anti-collision resource, used by get_coll_res_data in app_cmd.c */
nfc_tag_14a_coll_res_reference_t *nfc_tag_desfire_get_coll_res(void);

/* tag_base_map callbacks */
int nfc_tag_desfire_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer);
int nfc_tag_desfire_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer);
bool nfc_tag_desfire_data_factory(uint8_t slot, tag_specific_type_t tag_type);

/* Active slot's credential, for host load/dump commands. NULL when the active
 * slot is not a DESFire slot. */
DfcCredential *nfc_tag_desfire_get_credential(void);

/* Re-validate and re-bind the engine after a host command rewrites the slot
 * buffer in place. Returns false if the blob failed validation. */
bool nfc_tag_desfire_reload(void);

/* Diagnostics for the debug command: frames seen, engine rejections, and the
 * worst observed handler duration in microseconds. */
void nfc_tag_desfire_get_stats(
    uint16_t *frames_rx,
    uint16_t *frames_tx,
    uint16_t *engine_errors,
    uint16_t *max_handler_us,
    uint16_t *activation_requests,
    uint16_t *atqa_tx,
    uint16_t *fdt_timeouts,
    uint16_t *max_reset_us);

#endif
