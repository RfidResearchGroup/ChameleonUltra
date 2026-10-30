/**
 * @file nfc_desfire.c
 * @brief DESFire EV1 tag emulation for ChameleonUltra
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nfc_desfire.h"

#include <string.h>

#include "fds_ids.h"
#include "fds_util.h"
#include "nrf_nfct.h"
#include "tag_persistence.h"

#define NRF_LOG_MODULE_NAME nfc_desfire
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#include "dfc_port.h"
#include "dfc_virtual_picc.h"

/* ---------------------------------------------------------------- T=CL ---- */

#define PCB_IBLOCK_MASK   0xC0
#define PCB_IBLOCK_VAL    0x00
#define PCB_RBLOCK_MASK   0xE0
#define PCB_RBLOCK_VAL    0x80
#define PCB_SBLOCK_MASK   0xC0
#define PCB_SBLOCK_VAL    0xC0
#define PCB_BLOCK_NUM     0x01
/* ISO14443-4 Table 4/Table 6. An I-block is 0x02 plus the block number, so the
 * flags sit one bit lower than they look: CID is b4, NAD b3, chaining b5. The
 * well known encodings pin this down -- I-block with CID is 0x0A, R(ACK) with
 * CID 0xAA, S(DESELECT) with CID 0xCA, and a chained I-block 0x12. Getting
 * these one bit too high made the emulator read a reader's CID as a NAD, so it
 * skipped the byte but never echoed the CID back, and made a chained block look
 * like it carried a CID -- eating the first octet of the payload. */
#define PCB_CID_FOLLOWING 0x08
#define PCB_NAD_FOLLOWING 0x04
#define PCB_CHAIN         0x10
/* S(WTX) is 1111 0010b, so 0xF2, and 0xFA once the CID is appended. The bare
 * 0x30 this used to be is not an S-block at all: it lacks both the 0xC0 that
 * marks the block type and the mandatory b2, so every WTX we emitted was
 * malformed. */
#define PCB_SBLOCK_WTX    0xF2
#define PCB_SBLOCK_DESEL  0xC2
#define WTX_VALUE         0x3B

/* Frame delay the NFC hardware allows before it reports an overrun.
 *
 * The default set by nfc_fdt_reset() is 4096 ticks (~302 us), which is far too
 * short: a single enciphered read with 3DES secure messaging runs on the order
 * of 1-2 ms. 0x40000 ticks is ~19.3 ms -- roughly ten times the worst measured
 * case, and still comfortably inside the reader's own frame waiting time (FWI=8
 * in our ATS, ~77 ms).
 *
 * This has to be re-applied on every frame, not once at load: nfc_fdt_reset()
 * lowers it again after any receive we do not answer. */
#define NFC_DESFIRE_FDT_MAX 0x40000UL

/* Largest INF a reader may send us given the FSC we advertise (64) minus PCB
 * and CRC. Also the ceiling on what we will emit in one I-block. */
#define NFC_DESFIRE_MAX_INF 61

/* ------------------------------------------------------------- state ---- */

static nfc_tag_desfire_information_t *m_info = NULL;
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res = {0};
static DfcVirtualPiccSession *m_session = NULL;

static uint8_t m_block_num = 0;
static bool m_cid_supported = false;
static uint8_t m_cid = 0;

/* Anti-collision values the credential's PICC configuration overrides.
 *
 * The slot's own res_coll stays the record the host reads and writes with the
 * 14A anti-collision command, and it is what is persisted. These buffers hold
 * only what the loaded credential states, and get_coll_res points the reference
 * at them field by field, so a credential that says nothing about (say) SAK
 * leaves the slot's SAK alone.
 *
 * m_ac_uid holds the generated identifier when random ID is enabled. It is
 * re-rolled per activation, in the reset handler, and never inside
 * get_coll_res -- that runs on every received frame, and re-rolling there would
 * change the identifier in the middle of anti-collision. */
static uint8_t m_ac_uid[10];
static nfc_tag_14a_uid_size m_ac_uid_size = NFC_TAG_14A_UID_SINGLE_SIZE;
static size_t m_ac_uid_len = 0;
static uint8_t m_ac_sak[1];
static uint8_t m_ac_atqa[2];
static nfc_14a_ats_t m_ac_ats;

