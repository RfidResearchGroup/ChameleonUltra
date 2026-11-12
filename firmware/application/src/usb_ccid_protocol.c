#include "usb_ccid_protocol.h"

#include <string.h>

/* USB CCID 1.1, sections 6.1 and 6.2: PC_to_RDR and RDR_to_PC messages. */
#define SET_PARAMETERS     0x61U
#define ICC_POWER_ON       0x62U
#define ICC_POWER_OFF      0x63U
#define GET_SLOT_STATUS    0x65U
#define ESCAPE             0x6BU
#define GET_PARAMETERS     0x6CU
#define RESET_PARAMETERS   0x6DU
#define XFR_BLOCK          0x6FU
#define ABORT              0x72U
#define SET_RATE_CLOCK     0x73U

#define DATA_BLOCK         0x80U
#define SLOT_STATUS        0x81U
#define PARAMETERS         0x82U
#define ESCAPE_RESPONSE    0x83U
#define RATE_CLOCK         0x84U

#define COMMAND_OK         0x00U
#define COMMAND_FAILED     0x40U
#define ICC_ACTIVE         0x00U
#define ICC_INACTIVE       0x01U
#define ICC_ABSENT         0x02U

#define ERROR_UNSUPPORTED  0x00U
#define ERROR_BAD_LENGTH   0x01U
#define ERROR_BAD_SLOT     0x05U
#define ERROR_SLOT_BUSY    0xE0U
#define ERROR_PROTOCOL     0xF6U
#define ERROR_HW           0xFBU
#define ERROR_OVERRUN      0xFCU
#define ERROR_MUTE         0xFEU
#define ERROR_ABORTED      0xFFU

/*
 * T=1 is the host's PC/SC transport for the storage-card pseudo ATR, not a
 * promise of T=CL APDU pass-through. Clock/rate parameters are fixed because
 * the contactless RF transport does not have a contact-card clock to change.
 */
static const uint8_t m_t1_parameters[] = {
    0x11, 0x10, 0x00, 0x4D, 0x00, 0xFE, 0x00,
};

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void write_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

bool usb_ccid_rx_feed(usb_ccid_rx_t *rx, const uint8_t *packet, size_t length) {
    /*
     * Nordic's default consumer waits for a short packet unless its entire
     * requested buffer is filled. Receive one packet at a time and stop at
     * dwLength instead: valid CCID messages may end on a 64-byte boundary.
     */
    if (rx->length <= USB_CCID_MAX_MESSAGE &&
            length <= USB_CCID_MAX_MESSAGE - rx->length) {
        memcpy(&rx->bytes[rx->length], packet, length);
        rx->length += length;
    } else {
        /* Keep the header for an error reply while draining oversized data. */
        rx->length = USB_CCID_MAX_MESSAGE + 1U;
    }
    if (rx->length >= USB_CCID_HEADER_SIZE && rx->length <= USB_CCID_MAX_MESSAGE) {
        uint32_t declared = read_le32(&rx->bytes[1]);
        if (declared <= USB_CCID_MAX_PAYLOAD &&
                rx->length >= USB_CCID_HEADER_SIZE + declared) {
            return true;
        }
    }
    /* A short packet also terminates a malformed or truncated host message. */
    return length < USB_CCID_PACKET_SIZE;
}

void usb_ccid_protocol_sync(usb_ccid_protocol_t *ctx) {
    bool present = false;
    bool active = false;
    usb_ccid_reader_get_state(&present, &active);
    if (present != ctx->present) {
        ctx->slot_changed = true;
    }
    ctx->present = present;
    ctx->active = present && active;
}

static uint8_t icc_status(const usb_ccid_protocol_t *ctx) {
    if (!ctx->present) {
        return ICC_ABSENT;
    }
    return ctx->active ? ICC_ACTIVE : ICC_INACTIVE;
}

static void response(
    usb_ccid_protocol_t *ctx,
    uint8_t type,
    uint32_t length,
    uint8_t slot,
    uint8_t sequence,
    uint8_t command_status,
    uint8_t error,
    uint8_t parameter
) {
    ctx->tx[0] = type;
    write_le32(&ctx->tx[1], length);
    ctx->tx[5] = slot;
    ctx->tx[6] = sequence;
    ctx->tx[7] = command_status | icc_status(ctx);
    ctx->tx[8] = error;
    ctx->tx[9] = parameter;
    ctx->tx_length = (uint16_t)(USB_CCID_HEADER_SIZE + length);
}

