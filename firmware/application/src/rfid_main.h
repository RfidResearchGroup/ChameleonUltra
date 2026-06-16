#ifndef RFID_MAIN_H
#define RFID_MAIN_H

#include "bsp_delay.h"
#include "bsp_time.h"
#include "hw_connect.h"
#include "lf_tag_em.h"
#include "nfc_14a.h"
#include "nfc_mf0_ntag.h"
#include "nfc_mf1.h"
#include "nrf_gpio.h"
#include "tag_emulation.h"

#if defined(PROJECT_CHAMELEON_ULTRA)
#include "lf_reader_main.h"
#include "mf1_toolbox.h"
#include "rc522.h"
#endif

typedef enum {
    DEVICE_MODE_NONE,
    DEVICE_MODE_READER,
    DEVICE_MODE_TAG,
} device_mode_t;

// functions define
void init_leds(void);
void light_up_by_slot(void);
void apply_slot_change(uint8_t slot_now, uint8_t slot_new);
void reader_mode_enter(void);
void tag_mode_enter(void);
device_mode_t get_device_mode(void);
uint8_t get_color_by_slot(uint8_t slot);

#endif
