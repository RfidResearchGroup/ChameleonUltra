#ifndef RFID_MAIN_H
#define RFID_MAIN_H

#include "nrf_gpio.h"

#include "rc522.h"
#include "nfc_14a.h"
#include "lf_tag_em.h"
#include "bsp_time.h"
#include "bsp_delay.h"
#include "mf1_toolbox.h"
#include "tag_emulation.h"
#include "lf_em410x_data.h"
#include "lf_125khz_radio.h"
#include "lf_reader_main.h"


// ****************** LED DEFINE ******************

#define LED_FIELD      (NRF_GPIO_PIN_MAP(1, 1))
#define LED_R          (NRF_GPIO_PIN_MAP(0, 24))
#define LED_G          (NRF_GPIO_PIN_MAP(0, 22))
#define LED_B          (NRF_GPIO_PIN_MAP(1, 0))
#define LED_8          (NRF_GPIO_PIN_MAP(0, 20))
#define LED_7          (NRF_GPIO_PIN_MAP(0, 17))
#define LED_6          (NRF_GPIO_PIN_MAP(0, 15))
#define LED_5          (NRF_GPIO_PIN_MAP(0, 13))
#define LED_4          (NRF_GPIO_PIN_MAP(0, 12))
#define LED_3          (NRF_GPIO_PIN_MAP(1, 9))
#define LED_2          (NRF_GPIO_PIN_MAP(0, 8))
#define LED_1          (NRF_GPIO_PIN_MAP(0, 6))
#define RGB_LIST_NUM 8
#define RGB_CTRL_NUM 3

// ****************** LF DEFINE ******************

// reader
#define LF_ANT_DRIVER  (NRF_GPIO_PIN_MAP(0, 31))    // LF ANT DRIVER
#define LF_OA_OUT      (NRF_GPIO_PIN_MAP(1, 15))    // LF DATA IN
// emulation
#define LF_MOD         (NRF_GPIO_PIN_MAP(1, 13))
#define LF_RSSI_PIN    (NRF_GPIO_PIN_MAP(0, 2))
#define LF_RSSI        NRF_LPCOMP_INPUT_0

// ****************** HF DEFINE ******************

// reader
#define HF_SPI_SELECT  (NRF_GPIO_PIN_MAP(1, 6))
#define HF_SPI_MISO    (NRF_GPIO_PIN_MAP(0, 11))
#define HF_SPI_MOSI    (NRF_GPIO_PIN_MAP(1, 7))
#define HF_SPI_SCK     (NRF_GPIO_PIN_MAP(1, 4))

#define HF_ANT_SEL     (NRF_GPIO_PIN_MAP(1, 10))


// ****************** BTN DEFINE ******************

#define BUTTON_1       (NRF_GPIO_PIN_MAP(0, 26))
#define BUTTON_2       (NRF_GPIO_PIN_MAP(1, 2))

// ****************** OTHER DEFINE ******************

#define BAT_SENSE      (NRF_GPIO_PIN_MAP(0, 4))
#define READER_POWER   (NRF_GPIO_PIN_MAP(0, 29))    // POWER

// 通用场灯的操作定义
#define TAG_FIELD_LED_ON()     nrf_gpio_pin_clear(LED_FIELD);
#define TAG_FIELD_LED_OFF()    nrf_gpio_pin_set(LED_FIELD);


typedef enum {
    DEVICE_MODE_NONE,
    DEVICE_MODE_READER,
    DEVICE_MODE_TAG,
} device_mode_t;

// functions define
void init_leds(void);
void light_up_by_slot(void);
void reader_mode_enter(void);
void tag_mode_enter(void);
void set_slot_ligth_color(uint8_t color);
device_mode_t get_device_mode(void);

#endif
