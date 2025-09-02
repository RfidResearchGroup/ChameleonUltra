#ifndef USB_MAIN_H
#define USB_MAIN_H

#include <stdbool.h>
#include <stdint.h>

void usb_cdc_init(void);
void usb_cdc_write(const void *p_buf, uint16_t length);
bool is_usb_working(void);

#endif
