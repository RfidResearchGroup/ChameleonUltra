#ifndef RFID_MAIN_H
#define RFID_MAIN_H

/* Forced before the production source so its device header stays host-only. */
typedef enum {
    DEVICE_MODE_NONE,
    DEVICE_MODE_READER,
    DEVICE_MODE_TAG,
} device_mode_t;

device_mode_t get_device_mode(void);

#endif
