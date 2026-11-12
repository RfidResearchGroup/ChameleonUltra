#ifndef USB_CCID_READER_H
#define USB_CCID_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_CCID_READER_MAX_APDU 261U

typedef enum {
    USB_CCID_READER_OK = 0,
    USB_CCID_READER_NO_CARD,
    USB_CCID_READER_BUSY,
    USB_CCID_READER_UNSUPPORTED,
    USB_CCID_READER_BAD_APDU,
    USB_CCID_READER_OVERFLOW,
    USB_CCID_READER_IO_ERROR,
} usb_ccid_reader_result_t;

/* Reset only the CCID-owned RF field; leave a CDC keep-field command alone. */
void usb_ccid_reader_ccid_reset(void);
usb_ccid_reader_result_t usb_ccid_reader_poll(bool *present);
usb_ccid_reader_result_t usb_ccid_reader_power_on(uint8_t *atr, size_t atr_capacity, size_t *atr_length);
void usb_ccid_reader_power_off(void);
usb_ccid_reader_result_t usb_ccid_reader_transceive(
    const uint8_t *apdu,
    size_t apdu_length,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length
);
void usb_ccid_reader_get_state(bool *present, bool *active);
bool usb_ccid_reader_mode_is_ready(void);
/* Called before a serial reader command or before leaving reader mode. */
void usb_ccid_reader_external_invalidate(void);

#endif
