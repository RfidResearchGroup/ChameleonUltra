#ifndef CCID_TRANSPORT_TEST_APP_USBD_CLASS_BASE_H
#define CCID_TRANSPORT_TEST_APP_USBD_CLASS_BASE_H

#include "app_usbd.h"

#define APP_USBD_CLASS_DESCRIPTOR_BEGIN(ctx, buffer, capacity) \
    size_t written = 0U; \
    (void)ctx;
#define APP_USBD_CLASS_DESCRIPTOR_WRITE(value) \
    do { \
        if (written >= capacity) return false; \
        buffer[written++] = (value); \
    } while (0)
#define APP_USBD_CLASS_DESCRIPTOR_END() return true;

#endif
