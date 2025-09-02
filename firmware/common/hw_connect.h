/*
 * hardware io connection map.
 */
#ifndef HW_CONNECT_H_
#define HW_CONNECT_H_

#include "device_info.h"
#include "nrf_lpcomp.h"
#include "nrf_saadc.h"

typedef enum {
    CHAMELEON_ULTRA,
    CHAMELEON_LITE,
} chameleon_device_type_t;

typedef enum { RGB_RED, RGB_GREEN, RGB_BLUE, RGB_MAGENTA, RGB_YELLOW, RGB_CYAN, RGB_WHITE } chameleon_rgb_type_t;

#define MAX_LED_NUM 8
#define MAX_RGB_NUM 3

extern uint32_t g_led_field;
extern uint32_t g_led_1;
extern uint32_t g_led_2;
extern uint32_t g_led_3;
extern uint32_t g_led_4;
extern uint32_t g_led_5;
extern uint32_t g_led_6;
extern uint32_t g_led_7;
extern uint32_t g_led_8;
extern uint32_t g_led_r;
extern uint32_t g_led_g;
extern uint32_t g_led_b;
extern uint32_t g_led_num;
extern uint32_t g_rgb_num;
extern uint32_t g_button1;
extern uint32_t g_button2;
extern uint32_t g_lf_mod;
extern uint32_t g_lf_rssi_pin;
extern nrf_lpcomp_input_t g_lf_rssi;
extern uint32_t g_bat_sense_pin;
extern nrf_saadc_input_t g_bat_sense;

#define LED_FIELD g_led_field
#define LED_1 g_led_1
#define LED_2 g_led_2
#define LED_3 g_led_3
#define LED_4 g_led_4
#define LED_5 g_led_5
#define LED_6 g_led_6
#define LED_7 g_led_7
#define LED_8 g_led_8
#define LED_R g_led_r
#define LED_G g_led_g
#define LED_B g_led_b
#define RGB_LIST_NUM g_led_num
#define RGB_CTRL_NUM g_rgb_num
#define BUTTON_1 g_button1
#define BUTTON_2 g_button2
#define BUTTON_PULL NRF_GPIO_PIN_PULLDOWN
#define LF_MOD g_lf_mod
#define LF_RSSI_PIN g_lf_rssi_pin
#define LF_RSSI g_lf_rssi
#define BAT_SENSE_PIN g_bat_sense_pin
#define BAT_SENSE g_bat_sense

#if defined(PROJECT_CHAMELEON_ULTRA)
extern uint32_t g_lf_ant_driver;
extern uint32_t g_lf_oa_out;
extern uint32_t g_hf_spi_select;
extern uint32_t g_hf_spi_miso;
extern uint32_t g_hf_spi_mosi;
extern uint32_t g_hf_spi_sck;
extern uint32_t g_hf_ant_sel;
extern uint32_t g_reader_power;

#define LF_ANT_DRIVER g_lf_ant_driver
#define LF_OA_OUT g_lf_oa_out
#define HF_SPI_SELECT g_hf_spi_select
#define HF_SPI_MISO g_hf_spi_miso
#define HF_SPI_MOSI g_hf_spi_mosi
#define HF_SPI_SCK g_hf_spi_sck
#define HF_ANT_SEL g_hf_ant_sel
#define READER_POWER g_reader_power
#endif

// Operational Definitions for General Field Lights
#define TAG_FIELD_LED_ON() nrf_gpio_pin_clear(LED_FIELD);
#define TAG_FIELD_LED_OFF() nrf_gpio_pin_set(LED_FIELD);

void hw_connect_init(void);
void init_leds(void);
uint32_t *hw_get_led_array(void);
uint32_t *hw_get_led_reversal_array(void);
uint32_t *hw_get_rgb_array(void);
chameleon_device_type_t hw_get_device_type(void);
uint8_t hw_get_version_code(void);
void set_slot_light_color(chameleon_rgb_type_t color);

#endif
