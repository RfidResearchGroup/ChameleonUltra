#include <stdint.h>
#include "rfid_main.h"


// LED卡槽灯GPIO组合
const uint32_t g_led_pins[RGB_LIST_NUM] = { LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8 };
const uint32_t g_led_rgb_pins[RGB_CTRL_NUM] = { LED_R, LED_G, LED_B };
// 设备当前处于的模式
static device_mode_t rfid_state = DEVICE_MODE_NONE;


/**
 * @brief Function for enter tag reader mode
 */
void reader_mode_enter(void) {
    if (rfid_state != DEVICE_MODE_READER) {
        rfid_state = DEVICE_MODE_READER;
        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_cfg_output(HF_ANT_SEL);
        // to end tag emulation
        tag_emulation_sense_end();
        // reader power enable
        nrf_gpio_pin_set(READER_POWER);
        // hf ant switch to reader mode
        nrf_gpio_pin_clear(HF_ANT_SEL);
        // init reader
        lf_125khz_radio_init();
        pcd_14a_reader_init();
        pcd_14a_reader_reset();
    }
}

/**
 * @brief Function for enter tag emulation mode
 */
void tag_mode_enter(void) {
    if (rfid_state != DEVICE_MODE_TAG) {
        rfid_state = DEVICE_MODE_TAG;
        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_cfg_output(HF_ANT_SEL);
        // uninit reader
        lf_125khz_radio_uninit();
        pcd_14a_reader_uninit();
        // lf reader driver
        nrf_gpio_pin_clear(LF_ANT_DRIVER);
        // reader power disable
        nrf_gpio_pin_clear(READER_POWER);
        // hf ant switch to emulation mode
        nrf_gpio_pin_set(HF_ANT_SEL);
        // to run tag emulation
        tag_emulation_sense_run();
    }
}

// 初始化设备的LED灯珠
void init_leds(void) {
    // 初始化卡槽那几颗LED灯的GPIO（其他的LED由其他的模块控制）
    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_cfg_output(g_led_pins[i]);
        nrf_gpio_pin_clear(g_led_pins[i]);
    }
    // 初始化RGB脚
    for (uint8_t i = 0; i < RGB_CTRL_NUM; i++) {
        nrf_gpio_cfg_output(g_led_rgb_pins[i]);
        nrf_gpio_pin_set(g_led_rgb_pins[i]);
    }
    // 设置FIELD LED脚为输出
    nrf_gpio_cfg_output(LED_FIELD);
    // 灭掉场灯
    TAG_FIELD_LED_OFF()
}

/**
 * @brief Function for light up led by slot index
 */
void light_up_by_slot(void) {
    // 目前的亮灯逻辑并没有非常大的变动，因此我们暂时只需要亮起指定的位置的灯即可
    uint8_t slot = tag_emulation_get_slot();
    for (int i = 0; i < ARRAY_SIZE(g_led_pins); i++) {
        if (i == slot) {
            nrf_gpio_pin_set(g_led_pins[i]);
        } else {
            nrf_gpio_pin_clear(g_led_pins[i]);
        }
    }
}

/**
 * @brief Function for enter tag emulation mode
 * @param color: 0 表示r, 1表示g, 2表示b
 */
void set_slot_ligth_color(uint8_t color) {
    nrf_gpio_pin_set(LED_R);
    nrf_gpio_pin_set(LED_G);
    nrf_gpio_pin_set(LED_B);
    uint32_t pin = LED_R;
    switch(color) {
        case 0:
            pin = LED_R;
            break;
        case 1:
            pin = LED_G;
            break;
        case 2:
            pin = LED_B;
            break;
    }
    nrf_gpio_pin_clear(pin);
}

/**
 * @brief Function for get current device mode
 */
device_mode_t get_device_mode(void) {
    return rfid_state;
}
