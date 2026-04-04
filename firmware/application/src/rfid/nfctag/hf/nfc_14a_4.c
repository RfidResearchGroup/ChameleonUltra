/**
 * @file nfc_14a_4.c
 * @brief ISO14443-4 T=CL emulation for ChameleonUltra
 *
 * Implements a full ISO14443-4 tag emulator with a static APDU response
 * table.  The table is populated by the host before field activation, so
 * the firmware can respond to an EMV reader autonomously without any USB
 * communication while the RF field is active.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include "nfc_14a_4.h"
#include "nfc_14a.h"
#include "tag_emulation.h"
#include "tag_persistence.h"
#include "fds_util.h"
#include "nrf_log.h"

/* ------------------------------------------------------------------ */
/*  PCB byte constants (ISO14443-4 §7)                                 */
/* ------------------------------------------------------------------ */
#define PCB_IBLOCK_MASK     0xC0
#define PCB_IBLOCK_VAL      0x00
#define PCB_RBLOCK_MASK     0xE0
#define PCB_RBLOCK_VAL      0x80   /* R(ACK) = 0xA2/0xA3, R(NAK) = 0xB2/0xB3 */
#define PCB_SBLOCK_MASK     0xC0
#define PCB_SBLOCK_VAL      0xC0
#define PCB_BLOCK_NUM       0x01
#define PCB_CID_FOLLOWING   0x10  /* bit4: CID follows */
#define PCB_NAD_FOLLOWING   0x08  /* bit3: NAD follows */
#define PCB_CHAIN           0x20  /* bit5: chaining flag per ISO14443-4 Table 3 */
#define PCB_SBLOCK_WTX      0x30
#define PCB_SBLOCK_DESELECT 0xC2
#define WTX_VALUE           0x3B   /* WTXM=59 (~3s extra wait) */

static inline bool is_iblock(uint8_t pcb) {
    return (pcb & PCB_IBLOCK_MASK) == PCB_IBLOCK_VAL;
}
static inline bool is_rblock(uint8_t pcb) {
    /* R-block: bit7=1, bit6=0, bit2=1, bit1=0 (mask 0xC6, value 0x82) */
    return (pcb & 0xC6) == 0x82;
}
static inline bool is_sblock(uint8_t pcb) {
    return (pcb & PCB_SBLOCK_MASK) == PCB_SBLOCK_VAL;
}

/* ------------------------------------------------------------------ */
/*  Module state                                                        */
/* ------------------------------------------------------------------ */
static nfc_tag_14a_4_information_t *m_tag_information = NULL;

/* Shadow coll-res references into m_tag_information */
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;

/* T=CL session state */
static uint8_t  m_block_num      = 0;
static bool     m_cid_supported  = false;
static uint8_t  m_cid            = 0;
static uint8_t  m_apdu_buf[NFC_14A_4_MAX_APDU];
static uint16_t m_apdu_len       = 0;
static bool     m_apdu_pending   = false;
static uint8_t  m_resp_buf[NFC_14A_4_MAX_APDU];
static uint16_t m_resp_len       = 0;
static bool     m_response_ready = false;

/* TX scratch buffer */
static uint8_t m_tx_buf[NFC_14A_4_MAX_APDU + 4];

/* Debug counters — readable via hf 14a debug */
static uint8_t  m_dbg_iblocks_rx  = 0;  /* I-blocks received */
static uint8_t  m_dbg_iblocks_tx  = 0;  /* I-blocks sent */
static uint8_t  m_dbg_last_rx_pcb = 0;  /* PCB of last received I-block */
static uint8_t  m_dbg_last_match  = 0;  /* last find_static_response result */

/* Static APDU response table (RAM copy, populated from m_tag_information) */
static nfc_tag_14a_4_static_response_t m_static_resp[NFC_14A_4_MAX_STATIC_RESPONSES];
static uint8_t m_static_resp_count = 0;

