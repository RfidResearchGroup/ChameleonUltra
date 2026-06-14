/*
 * ble_relay.c
 *
 * Connectionless peer-to-peer relay link between two ChameleonUltra devices.
 *
 * Uses advertising (Broadcaster) + scanning (Observer) — no BLE connections.
 * This requires ZERO changes to sdk_config.h because Observer/Broadcaster
 * roles do not need NRF_SDH_BLE_CENTRAL_LINK_COUNT > 0.
 *
 * Protocol:
 *   Both CUs embed relay data in their advertising manufacturer-specific field.
 *   Both CUs scan for the other's advertising packets.
 *   Role is determined by BLE MAC: lower MAC = RELAY_CARD, higher = RELAY_READER.
 *
 * Advertising manufacturer data format (28 bytes max):
 *   [0]     magic high : 0x52 ('R')
 *   [1]     magic low  : 0x4C ('L')
 *   [2]     msg type   : BLE_RELAY_MSG_*
 *   [3]     sequence   : incremented on every new payload
 *   [4..27] payload    : up to 24 bytes
 *
 * Company identifier in manufacturer AD: 0x4359 ('CY' for ChameleonUltra)
 *
 * Latency: ~40-80ms round-trip (scan interval + advertising interval).
 * WTX covers this for ISO14443-4.  MIFARE Classic is reader-dependent.
 */

#include "ble_relay.h"
#include "ble_main.h"
#include "utils/syssleep.h"


#include <string.h>
#include <stdint.h>

#include "ble.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "app_timer.h"

#define NRF_LOG_MODULE_NAME ble_relay
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

/* -----------------------------------------------------------------------
 * Protocol constants
 * ----------------------------------------------------------------------- */
#define RELAY_MFR_MAGIC_H   0x52   /* 'R' */
#define RELAY_MFR_MAGIC_L   0x4C   /* 'L' */
#define RELAY_MFR_COMPANY   0x4359 /* 'CY' */
/* Extended advertising payload limit. Legacy adv capped this at ~22 bytes
 * (31-byte ADV_IND minus framing), which truncated DESFire AES auth frames
 * (e.g. the 41-byte E(RndA||RndB') I-block). Extended advertising (BLE 5)
 * raises the AdvData limit to 255 bytes, so a single packet now carries any
 * DESFire auth frame and most file/EMV reads without fragmentation. */
#define RELAY_MAX_PAYLOAD   245
#define RELAY_HEADER_SIZE   4      /* magic(2) + type(1) + seq(1) */
#define RELAY_ADV_BUF_SIZE  255    /* extended advertising data buffer */

/* Scan parameters: 100ms interval, 50ms window */
/* Discovery phase — relaxed timing, conserves power */
#define RELAY_SCAN_INTERVAL   200   /* 125ms in 0.625ms units */
#define RELAY_SCAN_WINDOW      40   /* 25ms  */

/* Active relay phase — improved latency without starving SoftDevice.
 * 75% scan duty cycle: 48/64 units = 30ms/40ms.
 * Leaves ~10ms per cycle for advertising and SD processing.
 * Worst-case round-trip: ~80ms vs ~350ms original. */
#define RELAY_FAST_SCAN_INTERVAL  64   /* 40ms */
#define RELAY_FAST_SCAN_WINDOW    48   /* 30ms — 75% duty cycle */

/* -----------------------------------------------------------------------
 * Event queue (ISR → main loop)
 * ----------------------------------------------------------------------- */
#define RELAY_EVT_BUF_SIZE  16

typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint8_t  buf[RELAY_MAX_PAYLOAD];
    uint8_t  len;
    uint8_t  peer_addr[6];
} relay_evt_t;

static relay_evt_t       m_evt_buf[RELAY_EVT_BUF_SIZE];
static volatile uint8_t  m_evt_wr = 0;
static volatile uint8_t  m_evt_rd = 0;

