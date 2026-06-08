/*
 * ble_relay.h
 *
 * Peer-to-peer BLE relay link between two ChameleonUltra devices.
 *
 * Architecture:
 *   Both CUs advertise a custom relay service UUID when armed.
 *   The unit with the lower BLE MAC address acts as CENTRAL (scans and
 *   connects) and takes the RELAY_CARD role (NFCT, faces the reader).
 *   The unit with the higher MAC acts as PERIPHERAL (accepts) and takes
 *   the RELAY_READER role (RC522, faces the real card).
 *
 * After connection, both exchange their MAC via RELAY_CTRL NOTIFY.  The
 * role tiebreaker is resolved in software; the underlying BLE roles are
 * fixed by who connected to whom.
 *
 * GATT layout (on the RELAY_READER / peripheral side):
 *   Service  BLE_RELAY_SERVICE_UUID  (vendor 128-bit)
 *     RELAY_CTRL  (WRITE_WO_RSP + NOTIFY)  role negotiation, status
 *     RELAY_FRAME (WRITE_WO_RSP)           NFC frame: card→relay_reader
 *     RELAY_RESP  (NOTIFY)                 card response: relay_reader→card
 *
 * Message wire format on all characteristics:
 *   [u8 type][u8 reserved][u16 bits_le (0 for non-frame msgs)][u8[] payload]
 */

#ifndef BLE_RELAY_H
#define BLE_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include "ble.h"
#include "ble_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Service and characteristic UUIDs
 * 128-bit base: 43 55 52 45 4C 41 59 00 00 00 00 00 00 00 00 00
 *               C  U  R  E  L  A  Y  - (little-endian UUID base)
 * ----------------------------------------------------------------------- */
#define BLE_RELAY_UUID_TYPE             BLE_UUID_TYPE_VENDOR_BEGIN
#define BLE_RELAY_UUID_BASE             { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x59, 0x41, 0x4C, 0x45, 0x52, 0x55, 0x43, 0x00  \
}
#define BLE_RELAY_SERVICE_UUID          0x0001
#define BLE_RELAY_CTRL_UUID             0x0002
#define BLE_RELAY_FRAME_UUID            0x0003
#define BLE_RELAY_RESP_UUID             0x0004

/* -----------------------------------------------------------------------
 * Message types
 * ----------------------------------------------------------------------- */
#define BLE_RELAY_MSG_HELLO             0x01  /* {u8[6] ble_addr} */
#define BLE_RELAY_MSG_ROLE_CONFIRM      0x02  /* {u8 role} */
#define BLE_RELAY_MSG_CARD_IDENTITY     0x03  /* {u8[2] atqa, u8 sak, u8 uid_len, u8[uid_len] uid, u8 ats_len, u8[ats_len] ats} */
#define BLE_RELAY_MSG_PREAUTH           0x04  /* {u8 block, u8 key_type, u8[4] nt} */
#define BLE_RELAY_MSG_READY             0x05  /* {} peer is ready for reader */
#define BLE_RELAY_MSG_FRAME             0x06  /* {u16 bits_le, u8[] frame} NFC frame */
#define BLE_RELAY_MSG_RESPONSE          0x07  /* {u16 bits_le, u8[] response} */
#define BLE_RELAY_MSG_NO_RESPONSE       0x08  /* {} card did not respond */
#define BLE_RELAY_MSG_ERROR             0x09  /* {u8 code} */
#define BLE_RELAY_MSG_RESCAN_REQ        0x0A  /* {} CARD->READER: rescan for new card */
#define BLE_RELAY_MSG_FIELD_ON          0x0B  /* {} reserved */
#define BLE_RELAY_MSG_FIELD_OFF         0x0C  /* {} reserved */

#define BLE_RELAY_ROLE_CARD             0     /* NFCT, faces reader */
#define BLE_RELAY_ROLE_READER           1     /* RC522, faces real card */

#define BLE_RELAY_MAX_PAYLOAD           244   /* ATT MTU 247 - 3 header */
#define BLE_RELAY_MSG_HEADER_SIZE       4     /* type(1) + reserved(1) + bits_le(2) */

typedef enum {
    BLE_RELAY_STATE_IDLE = 0,
    BLE_RELAY_STATE_STARTING,       /* advertising + scanning simultaneously */
    BLE_RELAY_STATE_CONNECTING,     /* connect in progress (central side) */
    BLE_RELAY_STATE_DISCOVERING,    /* GATT service discovery */
    BLE_RELAY_STATE_NEGOTIATING,    /* role negotiation */
    BLE_RELAY_STATE_READY,          /* linked and role confirmed */
    BLE_RELAY_STATE_ACTIVE,         /* relay in progress */
    BLE_RELAY_STATE_ERROR,
} ble_relay_state_t;

