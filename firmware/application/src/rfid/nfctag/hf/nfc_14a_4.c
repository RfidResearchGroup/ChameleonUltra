/**
 * @file nfc_14a_4.c
 * @brief ISO14443-4 T=CL emulation for ChameleonUltra
 *
 * Implements ISO14443-4 tag transport with a static APDU response
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
#define PCB_RBLOCK_MASK     0xE6  /* R-block: bit8=1, bit7=0, bit6=1, bit3=0, bit2=1 */
#define PCB_RBLOCK_VAL      0xA2  /* R(ACK) = 0xA2/0xA3, R(NAK) = 0xB2/0xB3 */
#define PCB_SBLOCK_MASK     0xC0
#define PCB_SBLOCK_VAL      0xC0
#define PCB_BLOCK_NUM       0x01
#define PCB_CID_FOLLOWING   0x08  /* bit4: CID follows */
#define PCB_NAD_FOLLOWING   0x04  /* bit3: NAD follows */
#define PCB_CHAIN           0x10  /* bit5: chaining flag per ISO14443-4 Table 3 */
#define PCB_SBLOCK_WTX      0xF2
#define PCB_SBLOCK_DESELECT 0xC2
#define PCB_PPS             0xD0
#define WTX_VALUE           0x3B  /* WTXM=59 (~3s extra wait) */