/* Reassembly buffer for chained inbound I-blocks. */
static uint8_t m_rx_buf[DFC_WORKER_MAX_BUFFER_SIZE];
static uint16_t m_rx_len = 0;

static uint8_t m_resp_buf[NFC_DESFIRE_MAX_INF];
static uint16_t m_resp_len = 0;
static uint8_t m_tx_buf[NFC_DESFIRE_MAX_INF + 4];

static uint16_t m_frames_rx = 0;
static uint16_t m_frames_tx = 0;
static uint16_t m_engine_errors = 0;
static uint16_t m_max_handler_us = 0;
static uint16_t m_max_reset_us = 0;

/* ------------------------------------------------------- TX helpers ---- */

static void send_iblock(const uint8_t *data, uint16_t len) {
    uint8_t pcb = PCB_IBLOCK_VAL | 0x02 | (m_block_num & PCB_BLOCK_NUM);
    if (m_cid_supported) pcb |= PCB_CID_FOLLOWING;
    uint8_t off = 0;
    m_tx_buf[off++] = pcb;
    if (m_cid_supported) m_tx_buf[off++] = m_cid & 0x0F;
    if (len > NFC_DESFIRE_MAX_INF) len = NFC_DESFIRE_MAX_INF;
    memcpy(&m_tx_buf[off], data, len);
    nfc_tag_14a_tx_bytes(m_tx_buf, off + len, true);
    m_block_num ^= 1;
    m_frames_tx++;
}

static void send_rblock(bool ack) {
    uint8_t pcb = (ack ? 0xA2 : 0xB2) | (m_block_num & PCB_BLOCK_NUM);
    if (m_cid_supported) {
        pcb |= PCB_CID_FOLLOWING;
        uint8_t buf[2] = {pcb, m_cid & 0x0F};
        nfc_tag_14a_tx_bytes(buf, 2, true);
    } else {
        nfc_tag_14a_tx_bytes(&pcb, 1, true);
    }
}

/* ----------------------------------------------------- 14A handlers ---- */

static void refresh_activation_uid(void);
static void sync_coll_res_from_activation(void);

static void nfc_tag_desfire_reset_handler(void) {
    uint32_t t0 = DWT->CYCCNT;
    m_block_num = 0;
    m_cid_supported = false;
    m_cid = 0;
    m_rx_len = 0;
    m_resp_len = 0;

    if (m_session != NULL) {
        dfc_virtual_picc_reset_protocol(m_session);
    }
    uint32_t us = (DWT->CYCCNT - t0) / 64u;
    if (us > m_max_reset_us) m_max_reset_us = (uint16_t)(us > UINT16_MAX ? UINT16_MAX : us);
}

static void nfc_tag_desfire_field_handler(bool present) {
    nrf_nfct_frame_delay_max_set(NFC_DESFIRE_FDT_MAX);
    if (m_session == NULL) return;
    if (!present) {
        dfc_virtual_picc_field_off(m_session);
        m_ac_uid_len = 0;
        return;
    }
    refresh_activation_uid();
}

