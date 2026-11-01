#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb_ccid_protocol.h"

#define PC_SET_PARAMETERS    0x61U
#define PC_POWER_ON          0x62U
#define PC_POWER_OFF         0x63U
#define PC_SLOT_STATUS       0x65U
#define PC_ESCAPE            0x6BU
#define PC_GET_PARAMETERS    0x6CU
#define PC_RESET_PARAMETERS  0x6DU
#define PC_XFR_BLOCK         0x6FU
#define PC_ABORT             0x72U
#define PC_RATE_CLOCK        0x73U

#define RDR_DATA_BLOCK       0x80U
#define RDR_SLOT_STATUS      0x81U
#define RDR_PARAMETERS       0x82U
#define RDR_ESCAPE           0x83U
#define RDR_RATE_CLOCK       0x84U

static bool mock_present;
static bool mock_active;
static bool drop_card_during_exchange;
static usb_ccid_reader_result_t power_on_result;
static usb_ccid_reader_result_t exchange_result;
static unsigned power_on_calls;
static unsigned power_off_calls;
static unsigned exchange_calls;
static uint8_t received_apdu[USB_CCID_MAX_PAYLOAD];
static size_t received_apdu_length;

static const uint8_t mock_atr[] = {
    0x3B, 0x8F, 0x80, 0x01, 0x80, 0x4F, 0x0C, 0xA0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6A,
};
static const uint8_t uid_response[] = {0x12, 0x85, 0x44, 0x8F, 0x90, 0x00};
static const uint8_t get_uid[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
static const uint8_t t1_parameters[] = {0x11, 0x10, 0x00, 0x4D, 0x00, 0xFE, 0x00};

void usb_ccid_reader_get_state(bool *present, bool *active) {
    *present = mock_present;
    *active = mock_active;
}

usb_ccid_reader_result_t usb_ccid_reader_power_on(
    uint8_t *atr,
    size_t capacity,
    size_t *length
) {
    power_on_calls++;
    if (power_on_result == USB_CCID_READER_OK) {
        assert(capacity >= sizeof(mock_atr));
        memcpy(atr, mock_atr, sizeof(mock_atr));
        *length = sizeof(mock_atr);
        mock_present = true;
        mock_active = true;
    }
    return power_on_result;
}

void usb_ccid_reader_power_off(void) {
    power_off_calls++;
    mock_active = false;
}

usb_ccid_reader_result_t usb_ccid_reader_transceive(
    const uint8_t *apdu,
    size_t apdu_length,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length
) {
    exchange_calls++;
    assert(apdu_length <= sizeof(received_apdu));
    memcpy(received_apdu, apdu, apdu_length);
    received_apdu_length = apdu_length;
    if (drop_card_during_exchange) {
        mock_present = false;
        mock_active = false;
    }
    if (exchange_result == USB_CCID_READER_OK) {
        assert(response_capacity >= sizeof(uid_response));
        memcpy(response, uid_response, sizeof(uid_response));
        *response_length = sizeof(uid_response);
    }
    return exchange_result;
}

static void put_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t get_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void fixture(usb_ccid_protocol_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    mock_present = true;
    mock_active = true;
    drop_card_during_exchange = false;
    power_on_result = USB_CCID_READER_OK;
    exchange_result = USB_CCID_READER_OK;
    power_on_calls = 0U;
    power_off_calls = 0U;
    exchange_calls = 0U;
    received_apdu_length = 0U;
    memset(received_apdu, 0, sizeof(received_apdu));
}

static size_t request(
    uint8_t *message,
    uint8_t command,
    uint8_t slot,
    uint8_t sequence,
    const uint8_t *payload,
    size_t payload_length
) {
    memset(message, 0, USB_CCID_MAX_MESSAGE);
    message[0] = command;
    put_le32(&message[1], (uint32_t)payload_length);
    message[5] = slot;
    message[6] = sequence;
    if (payload_length != 0U) {
        memcpy(&message[USB_CCID_HEADER_SIZE], payload, payload_length);
    }
    return USB_CCID_HEADER_SIZE + payload_length;
}

static void assert_response(
    const usb_ccid_protocol_t *ctx,
    uint8_t type,
    uint8_t slot,
    uint8_t sequence,
    uint8_t status,
    uint8_t error,
    uint8_t parameter,
    size_t payload_length
) {
    assert(ctx->tx_length == USB_CCID_HEADER_SIZE + payload_length);
    assert(ctx->tx[0] == type);
    assert(get_le32(&ctx->tx[1]) == payload_length);
    assert(ctx->tx[5] == slot);
    assert(ctx->tx[6] == sequence);
    assert(ctx->tx[7] == status);
    assert(ctx->tx[8] == error);
    assert(ctx->tx[9] == parameter);
}

static void test_truncated_headers(void) {
    for (size_t length = 0U; length < USB_CCID_HEADER_SIZE; length++) {
        usb_ccid_protocol_t ctx;
        fixture(&ctx);
        uint8_t message[USB_CCID_MAX_MESSAGE];
        (void)request(message, PC_POWER_ON, 0U, 0xA7U, NULL, 0U);
        usb_ccid_protocol_process(&ctx, message, length);
        uint8_t type = length > 0U ? RDR_DATA_BLOCK : RDR_SLOT_STATUS;
        uint8_t sequence = length > 6U ? 0xA7U : 0U;
        assert_response(&ctx, type, 0U, sequence, 0x40U, 0x01U, 0U, 0U);
        assert(power_on_calls == 0U);
    }
}

static void test_bad_lengths_are_rejected_before_backend(void) {
    const uint32_t declared[] = {1U, USB_CCID_MAX_PAYLOAD + 1U, UINT32_MAX};
    for (size_t i = 0U; i < sizeof(declared) / sizeof(declared[0]); i++) {
        usb_ccid_protocol_t ctx;
        fixture(&ctx);
        uint8_t message[USB_CCID_MAX_MESSAGE];
        size_t length = request(message, PC_POWER_ON, 0U, 0x21U, NULL, 0U);
        put_le32(&message[1], declared[i]);
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x21U, 0x40U, 1U, 0U, 0U);
        assert(power_on_calls == 0U);
    }

    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_POWER_ON, 0U, 0x22U, NULL, 0U);
    message[length++] = 0U; /* Extra bytes are not silently ignored. */
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x22U, 0x40U, 1U, 0U, 0U);
    assert(power_on_calls == 0U);
}

