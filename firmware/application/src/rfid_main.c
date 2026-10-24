#include <stdint.h>
#include "app_timer.h"
#include "nrfx_power.h"
#include "rfid_main.h"
#include "rgb_marquee.h"

//The current mode of the device
device_mode_t rfid_state = DEVICE_MODE_NONE;

// External declaration for field state
extern bool m_is_field_on;

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
        // Safety check: turn off field if it's on
        if (m_is_field_on) {
            pcd_14a_reader_antenna_off();
            m_is_field_on = false;
        }

        // uninit reader
        lf_125khz_radio_uninit();
        pcd_14a_reader_uninit();

        // pin init
        nrf_gpio_cfg_output(LF_ANT_DRIVER);
        nrf_gpio_pin_clear(LF_ANT_DRIVER);  // lf reader driver

        nrf_gpio_cfg_output(READER_POWER);
        nrf_gpio_pin_clear(READER_POWER);   // reader power disable
        TAG_FIELD_LED_OFF();

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

// A slot with both an HF and an LF card switches between their colors this often
#define SLOT_COLOR_ALTERNATE_MS 1000

// Whether a dual-card slot currently shows the color of its LF card
static bool m_slot_color_phase_lf = false;

/**
 * @brief Function for light up led by slot index
 */
void light_up_by_slot(void) {
    uint32_t *led_pins = hw_get_led_array();
    // The current lighting logic has not changed very much, so we only need to light up the specified lamp for the time being.
    uint8_t slot = tag_emulation_get_slot();
    rgb_slot_indicator_show(get_color_by_slot(slot));
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        if (i == slot) {
            nrf_gpio_pin_set(led_pins[i]);
        } else {
            nrf_gpio_pin_clear(led_pins[i]);
        }
    }
}

/**
 * @brief Keep the slot LED up to date: alternate the colors of a dual-card slot,
 * and bring the slot color back once a reader field has painted it green / blue
 */
void slot_light_process(void) {
    static uint32_t last_switch = 0;
    static bool was_emulating = false;

    uint32_t now = app_timer_cnt_get();
    bool alternate = app_timer_cnt_diff_compute(now, last_switch) >= APP_TIMER_TICKS(SLOT_COLOR_ALTERNATE_MS);
    if (alternate) {
        last_switch = now;
        m_slot_color_phase_lf = !m_slot_color_phase_lf;
    }

    bool emulating = g_is_tag_emulating;
    bool emulation_ended = was_emulating && !emulating;
    was_emulating = emulating;
    // The USB animation owns the LEDs only while USB is plugged in; the flag alone stays set on battery
    bool usb_animation = rgb_marquee_is_enabled() && nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED;
    if (emulating || rfid_state != DEVICE_MODE_TAG || m_is_field_on || usb_animation) {
        return;
    }

    if (emulation_ended) {
        light_up_by_slot();
    } else if (alternate && rgb_slot_indicator_is_shown()) {
        rgb_slot_indicator_show(get_color_by_slot(tag_emulation_get_slot()));
    }
}

/**
 * @brief Apply visual and state changes after switching slot
 */
void apply_slot_change(uint8_t slot_now, uint8_t slot_new) {
    uint32_t color_now = get_color_by_slot(slot_now);
    uint32_t color_new = get_color_by_slot(slot_new);
    rgb_marquee_slot_switch(slot_now, color_now, slot_new, color_new);
    // Hand the slot LED from the fade PWM over to the indicator, so a dual-card slot alternates
    rgb_marquee_stop();
    light_up_by_slot();
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
 * Cards without a color of their own keep the legacy colors: R for a dual-card slot, G for HF, B for LF.
 * A dual-card slot with colors shows the HF and LF colors in turn.
 *
 * @param slot slot number, 0 - 7
 * @return uint32_t Color 0xRRGGBB
 */
uint32_t get_color_by_slot(uint8_t slot) {
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(slot, &tag_types);
    bool has_hf = tag_types.tag_hf != TAG_TYPE_UNDEFINED && is_slot_enabled(slot, TAG_SENSE_HF);
    bool has_lf = tag_types.tag_lf != TAG_TYPE_UNDEFINED && is_slot_enabled(slot, TAG_SENSE_LF);
    uint32_t color_hf = tag_emulation_slot_get_color(slot, TAG_SENSE_HF);
    uint32_t color_lf = tag_emulation_slot_get_color(slot, TAG_SENSE_LF);
    if (has_hf && has_lf) {
        if (color_hf == 0 && color_lf == 0) {
            return RGB_HEX_RED;     // Dual-frequency card emulation
        }
        if (m_slot_color_phase_lf) {
            return color_lf != 0 ? color_lf : RGB_HEX_BLUE;
        }
        return color_hf != 0 ? color_hf : RGB_HEX_GREEN;
    } else if (has_hf) {
        return color_hf != 0 ? color_hf : RGB_HEX_GREEN;
    } else {
        return color_lf != 0 ? color_lf : RGB_HEX_BLUE;
    }
}