static void evt_push(uint8_t type, uint8_t seq,
                     const uint8_t *peer, const uint8_t *data, uint8_t len) {
    uint8_t next = (m_evt_wr + 1) % RELAY_EVT_BUF_SIZE;
    if (next == m_evt_rd) { NRF_LOG_WARNING("relay: evt queue full"); return; }
    relay_evt_t *e = &m_evt_buf[m_evt_wr];
    e->type = type;
    e->seq  = seq;
    e->len  = (len > RELAY_MAX_PAYLOAD) ? RELAY_MAX_PAYLOAD : len;
    if (e->len && data) memcpy(e->buf, data, e->len);
    if (peer) memcpy(e->peer_addr, peer, 6);
    m_evt_wr = next;
}

static bool evt_pop(relay_evt_t *out) {
    if (m_evt_rd == m_evt_wr) return false;
    *out = m_evt_buf[m_evt_rd];
    m_evt_rd = (m_evt_rd + 1) % RELAY_EVT_BUF_SIZE;
    return true;
}

/* -----------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */
static struct {
    ble_relay_state_t state;
    uint8_t           role;
    bool              role_resolved;
    uint8_t           my_addr[6];
    uint8_t           peer_addr[6];
    uint8_t           tx_seq;       /* incremented on every new outbound msg */
    uint8_t           rx_seq;       /* last received seq from peer           */
    uint32_t          adv_reports;  /* total ADV_REPORT events received */
    uint32_t          relay_hits;   /* ADV_REPORT events matching relay magic */
} m_ctx;

static ble_relay_callbacks_t m_cbs;

/* -----------------------------------------------------------------------
 * BLE observer
 * ----------------------------------------------------------------------- */
static void relay_evt_handler(ble_evt_t const *p_evt, void *p_ctx);

NRF_SDH_BLE_OBSERVER(m_relay_observer, 2, relay_evt_handler, NULL);

/* -----------------------------------------------------------------------
 * Advertising helpers
 *
 * Takes over the existing ble_advertising handle (from ble_main.c) with
 * non-connectable relay data.  No second advertising set needed.
 * ----------------------------------------------------------------------- */
static uint8_t s_relay_adv_raw[RELAY_ADV_BUF_SIZE];

static void adv_send(uint8_t type, const uint8_t *data, uint8_t dlen) {
    /* Extended advertising allows up to 255 bytes of AdvData (vs 31 legacy). */
    if (dlen > RELAY_MAX_PAYLOAD) dlen = RELAY_MAX_PAYLOAD;

    /* Build raw AD bytes:
     *   02 01 06                  flags
     *   LL FF 59 43 MH ML T S ... manufacturer specific
     */
    uint8_t off = 0;
    s_relay_adv_raw[off++] = 2;
    s_relay_adv_raw[off++] = 0x01;
    s_relay_adv_raw[off++] = 0x06;

    uint8_t mfr_len = 2 + RELAY_HEADER_SIZE + dlen;
    s_relay_adv_raw[off++] = 1 + mfr_len;
    s_relay_adv_raw[off++] = 0xFF;
    s_relay_adv_raw[off++] = 0x59;               /* company ID 0x4359 low  */
    s_relay_adv_raw[off++] = 0x43;               /* company ID 0x4359 high */
    s_relay_adv_raw[off++] = RELAY_MFR_MAGIC_H;
    s_relay_adv_raw[off++] = RELAY_MFR_MAGIC_L;
    s_relay_adv_raw[off++] = type;
    s_relay_adv_raw[off++] = ++m_ctx.tx_seq;
    if (dlen && data) { memcpy(&s_relay_adv_raw[off], data, dlen); off += dlen; }

    ble_main_relay_adv_set(s_relay_adv_raw, off);
    NRF_LOG_DEBUG("relay: adv_send type=0x%02x seq=%u", type, m_ctx.tx_seq);
}

/* -----------------------------------------------------------------------
 * Scanning
 * ----------------------------------------------------------------------- */

/* File-scope scan state — must outlive each call to sd_ble_gap_scan_start.
 * Extended advertising reports can be up to 255 bytes, so the scan buffer
 * must be sized for extended (not BLE_GAP_SCAN_BUFFER_MIN which is for legacy). */