static void test_invalid_slot_is_echoed(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_POWER_ON, 0xFEU, 0xABU, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0xFEU, 0xABU, 0x40U, 5U, 0U, 0U);
    assert(power_on_calls == 0U);
}

static void test_power_on_and_off_status(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    mock_active = false;
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_POWER_ON, 0U, 0x31U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x31U, 0U, 0U, 0U, sizeof(mock_atr));
    assert(memcmp(&ctx.tx[USB_CCID_HEADER_SIZE], mock_atr, sizeof(mock_atr)) == 0);
    assert(power_on_calls == 1U);
    assert(ctx.present && ctx.active && ctx.slot_changed);

    ctx.slot_changed = false;
    length = request(message, PC_POWER_OFF, 0U, 0x32U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x32U, 1U, 0U, 1U, 0U);
    assert(power_off_calls == 1U);
    assert(ctx.present && !ctx.active && !ctx.slot_changed);
}

static void test_power_on_parameter_and_payload_errors(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_POWER_ON, 0U, 1U, NULL, 0U);
    message[7] = 4U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 1U, 0x40U, 7U, 0U, 0U);

    const uint8_t byte = 0U;
    length = request(message, PC_POWER_ON, 0U, 2U, &byte, 1U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 2U, 0x40U, 1U, 0U, 0U);
    assert(power_on_calls == 0U);
}

static void test_reader_error_mapping(void) {
    const usb_ccid_reader_result_t results[] = {
        USB_CCID_READER_NO_CARD, USB_CCID_READER_BUSY,
        USB_CCID_READER_UNSUPPORTED, USB_CCID_READER_BAD_APDU,
        USB_CCID_READER_OVERFLOW, USB_CCID_READER_IO_ERROR,
    };
    const uint8_t errors[] = {0xFEU, 0xE0U, 0xF6U, 1U, 0xFCU, 0xFBU};
    for (size_t i = 0U; i < sizeof(results) / sizeof(results[0]); i++) {
        usb_ccid_protocol_t ctx;
        fixture(&ctx);
        mock_present = false;
        mock_active = false;
        power_on_result = results[i];
        uint8_t message[USB_CCID_MAX_MESSAGE];
        size_t length = request(message, PC_POWER_ON, 0U, (uint8_t)i, NULL, 0U);
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, RDR_DATA_BLOCK, 0U, (uint8_t)i,
                        0x42U, errors[i], 0U, 0U);

        fixture(&ctx);
        exchange_result = results[i];
        length = request(message, PC_XFR_BLOCK, 0U, (uint8_t)i, get_uid, sizeof(get_uid));
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, RDR_DATA_BLOCK, 0U, (uint8_t)i,
                        0x40U, errors[i], 0U, 0U);
    }
}