static void nfc_tag_desfire_state_handler(uint8_t *p_data, uint16_t szDataBits) {
    nrf_nfct_frame_delay_max_set(NFC_DESFIRE_FDT_MAX);

    if (m_session == NULL || m_info == NULL) return;

    /* The 14A layer passes a bit count, and the frame still carries its two CRC
     * bytes. (The parameter is named szBytes in the sibling 14443-4 handler,
     * which is a long-standing misnomer there.) */
    if ((szDataBits & 7u) != 0) return;
    uint16_t len = szDataBits / 8u;
    if (len < 3) return; /* need at least PCB + CRC */
    /* checks_crc takes the whole frame: it computes over all but the last two
     * octets and compares them against those two. */
    if (!nfc_tag_14a_checks_crc(p_data, len)) return;
    len -= 2;

    m_frames_rx++;

    uint8_t pcb = p_data[0];

    /* --- S-block: DESELECT or the reader echoing our WTX --- */
    if ((pcb & PCB_SBLOCK_MASK) == PCB_SBLOCK_VAL) {
        /* An S-block carries a CID of its own, and a reader may send one before
         * any I-block has been exchanged -- a RATS followed straight by
         * DESELECT does exactly that. Latch it here too, or the answer goes out
         * without the CID the reader addressed us with. */
        if (pcb & PCB_CID_FOLLOWING) {
            if (len < 2) return;
            m_cid = p_data[1] & 0x0F;
            m_cid_supported = true;
        }
        if ((pcb & 0x30) == 0x30) {
            /* WTX response: nothing pending to resume, so just acknowledge. */
            uint8_t buf[3];
            uint8_t off = 0;
            buf[off++] = PCB_SBLOCK_WTX | (m_cid_supported ? PCB_CID_FOLLOWING : 0);
            if (m_cid_supported) buf[off++] = m_cid & 0x0F;
            buf[off++] = WTX_VALUE;
            nfc_tag_14a_tx_bytes(buf, off, true);
        } else {
            /* DESELECT: mirror it back and tear the session down. */
            uint8_t buf[2];
            uint8_t off = 0;
            buf[off++] = PCB_SBLOCK_DESEL | (m_cid_supported ? PCB_CID_FOLLOWING : 0);
            if (m_cid_supported) buf[off++] = m_cid & 0x0F;
            nfc_tag_14a_tx_bytes(buf, off, true);
            nfc_tag_desfire_reset_handler();
        }
        return;
    }

    /* --- R-block: retransmit the last response on NAK, ack otherwise --- */
    if ((pcb & PCB_RBLOCK_MASK) == PCB_RBLOCK_VAL) {
        bool nak = (pcb & 0x10) != 0;
        if (nak && m_resp_len > 0) {
            m_block_num ^= 1; /* undo the increment from the original send */
            send_iblock(m_resp_buf, m_resp_len);
        } else {
            send_rblock(true);
        }
        return;
    }

    /* --- I-block --- */
    if ((pcb & PCB_IBLOCK_MASK) != PCB_IBLOCK_VAL) return;

    uint8_t off = 1;
    if (pcb & PCB_CID_FOLLOWING) {
        if (len < off + 1) return;
        m_cid = p_data[off++] & 0x0F;
        m_cid_supported = true;
    }
    if (pcb & PCB_NAD_FOLLOWING) {
        if (len < off + 1) return;
        off++; /* NAD is not used by DESFire */
    }
    if (len < off) return;

    uint16_t inf_len = len - off;

    /* Reassemble chained commands. A conformant reader will not chain at FSC=64
     * because DESFire's own 0xAF framing keeps every command inside one frame,
     * but a non-conformant one must not be allowed to silently truncate. */
    if (m_rx_len + inf_len > sizeof(m_rx_buf)) {
        m_rx_len = 0;
        send_rblock(false);
        return;
    }
    memcpy(m_rx_buf + m_rx_len, &p_data[off], inf_len);
    m_rx_len += inf_len;

    if (pcb & PCB_CHAIN) {
        send_rblock(true);
        return;
    }

    if (m_rx_len == 0) return;

    uint32_t t0 = DWT->CYCCNT;

    size_t resp_len = 0;
    DfcVirtualPiccStatus st = dfc_virtual_picc_iso_dep_exchange(
                                 m_session, m_rx_buf, m_rx_len,
                                 m_resp_buf, sizeof(m_resp_buf), &resp_len);
    m_rx_len = 0;

    /* 64 MHz core, so cycles / 64 is microseconds. This is the number that
     * validates the frame delay budget on real hardware rather than on paper. */
    uint32_t us = (DWT->CYCCNT - t0) / 64u;
    if (us > m_max_handler_us) m_max_handler_us = (uint16_t)(us > 0xFFFF ? 0xFFFF : us);

    if (st != DfcVirtualPiccStatusOk || resp_len == 0) {
        m_engine_errors++;
        m_resp_len = 0;
        send_rblock(false);
        return;
    }

    /* Outbound chaining is deliberately not implemented: the engine caps native
     * responses at DFC_EV1_MAX_FRAME_PAYLOAD and continues them with 0xAF, so a
     * wire I-block never exceeds the advertised FSC. If that ever stops holding,
     * fail loudly rather than truncating the response. */
    if (resp_len > NFC_DESFIRE_MAX_INF) {
        NRF_LOG_ERROR("DESFire response %u exceeds FSC, dropping", (unsigned)resp_len);
        m_engine_errors++;
        m_resp_len = 0;
        send_rblock(false);
        return;
    }

    m_resp_len = (uint16_t)resp_len;
    send_iblock(m_resp_buf, m_resp_len);
}

/* --------------------------------------------------- slot callbacks ---- */