static inline bool is_iblock(uint8_t pcb) {
    return (pcb & PCB_IBLOCK_MASK) == PCB_IBLOCK_VAL;
}
static inline bool is_rblock(uint8_t pcb) {
    return (pcb & PCB_RBLOCK_MASK) == PCB_RBLOCK_VAL;
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
static nfc_tag_14a_4_tcl_state_t m_tcl_session_state;

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

/* Cache the exact last block, including its original block number. Replaying a
 * lost ACK/response must never advance state or publish an APDU a second time. */
static void transmit(nfc_tag_14a_4_tcl_state_t *session, const uint8_t *data, uint16_t length) {
    memcpy(session->m_last_tx, data, length);
    session->m_last_tx_len = length;
    nfc_tag_14a_tx_bytes(session->m_last_tx, length, true);
}

static void retransmit(nfc_tag_14a_4_tcl_state_t *session) {
    if (session->m_last_tx_len != 0) {
        nfc_tag_14a_tx_bytes(session->m_last_tx, session->m_last_tx_len, true);
    }
}

static uint8_t picc_block_num(const nfc_tag_14a_4_tcl_state_t *session) {
    return session->m_block_num ^ 1u;
}

static uint8_t prologue(nfc_tag_14a_4_tcl_state_t *session, uint8_t pcb, uint8_t *buffer) {
    buffer[0] = pcb | (session->m_use_cid ? PCB_CID_FOLLOWING : 0);
    if (session->m_use_cid) {
        buffer[1] = session->m_cid;
        return 2;
    }
    return 1;
}

static void send_iblock(nfc_tag_14a_4_tcl_state_t *session) {
    uint8_t off = prologue(session, 0x02 | picc_block_num(session), m_tx_buf);
    uint16_t remaining = session->m_resp_len - session->m_resp_offset;
    uint16_t capacity = session->m_fsd - off - NFC_TAG_14A_CRC_LENGTH;
    uint16_t length = remaining > capacity ? capacity : remaining;
    session->m_chaining_out = length < remaining;
    if (session->m_chaining_out) m_tx_buf[0] |= PCB_CHAIN;
    memcpy(m_tx_buf + off, session->m_resp_buf + session->m_resp_offset, length);
    session->m_resp_offset += length;
    transmit(session, m_tx_buf, off + length);
}

static void send_rack(nfc_tag_14a_4_tcl_state_t *session) {
    uint8_t buffer[2];
    uint8_t length = prologue(session, PCB_RBLOCK_VAL | picc_block_num(session), buffer);
    transmit(session, buffer, length);
}

static void send_wtx(nfc_tag_14a_4_tcl_state_t *session) {
    uint8_t buffer[3];
    uint8_t off = prologue(session, PCB_SBLOCK_WTX, buffer);
    session->m_wtxm = WTX_VALUE;
    buffer[off++] = session->m_wtxm;
    session->m_wtx_pending = true;
    transmit(session, buffer, off);
}

/* CID comes from RATS, not from an arbitrary I-block addressed to another PICC. */
static bool addressed_to_session(const nfc_tag_14a_4_tcl_state_t *session,
                                const uint8_t *data, uint16_t length) {
    if (data[0] & PCB_CID_FOLLOWING) {
        return length >= 2 && session->m_cid_supported && data[1] == session->m_cid;
    }
    return !session->m_cid_supported || session->m_cid == 0;
}

/* ------------------------------------------------------------------ */
/*  State handler (called from NFCT ISR on each received frame)        */
/* ------------------------------------------------------------------ */

void nfc_tag_14a_4_base_respond(nfc_tag_14a_4_tcl_state_t *session) {
    if (!session->m_activated || !session->m_waiting_response || session->m_wtx_pending) return;
    if (session->m_response_ready) {
        session->m_response_ready = false;
        session->m_waiting_response = false;
        session->m_resp_offset = 0;
        send_iblock(session);
    } else {
        send_wtx(session);
    }
}

bool nfc_tag_14a_4_base_handler(nfc_tag_14a_4_tcl_state_t *session, uint8_t *data, uint16_t szBits) {
    if (!session->m_activated) return false;
    if (szBits < 24 || (szBits & 7) != 0) {
        session->m_pps_allowed = false;
        return false;
    }
    uint16_t szBytes = szBits / 8;
    if (szBytes > session->m_fsc || !nfc_tag_14a_checks_crc(data, szBytes)) {
        session->m_pps_allowed = false;
        return false;
    }
    szBytes -= NFC_TAG_14A_CRC_LENGTH;
    uint8_t pcb = data[0];

    if ((pcb & 0xf0) == PCB_PPS) {
        if (!session->m_pps_allowed || (pcb & 0x0f) != session->m_cid) return false;
        session->m_pps_allowed = false;
        /* NFCT only supports 106 kbit/s. Never acknowledge a faster rate. */
        if ((szBytes == 2 && data[1] == 0x01) ||
                (szBytes == 3 && data[1] == 0x11 && data[2] == 0x00)) {
            nfc_tag_14a_tx_bytes(data, 1, true);
        }
        return false;
    }

    if (!addressed_to_session(session, data, szBytes)) return false;
    session->m_pps_allowed = false;
    uint8_t prologue_length = (pcb & PCB_CID_FOLLOWING) ? 2 : 1;

    /* S-blocks are pairs: only a matching outstanding WTX can release data. */
    if (is_sblock(pcb)) {
        if ((pcb & (uint8_t)~PCB_CID_FOLLOWING) == PCB_SBLOCK_DESELECT) {
            if (szBytes != prologue_length) return false;
            nfc_tag_14a_tx_bytes(data, szBytes, true);
            nfc_tag_14a_4_reset_state(session);
            nfc_tag_14a_set_state(NFC_TAG_STATE_14A_HALTED);
        } else if ((pcb & (uint8_t)~PCB_CID_FOLLOWING) == PCB_SBLOCK_WTX) {
            if (szBytes != prologue_length + 1 || !session->m_wtx_pending ||
                    ((pcb & PCB_CID_FOLLOWING) != 0) != session->m_use_cid ||
                    data[prologue_length] != session->m_wtxm) return false;
            session->m_wtx_pending = false;
            nfc_tag_14a_4_base_respond(session);
        }
        return false;
    }

    if (is_rblock(pcb)) {
        if (szBytes != prologue_length) return false;
        uint8_t reader_bn = pcb & PCB_BLOCK_NUM;
        if (reader_bn == picc_block_num(session)) {
            retransmit(session);
        } else if (pcb & 0x10) {
            session->m_use_cid = (pcb & PCB_CID_FOLLOWING) != 0;
            send_rack(session);
        } else if (session->m_chaining_out) {
            session->m_block_num ^= 1;
            send_iblock(session);
        }
        return false;
    }

    if (is_iblock(pcb)) {
        /* Fixed PCB bits must be valid; NAD transport is not implemented. */
        if ((pcb & 0xe2) != 0x02 || (pcb & PCB_NAD_FOLLOWING) ||
                szBytes < prologue_length) return false;
        uint8_t reader_bn = pcb & PCB_BLOCK_NUM;
        if (reader_bn != session->m_block_num) {
            retransmit(session);
            return false;
        }
        if (session->m_waiting_response || session->m_chaining_out) return false;
        uint16_t length = szBytes - prologue_length;
        uint16_t previous = session->m_chaining_in ? session->m_apdu_len : 0;
        if (length > NFC_14A_4_MAX_APDU - previous) return false;

        session->m_use_cid = (pcb & PCB_CID_FOLLOWING) != 0;
        memcpy(session->m_apdu_buf + previous, data + prologue_length, length);
        session->m_apdu_len = previous + length;
        session->m_chaining_in = (pcb & PCB_CHAIN) != 0;
        session->m_apdu_pending = !session->m_chaining_in;
        session->m_resp_len = 0;
        session->m_response_ready = false;
        session->m_block_num ^= 1; /* Accept once; ACK carries the accepted BN. */
        m_dbg_iblocks_rx++;
        m_dbg_last_rx_pcb = pcb;
        if (session->m_chaining_in) {
            send_rack(session);
            return false;
        }
        session->m_waiting_response = true;
        if (session->m_apdu_len == 0) {
            /* Empty I-block presence check; do not dispatch an empty APDU. */
            session->m_apdu_pending = false;
            session->m_response_ready = true;
            nfc_tag_14a_4_base_respond(session);
            return false;
        }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  State handler (called from NFCT ISR on each received frame)        */
/* ------------------------------------------------------------------ */

static void nfc_tag_14a_4_state_handler(uint8_t *data, uint16_t szBits) {
    if (!nfc_tag_14a_4_base_handler(&m_tcl_session_state, data, szBits)) return;
    
    /* APDU complete — check static table first, then WTX */
    uint8_t  *static_resp = NULL;
    uint16_t  static_len  = 0;
    bool _found = find_static_response(m_tcl_session_state.m_apdu_buf, m_tcl_session_state.m_apdu_len,
                                        &static_resp, &static_len);
    m_dbg_last_match = _found ? 1 : 0;
    NRF_LOG_INFO("14A4 find_static: found=%d static_len=%d resp_count=%d",
                    _found, static_len, m_static_resp_count);
    if (_found) {
        m_dbg_iblocks_tx++;
        memcpy(m_tcl_session_state.m_resp_buf, static_resp, static_len);
        m_tcl_session_state.m_resp_len = static_len;
        m_tcl_session_state.m_response_ready = true;
    }

    nfc_tag_14a_4_base_respond(&m_tcl_session_state);
}

/* ------------------------------------------------------------------ */
/*  APDU relay API (for host-driven responses)                         */
/* ------------------------------------------------------------------ */

bool nfc_tag_14a_4_get_pending_apdu(uint8_t *buf, uint16_t *length) {
    if (!m_tcl_session_state.m_apdu_pending) return false;
    m_tcl_session_state.m_apdu_pending = false;
    *length = m_tcl_session_state.m_apdu_len;
    memcpy(buf, m_tcl_session_state.m_apdu_buf, m_tcl_session_state.m_apdu_len);
    return true;
}

void nfc_tag_14a_4_set_response(const uint8_t *data, uint16_t length) {
    if (!m_tcl_session_state.m_activated || !m_tcl_session_state.m_waiting_response ||
            m_tcl_session_state.m_response_ready) return;
    if (length > NFC_14A_4_MAX_APDU) length = NFC_14A_4_MAX_APDU;
    memcpy(m_tcl_session_state.m_resp_buf, data, length);
    m_tcl_session_state.m_resp_len = length;
    m_tcl_session_state.m_response_ready = true;
}

/* ------------------------------------------------------------------ */
/*  Reset handler                                                       */
/* ------------------------------------------------------------------ */

void nfc_tag_14a_4_reset_state(nfc_tag_14a_4_tcl_state_t *m_tcl_session_state) {
    m_tcl_session_state->m_block_num      = 0;
    m_tcl_session_state->m_activated      = false;
    m_tcl_session_state->m_pps_allowed    = false;
    m_tcl_session_state->m_cid_supported  = false;
    m_tcl_session_state->m_cid            = 0;
    m_tcl_session_state->m_use_cid        = false;
    m_tcl_session_state->m_fsd            = 16;
    m_tcl_session_state->m_fsc            = 32;
    m_tcl_session_state->m_apdu_pending   = false;
    m_tcl_session_state->m_chaining_in    = false;
    m_tcl_session_state->m_response_ready = false;
    m_tcl_session_state->m_apdu_len       = 0;
    m_tcl_session_state->m_resp_len       = 0;
    m_tcl_session_state->m_waiting_response = false;
    m_tcl_session_state->m_wtx_pending    = false;
    m_tcl_session_state->m_wtxm          = 0;
    m_tcl_session_state->m_chaining_out   = false;
    m_tcl_session_state->m_resp_offset    = 0;
    m_tcl_session_state->m_last_tx_len    = 0;
}

void nfc_tag_14a_4_activate(nfc_tag_14a_4_tcl_state_t *session,
                          uint8_t rats_parameter, const nfc_14a_ats_t *ats) {
    static const uint16_t frame_sizes[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
    nfc_tag_14a_4_reset_state(session);
    if (ats == NULL || ats->length == 0 || (rats_parameter & 0x0f) == 15) return;
    uint8_t fsdi = rats_parameter >> 4;
    session->m_fsd = frame_sizes[fsdi > 8 ? 8 : fsdi];
    session->m_cid = rats_parameter & 0x0f;
    session->m_cid_supported = true; /* Default TC(1) = 0x02. */
    if (ats->length > 1) {
        uint8_t t0 = ats->data[1];
        uint8_t fsci = t0 & 0x0f;
        session->m_fsc = frame_sizes[fsci > 8 ? 8 : fsci];
        uint8_t tc_offset = 2 + ((t0 & 0x10) != 0) + ((t0 & 0x20) != 0);
        if (t0 & 0x40) {
            if (tc_offset >= ats->length) return;
            session->m_cid_supported = (ats->data[tc_offset] & 0x02) != 0;
        }
    }
    session->m_activated = true;
    session->m_pps_allowed = true;
}

static void nfc_tag_14a_4_rats_handler(uint8_t parameter) {
    if (m_tag_information != NULL) {
        nfc_tag_14a_4_activate(&m_tcl_session_state, parameter, &m_tag_information->res_coll.ats);
    }
}

void nfc_tag_14a_4_reset_handler(void) {
    nfc_tag_14a_4_reset_state(&m_tcl_session_state);
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
        .cb_rats      = nfc_tag_14a_4_rats_handler,
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
