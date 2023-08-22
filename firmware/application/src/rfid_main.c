#include <stdint.h>
#include "rfid_main.h"



// 设备当前处于的模式
device_mode_t rfid_state = DEVICE_MODE_NONE;


/**
 * @brief Function for enter tag reader mode
 */
void reader_mode_enter(void) {
// only chameleon ultra has reader mode support.
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (rfid_state != DEVICE_MODE_READER) {
        rfid_state = DEVICE_MODE_READER;

        tag_emulation_sense_end();          // to end tag emulation

        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_pin_set(READER_POWER);     // reader power enable
        nrf_gpio_cfg_output(HF_ANT_SEL);
        nrf_gpio_pin_clear(HF_ANT_SEL);     // hf ant switch to reader mode

        // init reader
        lf_125khz_radio_init();
        pcd_14a_reader_init();
        pcd_14a_reader_reset();
    }
#endif
}

/**
 * @brief Function for enter tag emulation mode
 */
void tag_mode_enter(void) {
    if (rfid_state != DEVICE_MODE_TAG) {
        rfid_state = DEVICE_MODE_TAG;

#if defined(PROJECT_CHAMELEON_ULTRA)
        // uninit reader
        lf_125khz_radio_uninit();
        pcd_14a_reader_uninit();

        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_pin_clear(LF_ANT_DRIVER);  // lf reader driver

        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_pin_clear(READER_POWER);   // reader power disable

        nrf_gpio_cfg_output(HF_ANT_SEL);
        nrf_gpio_pin_set(HF_ANT_SEL);       // hf ant switch to emulation mode
        // give time for fields to shutdown, else we get spurious LF detection triggered in LF emul
        // need at least about 30ms on dev kit
        bsp_delay_ms(60);
#endif

        // to run tag emulation
        tag_emulation_sense_run();
    }
}

/**
 * @brief Function for light up led by slot index
 */
void light_up_by_slot(void) {
    uint32_t *led_pins = hw_get_led_array();
    // 目前的亮灯逻辑并没有非常大的变动，因此我们暂时只需要亮起指定的位置的灯即可
    uint8_t slot = tag_emulation_get_slot();
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        if (i == slot) {
            nrf_gpio_pin_set(led_pins[i]);
        } else {
            nrf_gpio_pin_clear(led_pins[i]);
        }
    }
}

/**
 * @brief Function for get current device mode
 */
device_mode_t get_device_mode(void) {
    return rfid_state;
}

/**
 * @brief Get the color by slot
 *
 * @param slot slot number, 0 - 7
 * @return uint8_t Color 0R, 1G, 2B
 */
uint8_t get_color_by_slot(uint8_t slot) {
    tag_specific_type_t tag_type[2];
    tag_emulation_get_specific_type_by_slot(slot, tag_type);
    if (tag_type[0] != TAG_TYPE_UNKNOWN && tag_type[1] != TAG_TYPE_UNKNOWN) {
        return 0;   // 双频卡模拟，返回R，表示双频卡
    } else if (tag_type[0] != TAG_TYPE_UNKNOWN) {   // 高频模拟，返回G
        return 1;
    } else {    // 低频模拟，返回B
        return 2;
    }
}