/* Take the identifier this activation presents from the engine, which is the
 * credential's own UID, or a freshly rolled one when the credential asks for a
 * random identifier. Either way it is settled once per activation and never
 * inside get_coll_res.
 *
 * With a random identifier the credential's stored UID is still reachable, but
 * only through GetCardUID under an authenticated session. */
static void refresh_activation_uid(void) {
    if (m_info == NULL) return;
    DfcVirtualPiccActivation activation;
    if (m_session == NULL ||
            dfc_virtual_picc_scan_iso14443a(m_session, &activation) != DfcVirtualPiccStatusOk) {
        return;
    }
    if (activation.uid_len == 0 || activation.uid_len > sizeof(m_ac_uid)) return;
    memcpy(m_ac_uid, activation.uid, activation.uid_len);
    m_ac_uid_size = (nfc_tag_14a_uid_size)activation.uid_len;
    m_ac_uid_len = activation.uid_len;
}

nfc_tag_14a_coll_res_reference_t *nfc_tag_desfire_get_coll_res(void) {
    if (m_info == NULL) return NULL;
    const DfcCredential *cred = &m_info->credential;

    /* The slot's stored record is the baseline: it carries the board defaults
     * and anything the host set with the 14A anti-collision command. */
    m_shadow_coll_res.sak = m_info->res_coll.sak;
    m_shadow_coll_res.atqa = m_info->res_coll.atqa;
    m_shadow_coll_res.uid = m_info->res_coll.uid;
    m_shadow_coll_res.size = &m_info->res_coll.size;
    m_shadow_coll_res.ats = &m_info->res_coll.ats;

    /* Whatever the credential states wins over it. The identifier a reader sees
     * is the credential's, not the slot's: these credentials are keyed by their
     * own UID, so presenting the slot's would make a reader derive the wrong
     * keys. A random identifier is the same path with a rolled value. */
    if (m_ac_uid_len > 0) {
        m_shadow_coll_res.uid = m_ac_uid;
        m_shadow_coll_res.size = &m_ac_uid_size;
    }
    if (cred->picc_has_sak) {
        m_ac_sak[0] = cred->picc_sak;
        m_shadow_coll_res.sak = m_ac_sak;
    }
    if (cred->picc_has_atqa) {
        /* The credential holds ATQA most significant octet first; the wire
         * carries it least significant octet first, which is the order
         * res_coll uses. */
        m_ac_atqa[0] = cred->picc_atqa[1];
        m_ac_atqa[1] = cred->picc_atqa[0];
        m_shadow_coll_res.atqa = m_ac_atqa;
    }
    if (cred->picc_ats_len > 0 && cred->picc_ats_len <= sizeof(m_ac_ats.data)) {
        m_ac_ats.length = (uint8_t)cred->picc_ats_len;
        memcpy(m_ac_ats.data, cred->picc_ats, cred->picc_ats_len);
        m_shadow_coll_res.ats = &m_ac_ats;
    }
    return &m_shadow_coll_res;
}

static void build_defaults(nfc_tag_desfire_information_t *info) {
    memset(info, 0, sizeof(*info));
    info->magic = NFC_DESFIRE_MAGIC;
    info->version = NFC_DESFIRE_VERSION;
    info->cred_size = (uint16_t)sizeof(DfcCredential);
    info->max_apps = DFC_MAX_APPS;
    info->max_keys = DFC_MAX_KEYS;
    info->max_files = DFC_MAX_FILES;
    info->file_pool_size = DFC_FILE_POOL_SIZE;
    info->key_pool_size = DFC_KEY_POOL_SIZE;

    dfc_credential_init_blank(&info->credential);

    /* Board defaults, used for whatever the loaded credential says nothing
     * about. A credential that carries its own SAK, ATQA, ATS or asks for a
     * random identifier overrides these in nfc_tag_desfire_get_coll_res. */
    info->res_coll.size = NFC_TAG_14A_UID_DOUBLE_SIZE;
    /* ATQA 0x0344, transmitted least-significant byte first. */
    info->res_coll.atqa[0] = 0x44;
    info->res_coll.atqa[1] = 0x03;
    info->res_coll.sak[0] = 0x20; /* ISO14443-4 compliant */
    memcpy(info->res_coll.uid, info->credential.uid, DFC_DESFIRE_UID_LEN);

    /* Specification section 1.1 default: a genuine EV1's ATS. Keep in step with
     * the engine default in engine/dfc_virtual_picc.c -- both exist because a
     * slot's anti-collision record is written before any credential is loaded. */
    static const uint8_t default_ats[] = {0x05, 0x65, 0x81, 0x02, 0x80};
    info->res_coll.ats.length = sizeof(default_ats);
    memcpy(info->res_coll.ats.data, default_ats, sizeof(default_ats));
}