/* Large response overflow (RAM only, > NFC_14A_4_MAX_STATIC_RESP_LEN bytes).
 * NOT persisted to flash. Must reload via emv load after power cycle. */
typedef struct {
    uint8_t  cmd[NFC_14A_4_MAX_STATIC_CMD_LEN];
    uint8_t  cmd_len;
    uint8_t  resp[NFC_14A_4_MAX_LARGE_RESP_LEN];
    uint16_t resp_len;
} nfc_tag_14a_4_large_response_t;
static nfc_tag_14a_4_large_response_t m_large_resp[NFC_14A_4_MAX_LARGE_RESPONSES];
static uint8_t m_large_resp_count = 0;

/* ------------------------------------------------------------------ */
/*  Static response table                                               */
/* ------------------------------------------------------------------ */

void nfc_tag_14a_4_add_static_response(const uint8_t *cmd,  uint8_t cmd_len,
                                        const uint8_t *resp, uint16_t resp_len) {
    if (cmd_len > NFC_14A_4_MAX_STATIC_CMD_LEN) cmd_len = NFC_14A_4_MAX_STATIC_CMD_LEN;

    if (resp_len > NFC_14A_4_MAX_STATIC_RESP_LEN) {
        /* Large response: RAM-only overflow table */
        if (m_large_resp_count >= NFC_14A_4_MAX_LARGE_RESPONSES) return;
        if (resp_len > NFC_14A_4_MAX_LARGE_RESP_LEN) resp_len = NFC_14A_4_MAX_LARGE_RESP_LEN;
        nfc_tag_14a_4_large_response_t *le = &m_large_resp[m_large_resp_count++];
        le->cmd_len  = cmd_len;
        le->resp_len = resp_len;
        memcpy(le->cmd,  cmd,  cmd_len);
        memcpy(le->resp, resp, resp_len);
        return;
    }

    /* Normal response: flash-backed table */
    if (m_static_resp_count >= NFC_14A_4_MAX_STATIC_RESPONSES) return;
    nfc_tag_14a_4_static_response_t *e = &m_static_resp[m_static_resp_count++];
    e->cmd_len  = cmd_len;
    e->resp_len = (uint8_t)resp_len;
    memcpy(e->cmd,  cmd,  cmd_len);
    memcpy(e->resp, resp, resp_len);
    if (m_tag_information &&
            m_tag_information->static_resp_count < NFC_14A_4_MAX_STATIC_RESPONSES) {
        memcpy(&m_tag_information->static_resp[m_tag_information->static_resp_count++],
               e, sizeof(*e));
    }
}

void nfc_tag_14a_4_clear_static_responses(void) {
    m_static_resp_count = 0;
    m_large_resp_count  = 0;
    if (m_tag_information) {
        m_tag_information->static_resp_count = 0;
    }
}