static void test_uid_exchange_and_short_apdu_limit(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_XFR_BLOCK, 0U, 0x41U, get_uid, sizeof(get_uid));
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x41U, 0U, 0U, 0U, sizeof(uid_response));
    assert(memcmp(&ctx.tx[USB_CCID_HEADER_SIZE], uid_response, sizeof(uid_response)) == 0);
    assert(received_apdu_length == sizeof(get_uid));
    assert(memcmp(received_apdu, get_uid, sizeof(get_uid)) == 0);

    uint8_t largest_apdu[USB_CCID_MAX_PAYLOAD] = {0};
    length = request(message, PC_XFR_BLOCK, 0U, 0x42U, largest_apdu, sizeof(largest_apdu));
    usb_ccid_protocol_process(&ctx, message, length);
    assert(received_apdu_length == sizeof(largest_apdu));
    assert(ctx.tx[7] == 0U);
}

static void test_exchange_preconditions(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_XFR_BLOCK, 0U, 1U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 1U, 0x40U, 1U, 0U, 0U);

    length = request(message, PC_XFR_BLOCK, 0U, 2U, get_uid, sizeof(get_uid));
    message[8] = 1U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 2U, 0x40U, 8U, 0U, 0U);
    message[8] = 0U;
    message[9] = 1U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 2U, 0x40U, 8U, 0U, 0U);

    mock_active = false;
    message[9] = 0U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 2U, 0x41U, 0xFEU, 0U, 0U);
    assert(exchange_calls == 0U);
}

static void test_slot_status_and_presence_changes(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_SLOT_STATUS, 0U, 0x51U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x51U, 0U, 0U, 0U, 0U);
    assert(ctx.slot_changed);
    ctx.slot_changed = false;
    usb_ccid_protocol_sync(&ctx);
    assert(!ctx.slot_changed);

    mock_active = false;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x51U, 1U, 0U, 1U, 0U);
    assert(!ctx.slot_changed);

    mock_present = false;
    mock_active = true; /* Defensive: absent always wins over a stale active bit. */
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x51U, 2U, 0U, 1U, 0U);
    assert(ctx.slot_changed && !ctx.active);
}

static void test_exchange_failure_reconciles_slot(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    usb_ccid_protocol_sync(&ctx);
    ctx.slot_changed = false;
    drop_card_during_exchange = true;
    exchange_result = USB_CCID_READER_NO_CARD;
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_XFR_BLOCK, 0U, 0x61U, get_uid, sizeof(get_uid));
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x61U, 0x42U, 0xFEU, 0U, 0U);
    assert(!ctx.present && !ctx.active && ctx.slot_changed);
}

static void test_fixed_t1_parameters(void) {
    const uint8_t get_commands[] = {PC_GET_PARAMETERS, PC_RESET_PARAMETERS};
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    for (size_t i = 0U; i < sizeof(get_commands); i++) {
        size_t length = request(message, get_commands[i], 0U, 0x71U, NULL, 0U);
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, RDR_PARAMETERS, 0U, 0x71U, 0U, 0U, 1U, sizeof(t1_parameters));
        assert(memcmp(&ctx.tx[USB_CCID_HEADER_SIZE], t1_parameters, sizeof(t1_parameters)) == 0);
    }
    size_t length = request(message, PC_SET_PARAMETERS, 0U, 0x72U,
                            t1_parameters, sizeof(t1_parameters));
    message[7] = 1U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_PARAMETERS, 0U, 0x72U, 0U, 0U, 1U, sizeof(t1_parameters));
    message[7] = 0U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_PARAMETERS, 0U, 0x72U, 0x40U, 7U, 0U, 0U);

    message[7] = 1U;
    for (size_t i = 0U; i < sizeof(t1_parameters); i++) {
        message[USB_CCID_HEADER_SIZE + i] ^= 1U;
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, RDR_PARAMETERS, 0U, 0x72U, 0x40U,
                        (uint8_t)(USB_CCID_HEADER_SIZE + i), 1U, sizeof(t1_parameters));
        assert(memcmp(&ctx.tx[USB_CCID_HEADER_SIZE], t1_parameters, sizeof(t1_parameters)) == 0);
        message[USB_CCID_HEADER_SIZE + i] ^= 1U;
    }
    put_le32(&message[1], sizeof(t1_parameters) - 1U);
    usb_ccid_protocol_process(&ctx, message, length - 1U);
    assert_response(&ctx, RDR_PARAMETERS, 0U, 0x72U, 0x40U, 1U, 0U, 0U);
}