static uint8_t response_type(uint8_t command) {
    switch (command) {
        case ICC_POWER_ON:
        case XFR_BLOCK:        return DATA_BLOCK;
        case SET_PARAMETERS:
        case GET_PARAMETERS:
        case RESET_PARAMETERS: return PARAMETERS;
        case ESCAPE:           return ESCAPE_RESPONSE;
        case SET_RATE_CLOCK:   return RATE_CLOCK;
        default:               return SLOT_STATUS;
    }
}

static void fail(
    usb_ccid_protocol_t *ctx,
    uint8_t command,
    uint8_t slot,
    uint8_t sequence,
    uint8_t error
) {
    usb_ccid_protocol_sync(ctx);
    uint8_t type = response_type(command);
    /* Even a failed command reports the current ICC and clock state. */
    uint8_t parameter = type == SLOT_STATUS && !ctx->active ? 1U : 0U;
    response(ctx, type, 0U, slot, sequence, COMMAND_FAILED, error, parameter);
}

static uint8_t reader_error(usb_ccid_reader_result_t result) {
    switch (result) {
        case USB_CCID_READER_NO_CARD:     return ERROR_MUTE;
        case USB_CCID_READER_BUSY:        return ERROR_SLOT_BUSY;
        case USB_CCID_READER_UNSUPPORTED: return ERROR_PROTOCOL;
        case USB_CCID_READER_BAD_APDU:    return ERROR_BAD_LENGTH;
        case USB_CCID_READER_OVERFLOW:    return ERROR_OVERRUN;
        default:                         return ERROR_HW;
    }
}

void usb_ccid_protocol_abort(usb_ccid_protocol_t *ctx, uint8_t sequence) {
    /*
     * Control and bulk Abort form a handshake using their own sequence number,
     * not the number of the operation being interrupted. Accept either order.
     */
    if (ctx->waiting_abort && ctx->waiting_abort_sequence == sequence) {
        ctx->waiting_abort = false;
        ctx->abort_pending = false;
        usb_ccid_protocol_sync(ctx);
        response(ctx, SLOT_STATUS, 0U, 0U, sequence, COMMAND_OK, 0U, ctx->active ? 0U : 1U);
        return;
    }
    ctx->abort_pending = true;
    ctx->abort_sequence = sequence;
}