/* Card identity pre-fetched by the relay_reader unit */
typedef struct {
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t uid[7];
    uint8_t uid_len;        /* 4 or 7 */
    uint8_t ats[32];
    uint8_t ats_len;        /* 0 if card does not support ISO14443-4 */
} relay_card_identity_t;

/* Pre-auth data: relay_reader has already run AUTH → got NT, is waiting
 * for the relay_card to forward NR||AR */
typedef struct {
    uint8_t block;
    uint8_t key_type;       /* PICC_AUTHENT1A or PICC_AUTHENT1B */
    uint8_t nt[4];
} relay_preauth_t;

/* -----------------------------------------------------------------------
 * Callbacks delivered to mode_relay.c
 * All called from main-loop context (deferred from BLE ISR via flag).
 * ----------------------------------------------------------------------- */
typedef void (*ble_relay_connected_cb_t)(uint8_t my_role);
typedef void (*ble_relay_card_identity_cb_t)(const relay_card_identity_t *id);
typedef void (*ble_relay_rescan_req_cb_t)(void);
typedef void (*ble_relay_field_on_cb_t)(void);
typedef void (*ble_relay_field_off_cb_t)(void);
typedef void (*ble_relay_preauth_cb_t)(const relay_preauth_t *pa);
typedef void (*ble_relay_ready_cb_t)(void);
typedef void (*ble_relay_frame_cb_t)(const uint8_t *data, uint16_t bits);
typedef void (*ble_relay_response_cb_t)(const uint8_t *data, uint16_t bits);
typedef void (*ble_relay_no_response_cb_t)(void);
typedef void (*ble_relay_disconnected_cb_t)(void);

typedef struct {
    ble_relay_connected_cb_t     on_connected;
    ble_relay_card_identity_cb_t on_card_identity;
    ble_relay_rescan_req_cb_t    on_rescan_req;
    ble_relay_field_on_cb_t      on_field_on;
    ble_relay_field_off_cb_t     on_field_off;
    ble_relay_preauth_cb_t       on_preauth;
    ble_relay_ready_cb_t         on_ready;
    ble_relay_frame_cb_t         on_frame;
    ble_relay_response_cb_t      on_response;
    ble_relay_no_response_cb_t   on_no_response;
    ble_relay_disconnected_cb_t  on_disconnected;
} ble_relay_callbacks_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/** Register callbacks and register the relay GATT service (call once at boot). */
void ble_relay_init(const ble_relay_callbacks_t *cbs);

/** Start advertising the relay service AND scanning for a relay peer.
 *  Call when the mode arms. */
void ble_relay_start(void);

/** Disconnect and stop advertising/scanning. */
void ble_relay_stop(void);
void ble_relay_set_fast_mode(bool fast);

/* Restart scanning — safe to call while already scanning */
void ble_relay_restart_scan(void);

/* Re-broadcast HELLO — call periodically from on_tick while in RS_LINKING */
void ble_relay_broadcast_hello(void);

/* Address accessors (6-byte little-endian BLE address) */
void ble_relay_get_my_addr(uint8_t out[6]);
void ble_relay_get_peer_addr(uint8_t out[6]);

ble_relay_state_t ble_relay_get_state(void);
uint8_t           ble_relay_get_role(void);
uint32_t          ble_relay_get_adv_reports(void);
uint32_t          ble_relay_get_relay_hits(void);

/* Called from on_tick() to process deferred BLE events in main-loop context. */
void ble_relay_process(void);

/* --- RELAY_CARD sends --- */
bool ble_relay_send_frame(const uint8_t *data, uint16_t bits);

/* --- RELAY_READER sends --- */
bool ble_relay_send_card_identity(const relay_card_identity_t *id);
bool ble_relay_send_rescan_req(void);
bool ble_relay_send_field_on(void);
bool ble_relay_send_field_off(void);
bool ble_relay_send_preauth(const relay_preauth_t *pa);
bool ble_relay_send_ready(void);
bool ble_relay_send_response(const uint8_t *data, uint16_t bits);
bool ble_relay_send_no_response(void);

/* BLE event hook — register with NRF_SDH_BLE_OBSERVER in ble_relay.c */

#ifdef __cplusplus
}
#endif

#endif /* BLE_RELAY_H */