static void test_clock_and_rate_parameters(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    uint8_t parameters[8];
    put_le32(parameters, USB_CCID_CLOCK_KHZ);
    put_le32(&parameters[4], USB_CCID_DATA_RATE_BPS);
    size_t length = request(message, PC_RATE_CLOCK, 0U, 0x81U, parameters, sizeof(parameters));
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_RATE_CLOCK, 0U, 0x81U, 0U, 0U, 0U, sizeof(parameters));
    assert(memcmp(&ctx.tx[USB_CCID_HEADER_SIZE], parameters, sizeof(parameters)) == 0);
    message[USB_CCID_HEADER_SIZE] ^= 1U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_RATE_CLOCK, 0U, 0x81U, 0x40U, 10U, 0U, 0U);
    message[USB_CCID_HEADER_SIZE] ^= 1U;
    message[USB_CCID_HEADER_SIZE + 4U] ^= 1U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_RATE_CLOCK, 0U, 0x81U, 0x40U, 14U, 0U, 0U);
    put_le32(&message[1], 7U);
    usb_ccid_protocol_process(&ctx, message, length - 1U);
    assert_response(&ctx, RDR_RATE_CLOCK, 0U, 0x81U, 0x40U, 1U, 0U, 0U);
}

static void test_abort_requires_matching_control_request(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_ABORT, 0U, 0x91U, NULL, 0U);
    usb_ccid_protocol_abort(&ctx, 0x92U);
    assert(ctx.abort_pending && ctx.abort_sequence == 0x92U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x91U, 0x40U, 0xFFU, 0U, 0U);
    assert(ctx.abort_pending);
    message[6] = 0x92U;
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x92U, 0U, 0U, 0U, 0U);
    assert(!ctx.abort_pending);
    /* A late Abort is acknowledged without resetting somebody else's RF lease. */
    assert(power_off_calls == 0U);
    assert(mock_present && mock_active);
}

static void test_abort_accepts_bulk_before_control(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_ABORT, 0U, 0x93U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert(ctx.waiting_abort && ctx.waiting_abort_sequence == 0x93U);
    assert(!ctx.abort_pending);
    assert(ctx.tx_length == 0U);

    /* A bulk-first handshake has no response until its control half arrives. */
    usb_ccid_protocol_abort(&ctx, 0x93U);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x93U, 0U, 0U, 0U, 0U);
    assert(!ctx.waiting_abort && !ctx.abort_pending);
    assert(power_off_calls == 0U);

    fixture(&ctx);
    usb_ccid_protocol_process(&ctx, message, length);
    usb_ccid_protocol_abort(&ctx, 0x94U);
    assert(ctx.waiting_abort && ctx.abort_pending);
    assert(ctx.tx_length == 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x93U, 0x40U, 0xFFU, 0U, 0U);
    /* A subsequent matching control half can still finish the waiting pair. */
    usb_ccid_protocol_abort(&ctx, 0x93U);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0x93U, 0U, 0U, 0U, 0U);
    assert(!ctx.waiting_abort && !ctx.abort_pending);
    assert(power_off_calls == 0U);
}