void usb_ccid_protocol_process(
    usb_ccid_protocol_t *ctx,
    const uint8_t *message,
    size_t message_length
) {
    uint8_t command = message_length > 0U ? message[0] : 0U;
    uint8_t slot = message_length > 5U ? message[5] : 0U;
    uint8_t sequence = message_length > 6U ? message[6] : 0U;
    ctx->tx_length = 0U;
    if (message_length < USB_CCID_HEADER_SIZE) {
        fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
        return;
    }
    if (slot != 0U) {
        fail(ctx, command, slot, sequence, ERROR_BAD_SLOT);
        return;
    }
    uint32_t length = read_le32(&message[1]);
    if (length > USB_CCID_MAX_PAYLOAD ||
            length != message_length - USB_CCID_HEADER_SIZE) {
        fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
        return;
    }

    usb_ccid_protocol_sync(ctx);
    if (ctx->abort_pending && command != ABORT) {
        /* CCID 1.1 section 5.3.1: reject commands until the Abort pair completes. */
        fail(ctx, command, slot, sequence, ERROR_ABORTED);
        return;
    }
    const uint8_t *payload = &message[USB_CCID_HEADER_SIZE];
    switch (command) {
        case ICC_POWER_ON: {
            if (length != 0U || message[7] > 3U) {
                fail(ctx, command, slot, sequence, length != 0U ? ERROR_BAD_LENGTH : 7U);
                return;
            }
            size_t atr_length = 0U;
            usb_ccid_reader_result_t result = usb_ccid_reader_power_on(
                &ctx->tx[USB_CCID_HEADER_SIZE], USB_CCID_MAX_PAYLOAD, &atr_length
            );
            if (result != USB_CCID_READER_OK) {
                fail(ctx, command, slot, sequence, reader_error(result));
                return;
            }
            usb_ccid_protocol_sync(ctx);
            response(ctx, DATA_BLOCK, atr_length, slot, sequence, COMMAND_OK, 0U, 0U);
            return;
        }

        case ICC_POWER_OFF:
        case GET_SLOT_STATUS:
            if (length != 0U) {
                fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
                return;
            }
            if (command == ICC_POWER_OFF) {
                usb_ccid_reader_power_off();
                usb_ccid_protocol_sync(ctx);
            }
            response(ctx, SLOT_STATUS, 0U, slot, sequence, COMMAND_OK, 0U, ctx->active ? 0U : 1U);
            return;

        case XFR_BLOCK: {
            if (!ctx->active) {
                fail(ctx, command, slot, sequence, ERROR_MUTE);
                return;
            }
            if (length == 0U || message[8] != 0U || message[9] != 0U) {
                fail(ctx, command, slot, sequence, length == 0U ? ERROR_BAD_LENGTH : 8U);
                return;
            }
            size_t response_length = 0U;
            usb_ccid_reader_result_t result = usb_ccid_reader_transceive(
                payload, length, &ctx->tx[USB_CCID_HEADER_SIZE],
                USB_CCID_MAX_PAYLOAD, &response_length
            );
            if (result != USB_CCID_READER_OK) {
                fail(ctx, command, slot, sequence, reader_error(result));
                return;
            }
            usb_ccid_protocol_sync(ctx);
            response(ctx, DATA_BLOCK, response_length, slot, sequence, COMMAND_OK, 0U, 0U);
            return;
        }

        case GET_PARAMETERS:
        case RESET_PARAMETERS:
        case SET_PARAMETERS:
            if (length != (command == SET_PARAMETERS ? sizeof(m_t1_parameters) : 0U)) {
                fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
                return;
            }
            if (command == SET_PARAMETERS) {
                if (message[7] != 1U) {
                    fail(ctx, command, slot, sequence, 7U);
                    return;
                }
                for (size_t i = 0U; i < sizeof(m_t1_parameters); i++) {
                    if (payload[i] != m_t1_parameters[i]) {
                        fail(ctx, command, slot, sequence, (uint8_t)(USB_CCID_HEADER_SIZE + i));
                        /*
                         * Section 6.2.3 requires the unchanged parameter block
                         * even when an individual parameter is unacceptable.
                         */
                        ctx->tx[9] = 1U;
                        memcpy(&ctx->tx[USB_CCID_HEADER_SIZE], m_t1_parameters, sizeof(m_t1_parameters));
                        write_le32(&ctx->tx[1], sizeof(m_t1_parameters));
                        ctx->tx_length = USB_CCID_HEADER_SIZE + sizeof(m_t1_parameters);
                        return;
                    }
                }
            }
            memcpy(&ctx->tx[USB_CCID_HEADER_SIZE], m_t1_parameters, sizeof(m_t1_parameters));
            response(ctx, PARAMETERS, sizeof(m_t1_parameters), slot, sequence, COMMAND_OK, 0U, 1U);
            return;

        case SET_RATE_CLOCK:
            if (length != 8U) {
                fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
                return;
            }
            if (read_le32(payload) != USB_CCID_CLOCK_KHZ ||
                    read_le32(&payload[4]) != USB_CCID_DATA_RATE_BPS) {
                fail(ctx, command, slot, sequence,
                     read_le32(payload) != USB_CCID_CLOCK_KHZ ? 10U : 14U);
                return;
            }
            write_le32(&ctx->tx[USB_CCID_HEADER_SIZE], USB_CCID_CLOCK_KHZ);
            write_le32(&ctx->tx[USB_CCID_HEADER_SIZE + 4U], USB_CCID_DATA_RATE_BPS);
            response(ctx, RATE_CLOCK, 8U, slot, sequence, COMMAND_OK, 0U, 0U);
            return;

        case ABORT:
            if (length != 0U) {
                fail(ctx, command, slot, sequence, ERROR_BAD_LENGTH);
                return;
            }
            /*
             * The storage bridge completes synchronously. An Abort arriving
             * afterwards must not power off a card or cancel a CDC command.
             */
            if (!ctx->abort_pending) {
                /* Bulk may arrive first; defer its reply until control pairs it. */
                ctx->waiting_abort = true;
                ctx->waiting_abort_sequence = sequence;
                return;
            }
            if (ctx->abort_sequence != sequence) {
                fail(ctx, command, slot, sequence, ERROR_ABORTED);
                return;
            }
            ctx->abort_pending = false;
            ctx->waiting_abort = false;
            response(ctx, SLOT_STATUS, 0U, slot, sequence, COMMAND_OK, 0U, ctx->active ? 0U : 1U);
            return;

        default:
            fail(ctx, command, slot, sequence, ERROR_UNSUPPORTED);
            return;
    }
}
