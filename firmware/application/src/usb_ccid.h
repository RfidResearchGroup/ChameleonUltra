#ifndef USB_CCID_H
#define USB_CCID_H

#include "app_usbd.h"

#if defined(PROJECT_CHAMELEON_ULTRA)
app_usbd_class_inst_t const *usb_ccid_class_inst_get(void);
void usb_ccid_process(void);
#endif

#endif