static void test_pending_abort_rejects_normal_commands(void) {
    const uint8_t commands[] = {
        PC_POWER_ON, PC_POWER_OFF, PC_SLOT_STATUS, PC_GET_PARAMETERS, PC_XFR_BLOCK,
    };
    const uint8_t response_types[] = {
        RDR_DATA_BLOCK, RDR_SLOT_STATUS, RDR_SLOT_STATUS, RDR_PARAMETERS, RDR_DATA_BLOCK,
    };
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    usb_ccid_protocol_abort(&ctx, 0x95U);
    for (size_t i = 0U; i < sizeof(commands); i++) {
        uint8_t message[USB_CCID_MAX_MESSAGE];
        size_t length = request(
            message, commands[i], 0U, (uint8_t)i,
            commands[i] == PC_XFR_BLOCK ? get_uid : NULL,
            commands[i] == PC_XFR_BLOCK ? sizeof(get_uid) : 0U
        );
        usb_ccid_protocol_process(&ctx, message, length);
        assert_response(&ctx, response_types[i], 0U, (uint8_t)i,
                        0x40U, 0xFFU, 0U, 0U);
        assert(ctx.abort_pending);
    }
    assert(power_on_calls == 0U && power_off_calls == 0U && exchange_calls == 0U);

    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_ABORT, 0U, 0x95U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert(!ctx.abort_pending);
    length = request(message, PC_XFR_BLOCK, 0U, 0x96U, get_uid, sizeof(get_uid));
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0x96U, 0U, 0U, 0U, sizeof(uid_response));
    assert(exchange_calls == 1U);
}

static void test_unsupported_commands_use_correct_response_type(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_ESCAPE, 0U, 0xA1U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_ESCAPE, 0U, 0xA1U, 0x40U, 0U, 0U, 0U);
    length = request(message, 0xFFU, 0U, 0xA2U, NULL, 0U);
    usb_ccid_protocol_process(&ctx, message, length);
    assert_response(&ctx, RDR_SLOT_STATUS, 0U, 0xA2U, 0x40U, 0U, 0U, 0U);
    assert(power_on_calls == 0U && power_off_calls == 0U && exchange_calls == 0U);
}

static void test_full_packet_messages_complete_without_zlp(void) {
    const size_t lengths[] = {64U, 128U, 192U, 256U};
    for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        usb_ccid_protocol_t ctx;
        fixture(&ctx);
        uint8_t payload[USB_CCID_MAX_PAYLOAD] = {0};
        uint8_t message[USB_CCID_MAX_MESSAGE];
        size_t length = request(message, PC_XFR_BLOCK, 0U, (uint8_t)i,
                                payload, lengths[i] - USB_CCID_HEADER_SIZE);
        assert(length == lengths[i]);
        usb_ccid_rx_t rx = {0};
        for (size_t offset = 0U; offset < length; offset += USB_CCID_PACKET_SIZE) {
            bool complete = usb_ccid_rx_feed(&rx, &message[offset], USB_CCID_PACKET_SIZE);
            assert(complete == (offset + USB_CCID_PACKET_SIZE == length));
        }
        /* dwLength completes a message even when its last packet is 64 bytes. */
        assert(rx.length == length);
        assert(memcmp(rx.bytes, message, length) == 0);
        usb_ccid_protocol_process(&ctx, rx.bytes, rx.length);
        assert_response(&ctx, RDR_DATA_BLOCK, 0U, (uint8_t)i, 0U, 0U, 0U,
                        sizeof(uid_response));
        assert(received_apdu_length == length - USB_CCID_HEADER_SIZE);
    }
}

static void test_maximum_message_reassembly(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t payload[USB_CCID_MAX_PAYLOAD];
    for (size_t i = 0U; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }
    uint8_t message[USB_CCID_MAX_MESSAGE];
    size_t length = request(message, PC_XFR_BLOCK, 0U, 0xB1U, payload, sizeof(payload));
    assert(length == 271U);
    usb_ccid_rx_t rx = {0};
    for (size_t offset = 0U; offset < length;) {
        size_t remaining = length - offset;
        size_t packet = remaining < USB_CCID_PACKET_SIZE ? remaining : USB_CCID_PACKET_SIZE;
        bool complete = usb_ccid_rx_feed(&rx, &message[offset], packet);
        offset += packet;
        assert(complete == (offset == length));
    }
    assert(rx.length == sizeof(message));
    assert(memcmp(rx.bytes, message, length) == 0);
    usb_ccid_protocol_process(&ctx, rx.bytes, rx.length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0xB1U, 0U, 0U, 0U, sizeof(uid_response));
    assert(received_apdu_length == sizeof(payload));
    assert(memcmp(received_apdu, payload, sizeof(payload)) == 0);
}

