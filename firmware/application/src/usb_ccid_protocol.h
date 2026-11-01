#ifndef USB_CCID_PROTOCOL_H
#define USB_CCID_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usb_ccid_reader.h"

#define USB_CCID_HEADER_SIZE 10U
#define USB_CCID_MAX_PAYLOAD USB_CCID_READER_MAX_APDU
#define USB_CCID_MAX_MESSAGE (USB_CCID_HEADER_SIZE + USB_CCID_MAX_PAYLOAD)
#define USB_CCID_PACKET_SIZE 64U
#define USB_CCID_CLOCK_KHZ 4000UL
#define USB_CCID_DATA_RATE_BPS 9600UL

/*
 * Keep the CCID message layer independent of the Nordic USB driver. This lets
 * the same parser and status transitions run in host tests with a mock reader.
 */
typedef struct {
    bool present;
    bool active;
    bool slot_changed;
    bool abort_pending;
    bool waiting_abort;
    uint8_t abort_sequence;
    uint8_t waiting_abort_sequence;
    uint8_t tx[USB_CCID_MAX_MESSAGE];
    uint16_t tx_length;
} usb_ccid_protocol_t;

typedef struct {
    uint8_t bytes[USB_CCID_MAX_MESSAGE];
    size_t length;
} usb_ccid_rx_t;

/* Feed one bulk packet; true means a complete message (possibly malformed). */
bool usb_ccid_rx_feed(usb_ccid_rx_t *rx, const uint8_t *packet, size_t length);

void usb_ccid_protocol_sync(usb_ccid_protocol_t *ctx);
void usb_ccid_protocol_process(
    usb_ccid_protocol_t *ctx,
    const uint8_t *message,
    size_t message_length
);
void usb_ccid_protocol_abort(usb_ccid_protocol_t *ctx, uint8_t sequence);

#endif