static ble_gap_scan_params_t s_scan_params;
static uint8_t               s_scan_buf[BLE_GAP_SCAN_BUFFER_EXTENDED_MIN];
static ble_data_t            s_scan_data;

static bool s_fast_scan = false;

static void start_scanning(void) {
    memset(&s_scan_params, 0, sizeof(s_scan_params));
    s_scan_params.extended  = 1;   /* receive extended advertising reports */
    s_scan_params.active    = 0;
    s_scan_params.interval  = s_fast_scan ? RELAY_FAST_SCAN_INTERVAL : RELAY_SCAN_INTERVAL;
    s_scan_params.window    = s_fast_scan ? RELAY_FAST_SCAN_WINDOW   : RELAY_SCAN_WINDOW;
    s_scan_params.timeout   = 0;
    s_scan_params.scan_phys = BLE_GAP_PHY_1MBPS;
    /* channel_mask all-zero = scan on all primary channels 37/38/39 */

    s_scan_data.p_data = s_scan_buf;
    s_scan_data.len    = sizeof(s_scan_buf);

    ret_code_t rc = sd_ble_gap_scan_start(&s_scan_params, &s_scan_data);
    if (rc != NRF_SUCCESS && rc != NRF_ERROR_INVALID_STATE) {
        NRF_LOG_WARNING("relay: scan start rc=%u", rc);
    } else {
        NRF_LOG_INFO("relay: scanning started");
    }
}

/* -----------------------------------------------------------------------
 * Role resolution
 * ----------------------------------------------------------------------- */
static void resolve_role(const uint8_t *peer_addr) {
    memcpy(m_ctx.peer_addr, peer_addr, 6);
    /* Lower MAC (byte 5 = MSB) = RELAY_CARD; higher = RELAY_READER */
    m_ctx.role = (memcmp(m_ctx.my_addr, peer_addr, 6) < 0)
                 ? BLE_RELAY_ROLE_CARD : BLE_RELAY_ROLE_READER;
    m_ctx.role_resolved = true;
    m_ctx.state         = BLE_RELAY_STATE_NEGOTIATING;
    NRF_LOG_INFO("relay: role=%s",
                 m_ctx.role == BLE_RELAY_ROLE_CARD ? "CARD" : "READER");
    NRF_LOG_INFO("relay: peer ...%02X:%02X:%02X:%02X",
                 peer_addr[3], peer_addr[2], peer_addr[1], peer_addr[0]);
}

/* -----------------------------------------------------------------------
 * Parse relay payload from an ADV_REPORT
 * Returns true if the advertisement contains valid relay data.
 * ----------------------------------------------------------------------- */
static bool parse_relay_adv(const ble_gap_evt_adv_report_t *rep,
                             uint8_t *out_type, uint8_t *out_seq,
                             const uint8_t **out_data, uint8_t *out_len) {
    const uint8_t *d = rep->data.p_data;
    uint16_t n = rep->data.len;
    uint8_t off = 0;
    while (off + 1 < n) {
        uint8_t adlen = d[off];
        uint8_t adtyp = d[off + 1];
        if (adtyp == 0xFF && adlen >= 7) {
            /* adlen = type(1) + company(2) + header(4) + payload
             * so payload_len = adlen - 7                         */
            uint16_t cid = d[off + 2] | ((uint16_t)d[off + 3] << 8);
            if (cid == RELAY_MFR_COMPANY &&
                d[off + 4] == RELAY_MFR_MAGIC_H &&
                d[off + 5] == RELAY_MFR_MAGIC_L) {
                *out_type = d[off + 6];
                *out_seq  = d[off + 7];
                *out_data = &d[off + 8];
                *out_len  = adlen - 7;   /* correct: not adlen-6 */
                return true;
            }
        }
        off += adlen + 1;
    }
    return false;
}

/* -----------------------------------------------------------------------
 * BLE event handler (ISR context)
 * ----------------------------------------------------------------------- */
