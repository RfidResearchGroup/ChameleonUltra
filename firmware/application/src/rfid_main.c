#include "rfid_main.h"

#include <stdint.h>

#include "rgb_marquee.h"

// The current mode of the device
device_mode_t rfid_state = DEVICE_MODE_NONE;

/**
 * @brief Function for enter tag reader mode
 */
void reader_mode_enter(void)
{
// only chameleon ultra has reader mode support.
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (rfid_state != DEVICE_MODE_READER) {
        rfid_state = DEVICE_MODE_READER;

        tag_emulation_sense_end();  // to end tag emulation

        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_pin_set(READER_POWER);  // reader power enable
        nrf_gpio_cfg_output(HF_ANT_SEL);
        nrf_gpio_pin_clear(HF_ANT_SEL);  // hf ant switch to reader mode

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
void tag_mode_enter(void)
{
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
        nrf_gpio_pin_clear(READER_POWER);  // reader power disable
        TAG_FIELD_LED_OFF();

        nrf_gpio_cfg_output(HF_ANT_SEL);
        nrf_gpio_pin_set(HF_ANT_SEL);  // hf ant switch to emulation mode
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
void light_up_by_slot(void)
{
    uint32_t *led_pins = hw_get_led_array();
    // The current lighting logic has not changed very much, so we only need to light up the specified lamp for the time
    // being.
    uint8_t slot = tag_emulation_get_slot();
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        if (i == slot) {
            nrf_gpio_pin_set(led_pins[i]);
        }
        else {
            nrf_gpio_pin_clear(led_pins[i]);
        }
    }
}

/**
 * @brief Apply visual and state changes after switching slot
 */
void apply_slot_change(uint8_t slot_now, uint8_t slot_new)
{
    uint8_t color_now = get_color_by_slot(slot_now);
    uint8_t color_new = get_color_by_slot(slot_new);
    ledblink3(slot_now, color_now, slot_new, color_new);
}

/**
 * @brief Function for get current device mode
 */
device_mode_t get_device_mode(void) { return rfid_state; }

/**
 * @brief Get the color by slot
 *
 * @param slot slot number, 0 - 7
 * @return uint8_t Color 0R, 1G, 2B
 */
uint8_t get_color_by_slot(uint8_t slot)
{
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(slot, &tag_types);
    bool enabled_lf = is_slot_enabled(slot, TAG_SENSE_LF);
    bool enabled_hf = is_slot_enabled(slot, TAG_SENSE_HF);
    if (tag_types.tag_hf != TAG_TYPE_UNDEFINED && tag_types.tag_lf != TAG_TYPE_UNDEFINED && enabled_hf && enabled_lf) {
        return 0;  // Dual -frequency card emulation, return R, indicate a dual -frequency card
    }
    else if (tag_types.tag_hf != TAG_TYPE_UNDEFINED && enabled_hf) {  // High -frequency emulation, return G
        return 1;
    }
    else {  // Low -frequency emulation, return B
        return 2;
    }
}