static bool info_is_valid(const nfc_tag_desfire_information_t *info) {
    return info->magic == NFC_DESFIRE_MAGIC && info->version == NFC_DESFIRE_VERSION &&
           info->cred_size == sizeof(DfcCredential) && info->max_apps == DFC_MAX_APPS &&
           info->max_keys == DFC_MAX_KEYS && info->max_files == DFC_MAX_FILES &&
           info->file_pool_size == DFC_FILE_POOL_SIZE &&
           info->key_pool_size == DFC_KEY_POOL_SIZE;
}

/* Enable the cycle counter used to time the engine call. Free on Cortex-M4 and
 * unused elsewhere in this firmware, so no timer instance is consumed. */
static void enable_cycle_counter(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static bool bind_session(void) {
    if (m_session != NULL) {
        dfc_virtual_picc_session_free(m_session);
        m_session = NULL;
    }
    m_session = dfc_virtual_picc_session_alloc(&m_info->credential);
    if (m_session == NULL) {
        NRF_LOG_ERROR("DESFire: no session slot available");
        return false;
    }

    DfcVirtualPiccActivation activation;
    if (dfc_virtual_picc_scan_iso14443a(m_session, &activation) != DfcVirtualPiccStatusOk) {
        /* Happens when the UID is unusable (all zero, or not starting 0x04). */
        NRF_LOG_ERROR("DESFire: credential UID not detectable");
        return false;
    }
    /* Have an identifier ready before the first REQA, in case anti-collision
     * starts before any reset handler has run. */
    if (m_info->credential.picc_random_id && activation.uid_len <= sizeof(m_ac_uid)) {
        memcpy(m_ac_uid, activation.uid, activation.uid_len);
        m_ac_uid_size = (nfc_tag_14a_uid_size)activation.uid_len;
    }
    return true;
}

int nfc_tag_desfire_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    (void)type;
    int info_size = sizeof(nfc_tag_desfire_information_t);
    if (buffer->length < info_size) {
        NRF_LOG_ERROR("DESFire loadcb: buffer too small (%d < %d)", buffer->length, info_size);
        return info_size;
    }

    enable_cycle_counter();
    m_info = (nfc_tag_desfire_information_t *)buffer->buffer;
    /* Whatever the previous slot presented is not this slot's. */
    m_ac_uid_len = 0;

    if (!info_is_valid(m_info)) {
        /* Rebuild rather than reinterpret a record written by a firmware with
         * different capacity limits. Loud, because it discards stored data. */
        NRF_LOG_WARNING("DESFire loadcb: slot record invalid, rebuilding defaults");
        build_defaults(m_info);
    }

    if (!bind_session()) {
        build_defaults(m_info);
        if (!bind_session()) return info_size;
    }

    /* A record written by an older firmware may carry anti-collision values the
     * engine no longer answers with, so settle them here too rather than only
     * where a credential is installed. */
    sync_coll_res_from_activation();

    nfc_tag_14a_handler_t handler = {
        .get_coll_res = nfc_tag_desfire_get_coll_res,
        .cb_state = nfc_tag_desfire_state_handler,
        .cb_reset = nfc_tag_desfire_reset_handler,
        .cb_field = nfc_tag_desfire_field_handler,
    };
    nfc_tag_14a_set_handler(&handler);

    NRF_LOG_INFO(
        "DESFire loadcb OK: apps=%d files=%d pool=%d/%d",
        (int)m_info->credential.num_apps,
        (int)m_info->credential.num_files,
        (int)m_info->credential.file_pool_used,
        DFC_FILE_POOL_SIZE);
    return info_size;
}

int nfc_tag_desfire_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    (void)type;
    (void)buffer;
    /* The engine mutates the credential in place inside the slot buffer, so
     * there is nothing to serialise -- tag_emulation compares the CRC of this
     * region and writes it out when a reader has changed it. */
    if (m_info == NULL) return 0;
    return sizeof(nfc_tag_desfire_information_t);
}