static void relay_evt_handler(ble_evt_t const *p_evt, void *p_ctx) {
    (void)p_ctx;
    if (m_ctx.state == BLE_RELAY_STATE_IDLE) return;

    if (p_evt->header.evt_id == BLE_GAP_EVT_ADV_REPORT) {
        const ble_gap_evt_adv_report_t *rep =
            &p_evt->evt.gap_evt.params.adv_report;

        uint8_t type, seq;
        const uint8_t *data;
        uint8_t dlen;

        m_ctx.adv_reports++;
        if (parse_relay_adv(rep, &type, &seq, &data, &dlen)) {
            m_ctx.relay_hits++;
            const uint8_t *peer = rep->peer_addr.addr;

            /* Copy payload to a local buffer immediately — the scan buffer
             * (s_scan_buf / rep->data.p_data) is handed back to the SoftDevice
             * when we call sd_ble_gap_scan_start below, so we must not hold
             * a pointer into it past that point. */
            uint8_t payload_copy[RELAY_MAX_PAYLOAD];
            uint8_t payload_len = (dlen > RELAY_MAX_PAYLOAD) ? RELAY_MAX_PAYLOAD : dlen;
            if (payload_len && data) memcpy(payload_copy, data, payload_len);

            /* Ignore our own advertising (same MAC) */
            if (memcmp(peer, m_ctx.my_addr, 6) != 0) {
                /* Filter: if role resolved, only accept from known peer */
                if (!m_ctx.role_resolved ||
                    memcmp(peer, m_ctx.peer_addr, 6) == 0) {
                    /* Dedup: HELLO only once (until role resolved);
                     * all other message types dedup by seq number. */
                    bool should_queue = false;
                    if (type == BLE_RELAY_MSG_HELLO) {
                        should_queue = !m_ctx.role_resolved;
                    } else {
                        should_queue = (seq != m_ctx.rx_seq);
                    }
                    if (should_queue) {
                        m_ctx.rx_seq = seq;
                        evt_push(type, seq, peer, payload_copy, payload_len);
                    }
                }
            }
        }

        /* Resume scan — NULL params = continue with existing scan parameters.
         * Nordic SDK pattern: always use NULL after ADV_REPORT, not full params. */
        sd_ble_gap_scan_start(NULL, &s_scan_data);
    }
}

/* -----------------------------------------------------------------------
 * Event dispatch (main-loop context)
 * ----------------------------------------------------------------------- */