static void test_short_packet_terminates_truncated_messages(void) {
    usb_ccid_protocol_t ctx;
    fixture(&ctx);
    uint8_t message[USB_CCID_MAX_MESSAGE];
    (void)request(message, PC_XFR_BLOCK, 0U, 0xB2U, NULL, 0U);
    put_le32(&message[1], USB_CCID_MAX_PAYLOAD);
    usb_ccid_rx_t rx = {0};
    assert(!usb_ccid_rx_feed(&rx, message, USB_CCID_PACKET_SIZE));
    assert(usb_ccid_rx_feed(&rx, &message[USB_CCID_PACKET_SIZE], 15U));
    assert(rx.length == 79U);
    usb_ccid_protocol_process(&ctx, rx.bytes, rx.length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0xB2U, 0x40U, 1U, 0U, 0U);
    assert(exchange_calls == 0U);

    /* A ZLP is also a short packet and terminates a truncated full-packet run. */
    memset(&rx, 0, sizeof(rx));
    for (unsigned i = 0U; i < 4U; i++) {
        assert(!usb_ccid_rx_feed(&rx, &message[i * USB_CCID_PACKET_SIZE],
                                 USB_CCID_PACKET_SIZE));
    }
    assert(usb_ccid_rx_feed(&rx, message, 0U));
    assert(rx.length == 256U);
    usb_ccid_protocol_process(&ctx, rx.bytes, rx.length);
    assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0xB2U, 0x40U, 1U, 0U, 0U);
    assert(exchange_calls == 0U);
}

static void test_oversized_packet_runs_preserve_header_and_bounds(void) {
    const uint32_t declared[] = {USB_CCID_MAX_PAYLOAD + 1U, UINT32_MAX};
    for (size_t scenario = 0U; scenario < sizeof(declared) / sizeof(declared[0]); scenario++) {
        struct {
            uint8_t before[16];
            usb_ccid_rx_t rx;
            uint8_t after[16];
        } guarded;
        memset(&guarded, 0xA5, sizeof(guarded));
        guarded.rx.length = 0U;
        uint8_t packet[USB_CCID_PACKET_SIZE] = {0};
        packet[0] = PC_XFR_BLOCK;
        put_le32(&packet[1], declared[scenario]);
        packet[6] = 0xB3U;
        assert(!usb_ccid_rx_feed(&guarded.rx, packet, sizeof(packet)));
        uint8_t header[USB_CCID_HEADER_SIZE];
        memcpy(header, packet, sizeof(header));
        memset(packet, 0x37, sizeof(packet));
        for (unsigned i = 0U; i < 5U; i++) {
            assert(!usb_ccid_rx_feed(&guarded.rx, packet, sizeof(packet)));
        }
        assert(guarded.rx.length == USB_CCID_MAX_MESSAGE + 1U);
        assert(memcmp(guarded.rx.bytes, header, sizeof(header)) == 0);
        assert(usb_ccid_rx_feed(&guarded.rx, packet, 1U));
        assert(guarded.rx.length == USB_CCID_MAX_MESSAGE + 1U);
        for (size_t i = 0U; i < sizeof(guarded.before); i++) {
            assert(guarded.before[i] == 0xA5U);
            assert(guarded.after[i] == 0xA5U);
        }

        usb_ccid_protocol_t ctx;
        fixture(&ctx);
        usb_ccid_protocol_process(&ctx, guarded.rx.bytes, guarded.rx.length);
        assert_response(&ctx, RDR_DATA_BLOCK, 0U, 0xB3U, 0x40U, 1U, 0U, 0U);
        assert(exchange_calls == 0U);
    }
}

int main(void) {
    test_truncated_headers();
    test_bad_lengths_are_rejected_before_backend();
    test_invalid_slot_is_echoed();
    test_power_on_and_off_status();
    test_power_on_parameter_and_payload_errors();
    test_reader_error_mapping();
    test_uid_exchange_and_short_apdu_limit();
    test_exchange_preconditions();
    test_slot_status_and_presence_changes();
    test_exchange_failure_reconciles_slot();
    test_fixed_t1_parameters();
    test_clock_and_rate_parameters();
    test_abort_requires_matching_control_request();
    test_abort_accepts_bulk_before_control();
    test_pending_abort_rejects_normal_commands();
    test_unsupported_commands_use_correct_response_type();
    test_full_packet_messages_complete_without_zlp();
    test_maximum_message_reassembly();
    test_short_packet_terminates_truncated_messages();
    test_oversized_packet_runs_preserve_header_and_bounds();
    puts("usb-ccid message protocol: 20 test groups passed");
    return 0;
}