bool nfc_tag_desfire_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    if (tag_type != TAG_TYPE_DESFIRE_EV1_2K && tag_type != TAG_TYPE_DESFIRE_EV1_4K &&
        tag_type != TAG_TYPE_DESFIRE_EV1_8K) {
        return false;
    }

    /* Build into the shared HF slot buffer rather than on the stack: the record
     * is nearly 4 KiB and the task stack is 8 KiB. */
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL || buffer->length < sizeof(nfc_tag_desfire_information_t)) {
        NRF_LOG_ERROR("DESFire factory: no buffer for slot %d", slot);
        return false;
    }

    nfc_tag_desfire_information_t *info = (nfc_tag_desfire_information_t *)buffer->buffer;
    build_defaults(info);

    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, TAG_SENSE_HF, &map_info);
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(*info), info);
    NRF_LOG_INFO("DESFire factory slot %d: %s", slot, ret ? "OK" : "FAIL");
    return ret;
}

DfcCredential *nfc_tag_desfire_get_credential(void) {
    if (m_info == NULL) return NULL;
    return &m_info->credential;
}

/* Bring the slot's stored anti-collision record into line with what the card
 * actually presents.
 *
 * The engine is the only thing that knows: it answers activation from the
 * credential's PICC configuration and from its own defaults for whatever the
 * credential leaves out. A host cannot supply those values without duplicating
 * those defaults, and a slot record that disagrees with the card is a trap -- one
 * identifier in the listing, another on the wire. So the record is written from
 * the activation the engine computes, once, when the credential is bound.
 *
 * A credential asking for a random identifier keeps its stored UID here: that is
 * the one GetCardUID returns under an authenticated session, and the per
 * activation identifier is applied separately in get_coll_res. */
static void sync_coll_res_from_activation(void) {
    if (m_info == NULL || m_session == NULL) return;

    DfcVirtualPiccActivation activation;
    if (dfc_virtual_picc_scan_iso14443a(m_session, &activation) != DfcVirtualPiccStatusOk) return;

    const DfcCredential *cred = &m_info->credential;
    const uint8_t *uid = cred->picc_random_id ? cred->uid : activation.uid;
    size_t uid_len = cred->picc_random_id ? cred->uid_len : activation.uid_len;
    if (uid_len > 0 && uid_len <= sizeof(m_info->res_coll.uid)) {
        memcpy(m_info->res_coll.uid, uid, uid_len);
        m_info->res_coll.size = (nfc_tag_14a_uid_size)uid_len;
    }
    m_info->res_coll.sak[0] = activation.sak;
    if (activation.atqa_len == 2) {
        /* The engine reports ATQA most significant octet first; res_coll uses
         * wire order, least significant first. */
        m_info->res_coll.atqa[0] = activation.atqa[1];
        m_info->res_coll.atqa[1] = activation.atqa[0];
    }
    if (activation.ats_len > 0 && activation.ats_len <= sizeof(m_info->res_coll.ats.data)) {
        memcpy(m_info->res_coll.ats.data, activation.ats, activation.ats_len);
        m_info->res_coll.ats.length = (uint8_t)activation.ats_len;
    }
}

bool nfc_tag_desfire_reload(void) {
    if (m_info == NULL) return false;
    if (!info_is_valid(m_info)) return false;
    if (!bind_session()) return false;
    /* Bind first: the activation this reads comes from the bound session. */
    sync_coll_res_from_activation();
    return true;
}

void nfc_tag_desfire_get_stats(
    uint16_t *frames_rx,
    uint16_t *frames_tx,
    uint16_t *engine_errors,
    uint16_t *max_handler_us,
    uint16_t *activation_requests,
    uint16_t *atqa_tx,
    uint16_t *fdt_timeouts,
    uint16_t *max_reset_us) {
    if (frames_rx) *frames_rx = m_frames_rx;
    if (frames_tx) *frames_tx = m_frames_tx;
    if (engine_errors) *engine_errors = m_engine_errors;
    if (max_handler_us) *max_handler_us = m_max_handler_us;
    nfc_tag_14a_get_activation_stats(activation_requests, atqa_tx, fdt_timeouts);
    if (max_reset_us) *max_reset_us = m_max_reset_us;
}

_Static_assert(
    sizeof(nfc_tag_desfire_information_t) <= 4500,
    "DESFire slot record exceeds the shared HF tag data buffer");