static void dispatch(const relay_evt_t *e) {
    switch (e->type) {
    case BLE_RELAY_MSG_HELLO:
        if (!m_ctx.role_resolved) {
            resolve_role(e->peer_addr);
            /* Reply with HELLO so peer can resolve too */
            adv_send(BLE_RELAY_MSG_HELLO, m_ctx.my_addr, 6);
            if (m_cbs.on_connected) m_cbs.on_connected(m_ctx.role);
            m_ctx.state = BLE_RELAY_STATE_READY;
        }
        break;

    case BLE_RELAY_MSG_ROLE_CONFIRM:
        m_ctx.state = BLE_RELAY_STATE_READY;
        break;

    case BLE_RELAY_MSG_CARD_IDENTITY:
        if (e->len >= 4 && m_cbs.on_card_identity) {
            relay_card_identity_t id;
            memset(&id, 0, sizeof(id));
            id.atqa[0] = e->buf[0];
            id.atqa[1] = e->buf[1];
            id.sak     = e->buf[2];
            id.uid_len = e->buf[3];
            uint8_t off = 4;
            if (id.uid_len > 7) id.uid_len = 7;
            if (off + id.uid_len <= e->len) {
                memcpy(id.uid, e->buf + off, id.uid_len);
                off += id.uid_len;
            }
            if (off < e->len) {
                id.ats_len = e->buf[off++];
                if (id.ats_len > 32) id.ats_len = 32;
                if (off + id.ats_len <= e->len)
                    memcpy(id.ats, e->buf + off, id.ats_len);
            }
            m_cbs.on_card_identity(&id);
        }
        break;

    case BLE_RELAY_MSG_RESCAN_REQ:
        if (m_cbs.on_rescan_req) m_cbs.on_rescan_req();
        break;
    case BLE_RELAY_MSG_FIELD_ON:
        if (m_cbs.on_field_on) m_cbs.on_field_on();
        break;
    case BLE_RELAY_MSG_FIELD_OFF:
        if (m_cbs.on_field_off) m_cbs.on_field_off();
        break;

    case BLE_RELAY_MSG_PREAUTH:
        if (e->len >= 6 && m_cbs.on_preauth) {
            relay_preauth_t pa;
            pa.block    = e->buf[0];
            pa.key_type = e->buf[1];
            memcpy(pa.nt, e->buf + 2, 4);
            m_cbs.on_preauth(&pa);
        }
        break;

    case BLE_RELAY_MSG_READY:
        if (m_cbs.on_ready) m_cbs.on_ready();
        break;

    case BLE_RELAY_MSG_FRAME:
        if (e->len > 0 && m_cbs.on_frame) {
            uint16_t bits = e->buf[0] | ((uint16_t)e->buf[1] << 8);
            m_cbs.on_frame(e->buf + 2, bits);
        }
        break;

    case BLE_RELAY_MSG_RESPONSE:
        if (e->len > 0 && m_cbs.on_response) {
            uint16_t bits = e->buf[0] | ((uint16_t)e->buf[1] << 8);
            m_cbs.on_response(e->buf + 2, bits);
        }
        break;

    case BLE_RELAY_MSG_NO_RESPONSE:
        if (m_cbs.on_no_response) m_cbs.on_no_response();
        break;

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void ble_relay_init(const ble_relay_callbacks_t *cbs) {
    memcpy(&m_cbs, cbs, sizeof(m_cbs));
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.state = BLE_RELAY_STATE_IDLE;

    ble_gap_addr_t addr;
    sd_ble_gap_addr_get(&addr);
    memcpy(m_ctx.my_addr, addr.addr, 6);

    NRF_LOG_INFO("relay: init addr ...%02X:%02X:%02X:%02X",
                 m_ctx.my_addr[3], m_ctx.my_addr[2],
                 m_ctx.my_addr[1], m_ctx.my_addr[0]);
}

void ble_relay_start(void) {
    m_ctx.state        = BLE_RELAY_STATE_STARTING;
    m_ctx.role_resolved= false;
    m_ctx.tx_seq       = 0;
    m_ctx.rx_seq       = 0xFF;

    /* Advertise HELLO with our MAC — re-broadcast every 2s from on_tick */
    s_fast_scan = false;
    adv_send(BLE_RELAY_MSG_HELLO, m_ctx.my_addr, 6);
    start_scanning();
    NRF_LOG_INFO("relay: started - broadcasting HELLO + scanning");
}

void ble_relay_stop(void) {
    s_fast_scan = false;
    sd_ble_gap_scan_stop();
    ble_main_relay_adv_restore();   /* restore normal CU advertising */
    m_ctx.state = BLE_RELAY_STATE_IDLE;
    NRF_LOG_INFO("relay: stopped");
}

/* Switch to fast scan + fast advertising for the active relay phase.
 * Call once both roles are connected and the relay loop starts.
 * Reduces worst-case round-trip from ~175ms to ~25ms. */
void ble_relay_set_fast_mode(bool fast) {
    if (s_fast_scan == fast) return;
    s_fast_scan = fast;
    /* Restart scan immediately with new parameters */
    sd_ble_gap_scan_stop();
    if (m_ctx.state != BLE_RELAY_STATE_IDLE) start_scanning();
    NRF_LOG_INFO("relay: %s scan mode", fast ? "FAST" : "normal");
}

ble_relay_state_t ble_relay_get_state(void)       { return m_ctx.state;       }
uint8_t           ble_relay_get_role(void)        { return m_ctx.role;        }
uint32_t          ble_relay_get_adv_reports(void) { return m_ctx.adv_reports; }
uint32_t          ble_relay_get_relay_hits(void)  { return m_ctx.relay_hits;  }
void ble_relay_get_my_addr(uint8_t out[6])        { memcpy(out, m_ctx.my_addr,   6); }
void ble_relay_get_peer_addr(uint8_t out[6])      { memcpy(out, m_ctx.peer_addr, 6); }

void ble_relay_restart_scan(void) {
    if (m_ctx.state != BLE_RELAY_STATE_IDLE) start_scanning();
}

void ble_relay_broadcast_hello(void) {
    if (m_ctx.state != BLE_RELAY_STATE_IDLE)
        adv_send(BLE_RELAY_MSG_HELLO, m_ctx.my_addr, 6);
}

void ble_relay_process(void) {
    /* Aggressively cancel any sleep timer — multiple events (BLE disconnect,
     * NFCT field lost) can start a 3-4s sleep timer. Processing BLE events
     * means we're active; prevent sleep regardless of other state flags. */
    sleep_timer_stop();
    relay_evt_t e;
    while (evt_pop(&e)) dispatch(&e);
}

/* -----------------------------------------------------------------------
 * Message senders
 * ----------------------------------------------------------------------- */
bool ble_relay_send_frame(const uint8_t *data, uint16_t bits) {
    if (!data || (bits + 7) / 8 > RELAY_MAX_PAYLOAD - 2) return false;
    uint8_t buf[2 + RELAY_MAX_PAYLOAD];
    uint8_t dlen = (bits + 7) / 8;
    buf[0] = (uint8_t)(bits      );
    buf[1] = (uint8_t)(bits >> 8 );
    memcpy(buf + 2, data, dlen);
    adv_send(BLE_RELAY_MSG_FRAME, buf, 2 + dlen);
    return true;
}

bool ble_relay_send_response(const uint8_t *data, uint16_t bits) {
    if (!data) return false;
    uint8_t buf[2 + RELAY_MAX_PAYLOAD];
    uint8_t dlen = (bits + 7) / 8;
    if (dlen > RELAY_MAX_PAYLOAD - 2) dlen = RELAY_MAX_PAYLOAD - 2;
    buf[0] = (uint8_t)(bits      );
    buf[1] = (uint8_t)(bits >> 8 );
    memcpy(buf + 2, data, dlen);
    adv_send(BLE_RELAY_MSG_RESPONSE, buf, 2 + dlen);
    return true;
}

bool ble_relay_send_no_response(void) {
    adv_send(BLE_RELAY_MSG_NO_RESPONSE, NULL, 0);
    return true;
}

bool ble_relay_send_ready(void) {
    adv_send(BLE_RELAY_MSG_READY, NULL, 0);
    return true;
}

bool ble_relay_send_card_identity(const relay_card_identity_t *id) {
    uint8_t p[24];
    uint8_t off = 0;
    p[off++] = id->atqa[0];
    p[off++] = id->atqa[1];
    p[off++] = id->sak;
    p[off++] = id->uid_len;
    uint8_t ulen = id->uid_len > 7 ? 7 : id->uid_len;
    memcpy(p + off, id->uid, ulen); off += ulen;
    uint8_t alen = id->ats_len > (24 - off - 1) ? (24 - off - 1) : id->ats_len;
    p[off++] = alen;
    if (alen) { memcpy(p + off, id->ats, alen); off += alen; }
    adv_send(BLE_RELAY_MSG_CARD_IDENTITY, p, off);
    return true;
}

bool ble_relay_send_rescan_req(void) { adv_send(BLE_RELAY_MSG_RESCAN_REQ, NULL, 0); return true; }
bool ble_relay_send_field_on(void)   { adv_send(BLE_RELAY_MSG_FIELD_ON,  NULL, 0); return true; }
bool ble_relay_send_field_off(void)  { adv_send(BLE_RELAY_MSG_FIELD_OFF, NULL, 0); return true; }

bool ble_relay_send_preauth(const relay_preauth_t *pa) {
    uint8_t p[6] = { pa->block, pa->key_type,
                     pa->nt[0], pa->nt[1], pa->nt[2], pa->nt[3] };
    adv_send(BLE_RELAY_MSG_PREAUTH, p, 6);
    return true;
}