static bool find_static_response(const uint8_t *apdu, uint16_t apdu_len,
                                  uint8_t **resp_out, uint16_t *resp_len_out) {
    /* Flash-backed table */
    for (uint8_t i = 0; i < m_static_resp_count; i++) {
        nfc_tag_14a_4_static_response_t *e = &m_static_resp[i];
        if (apdu_len >= e->cmd_len &&
                memcmp(apdu, e->cmd, e->cmd_len) == 0) {
            *resp_out     = e->resp;
            *resp_len_out = e->resp_len;
            return true;
        }
    }
    /* RAM-only large response table */
    for (uint8_t i = 0; i < m_large_resp_count; i++) {
        nfc_tag_14a_4_large_response_t *e = &m_large_resp[i];
        if (apdu_len >= e->cmd_len &&
                memcmp(apdu, e->cmd, e->cmd_len) == 0) {
            *resp_out     = e->resp;
            *resp_len_out = e->resp_len;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  TX helpers                                                          */
/* ------------------------------------------------------------------ */

static void send_iblock(const uint8_t *data, uint16_t len) {
    uint8_t pcb = 0x02 | (m_block_num & 0x01);
    if (m_cid_supported) pcb |= PCB_CID_FOLLOWING;
    uint8_t off = 0;
    m_tx_buf[off++] = pcb;
    if (m_cid_supported) m_tx_buf[off++] = m_cid & 0x0F;
    if (len > NFC_14A_4_MAX_APDU) len = NFC_14A_4_MAX_APDU;
    memcpy(&m_tx_buf[off], data, len);
    nfc_tag_14a_tx_bytes(m_tx_buf, off + len, true);
    m_block_num ^= 1;
}

static void send_rack(void) {
    uint8_t pcb = 0xA2 | (m_block_num & 0x01);
    if (m_cid_supported) {
        pcb |= PCB_CID_FOLLOWING;
        uint8_t buf[2] = { pcb, m_cid & 0x0F };
        nfc_tag_14a_tx_bytes(buf, 2, true);
    } else {
        nfc_tag_14a_tx_bytes(&pcb, 1, true);
    }
}

static void send_wtx(void) {
    uint8_t buf[3];
    uint8_t off = 0;
    buf[off++] = PCB_SBLOCK_WTX | (m_cid_supported ? PCB_CID_FOLLOWING : 0);
    if (m_cid_supported) buf[off++] = m_cid & 0x0F;
    buf[off++] = WTX_VALUE;
    nfc_tag_14a_tx_bytes(buf, off, true);
}

/* ------------------------------------------------------------------ */
/*  State handler (called from NFCT ISR on each received frame)        */
/* ------------------------------------------------------------------ */

static void nfc_tag_14a_4_state_handler(uint8_t *data, uint16_t szBytes) {
    if (szBytes == 0) return;
    uint8_t pcb = data[0];

    /* ---- S-block ---- */
    if (is_sblock(pcb)) {
        if ((pcb & 0xF7) == PCB_SBLOCK_DESELECT) {
            /* Echo DESELECT */
            nfc_tag_14a_tx_bytes(data, szBytes, true);
            nfc_tag_14a_4_reset_handler();
            return;
        }
        if ((pcb & 0x3F) == (PCB_SBLOCK_WTX & 0x3F)) {
            /* Reader sending WTX — echo back with our WTXM */
            uint8_t wtxm = (szBytes > 1) ? data[szBytes - 1] & 0x3F : WTX_VALUE;
            uint8_t resp[3];
            uint8_t off = 0;
            resp[off++] = PCB_SBLOCK_WTX | (m_cid_supported ? PCB_CID_FOLLOWING : 0);
            if (m_cid_supported) resp[off++] = m_cid & 0x0F;
            resp[off++] = wtxm;
            nfc_tag_14a_tx_bytes(resp, off, true);
            /* If we now have a response ready, send it next I-block */
            if (m_response_ready) {
                m_response_ready = false;
                send_iblock(m_resp_buf, m_resp_len);
            }
            return;
        }
        return;
    }

    /* ---- R-block ---- */
    if (is_rblock(pcb)) {
        send_rack();
        return;
    }

    /* ---- I-block ---- */
    if (is_iblock(pcb)) {
        uint8_t reader_blknum = pcb & PCB_BLOCK_NUM;
        bool    has_cid       = (pcb & PCB_CID_FOLLOWING) != 0;
        bool    has_nad       = (pcb & PCB_NAD_FOLLOWING) != 0;
        bool    more_chain    = (pcb & PCB_CHAIN)         != 0;

        uint8_t offset = 1;
        if (has_cid) {
            /* CID acknowledged but not used in responses (keeps protocol simpler) */
            m_cid_supported = false;
            offset++;  /* skip CID byte */
        }
        if (has_nad) offset++;

        if (offset >= szBytes) {
            send_rack();
            return;
        }

        uint16_t apdu_len = szBytes - offset;
        if (apdu_len > NFC_14A_4_MAX_APDU) apdu_len = NFC_14A_4_MAX_APDU;

        m_dbg_iblocks_rx++;
        m_dbg_last_rx_pcb = pcb;
        NRF_LOG_INFO("14A4 I-block #%d: reader_blk=%d m_block_num=%d apdu_len=%d",
                      m_dbg_iblocks_rx, reader_blknum, m_block_num, apdu_len);

        /* Block number check per ISO14443-4 §7.5.3.3:
         * If block number matches expected, process new APDU.
         * If block number does NOT match, it is a retransmit —
         * resend the last response without re-processing. */
        if (reader_blknum != (m_block_num & 0x01)) {
            /* Retransmit: resend last response */
            if (m_resp_len > 0) {
                /* Restore block num to what we sent last time and resend */
                m_block_num ^= 1;  /* undo the increment from last send */
                send_iblock(m_resp_buf, m_resp_len);
            } else {
                send_rack();
            }
            return;
        }

        memcpy(m_apdu_buf, &data[offset], apdu_len);
        m_apdu_len    = apdu_len;
        m_apdu_pending = true;
        m_response_ready = false;

        if (more_chain) {
            send_rack();
            return;
        }

        /* APDU complete — check static table first, then WTX */
        {
            uint8_t  *static_resp = NULL;
            uint16_t  static_len  = 0;
            bool _found = find_static_response(m_apdu_buf, apdu_len,
                                      &static_resp, &static_len);
            m_dbg_last_match = _found ? 1 : 0;
            NRF_LOG_INFO("14A4 find_static: found=%d static_len=%d resp_count=%d",
                          _found, static_len, m_static_resp_count);
            if (_found) {
                m_dbg_iblocks_tx++;
                memcpy(m_resp_buf, static_resp, static_len);
                m_resp_len = static_len;
                send_iblock(m_resp_buf, m_resp_len);
            } else if (m_response_ready) {
                m_response_ready = false;
                send_iblock(m_resp_buf, m_resp_len);
            } else {
                /* No response ready — keep reader alive with WTX */
                send_wtx();
            }
        }
        return;
    }

    NRF_LOG_INFO("14A-4: unknown PCB 0x%02x", pcb);
}


/* ------------------------------------------------------------------ */
/*  APDU relay API (for host-driven responses)                         */
/* ------------------------------------------------------------------ */

bool nfc_tag_14a_4_get_pending_apdu(uint8_t *buf, uint16_t *length) {
    if (!m_apdu_pending) return false;
    m_apdu_pending = false;
    *length = m_apdu_len;
    memcpy(buf, m_apdu_buf, m_apdu_len);
    return true;
}

void nfc_tag_14a_4_set_response(const uint8_t *data, uint16_t length) {
    if (length > NFC_14A_4_MAX_APDU) length = NFC_14A_4_MAX_APDU;
    memcpy(m_resp_buf, data, length);
    m_resp_len = length;
    m_response_ready = true;
}

/* ------------------------------------------------------------------ */
/*  Reset handler                                                       */
/* ------------------------------------------------------------------ */

void nfc_tag_14a_4_reset_handler(void) {
    m_block_num      = 0;
    m_cid_supported  = false;
    m_cid            = 0;
    m_apdu_pending   = false;
    m_response_ready = false;
    m_apdu_len       = 0;
    m_resp_len       = 0;
}

void nfc_tag_14a_4_get_debug_counters(uint8_t *rx, uint8_t *tx,
                                       uint8_t *last_pcb, uint8_t *last_match) {
    *rx = m_dbg_iblocks_rx;
    *tx = m_dbg_iblocks_tx;
    *last_pcb = m_dbg_last_rx_pcb;
    *last_match = m_dbg_last_match;
}

/* ------------------------------------------------------------------ */
/*  Anti-collision resource                                             */
/* ------------------------------------------------------------------ */

nfc_tag_14a_coll_res_reference_t *nfc_tag_14a_4_get_coll_res(void) {
    if (m_tag_information == NULL) return NULL;
    m_shadow_coll_res.sak  = m_tag_information->res_coll.sak;
    m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
    m_shadow_coll_res.uid  = m_tag_information->res_coll.uid;
    m_shadow_coll_res.size = &m_tag_information->res_coll.size;
    m_shadow_coll_res.ats  = &m_tag_information->res_coll.ats;
    return &m_shadow_coll_res;
}

/* ------------------------------------------------------------------ */
/*  Data load / save / factory callbacks                               */
/* ------------------------------------------------------------------ */

int nfc_tag_14a_4_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    int info_size = sizeof(nfc_tag_14a_4_information_t);
    if (buffer->length < info_size) {
        NRF_LOG_ERROR("14A-4 loadcb: buffer too small (%d < %d)",
                       buffer->length, info_size);
        return info_size;
    }
    m_tag_information = (nfc_tag_14a_4_information_t *)buffer->buffer;

    /* Populate RAM static table from persisted slot data */
    m_static_resp_count = m_tag_information->static_resp_count;
    if (m_static_resp_count > NFC_14A_4_MAX_STATIC_RESPONSES)
        m_static_resp_count = NFC_14A_4_MAX_STATIC_RESPONSES;
    memcpy(m_static_resp, m_tag_information->static_resp,
           m_static_resp_count * sizeof(nfc_tag_14a_4_static_response_t));

    nfc_tag_14a_handler_t handler = {
        .get_coll_res = nfc_tag_14a_4_get_coll_res,
        .cb_state     = nfc_tag_14a_4_state_handler,
        .cb_reset     = nfc_tag_14a_4_reset_handler,
    };
    nfc_tag_14a_set_handler(&handler);
    NRF_LOG_INFO("14A-4 loadcb OK: SAK=%02x uid_sz=%d static_resp=%d",
                  m_tag_information->res_coll.sak[0],
                  m_tag_information->res_coll.size,
                  m_static_resp_count);
    return info_size;
}

int nfc_tag_14a_4_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return sizeof(nfc_tag_14a_4_information_t);
}

bool nfc_tag_14a_4_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    if (tag_type != TAG_TYPE_HF14A_4) return false;

    /* Build factory defaults on stack and write directly to FDS
     * (same pattern as nfc_tag_mf1_data_factory). */
    nfc_tag_14a_4_information_t info;
    memset(&info, 0, sizeof(info));

    /* Placeholder 7-byte NXP-style UID */
    info.res_coll.size    = NFC_TAG_14A_UID_DOUBLE_SIZE;
    info.res_coll.atqa[0] = 0x04;
    info.res_coll.atqa[1] = 0x00;
    info.res_coll.sak[0]  = 0x20;   /* ISO14443-4 */
    info.res_coll.uid[0]  = 0x04;
    info.res_coll.uid[1]  = 0x01;
    info.res_coll.uid[2]  = 0x02;
    info.res_coll.uid[3]  = 0x03;
    info.res_coll.uid[4]  = 0x04;
    info.res_coll.uid[5]  = 0x05;
    info.res_coll.uid[6]  = 0x06;

    static const uint8_t default_ats[] = {
        0x10, 0x78, 0x80, 0x70, 0x02, 0x00,
        0x31, 0xC1, 0x64, 0x09, 0x97, 0x61,
        0x26, 0x00, 0x90, 0x00
    };
    info.res_coll.ats.length = sizeof(default_ats);
    memcpy(info.res_coll.ats.data, default_ats, sizeof(default_ats));
    info.static_resp_count = 0;

    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, TAG_SENSE_HF, &map_info);
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(info), &info);
    NRF_LOG_INFO("14A-4 factory slot %d: %s", slot, ret ? "OK" : "FAIL");
    return ret;
}
