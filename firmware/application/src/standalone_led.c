/*
 * standalone_led.c
 *
 * Concrete LED feedback patterns for standalone modes. Built on the
 * existing primitives - mirrors the patterns used by show_battery() and
 * offline_status_blink_color() in app_main.c.
 *
 * All functions block. Total feedback duration is bounded (<300ms typical,
 * <500ms worst case for ARMED sweep). Modes call these from button handlers;
 * never from the tick path.
 */

#include "standalone_led.h"

#include "nrf_gpio.h"
#include "hw_connect.h"
#include "bsp_delay.h"

/* Per-mode palette. CU's enum gives 7 colours; we reserve RED/GREEN for
 * error/success feedback and use the rest for mode-armed indication. */
static chameleon_rgb_type_t m_palette[STANDALONE_MODE__COUNT] = {
    [STANDALONE_MODE_DISABLED]    = RGB_WHITE,    /* never actually shown */
    [STANDALONE_MODE_AUTOCLONE]   = RGB_BLUE,
    [STANDALONE_MODE_READ_REPLAY] = RGB_CYAN,
    [STANDALONE_MODE_AUTHTRACE]   = RGB_MAGENTA,
    [STANDALONE_MODE_SLOT_CYCLE]  = RGB_YELLOW,
    [STANDALONE_MODE_DICT_CHECK]  = RGB_WHITE,
};

static bool m_initialised = false;

/* -------------------------------------------------------------------------
 * Low-level helpers
 * ------------------------------------------------------------------------- */

static void leds_all_off(void) {
    uint32_t *p = hw_get_led_array();
    for (uint32_t i = 0; i < RGB_LIST_NUM; i++) nrf_gpio_pin_clear(p[i]);
}

static void leds_all_on(void) {
    uint32_t *p = hw_get_led_array();
    for (uint32_t i = 0; i < RGB_LIST_NUM; i++) nrf_gpio_pin_set(p[i]);
}

static void leds_sweep_on(chameleon_rgb_type_t color, uint16_t per_step_ms) {
    uint32_t *p = hw_get_led_array();
    leds_all_off();
    set_slot_light_color(color);
    for (uint32_t i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_set(p[i]);
        bsp_delay_ms(per_step_ms);
    }
}

static void leds_sweep_off(uint16_t per_step_ms) {
    uint32_t *p = hw_get_led_array();
    for (int32_t i = (int32_t)RGB_LIST_NUM - 1; i >= 0; i--) {
        nrf_gpio_pin_clear(p[i]);
        bsp_delay_ms(per_step_ms);
    }
}

static void leds_flash_n(chameleon_rgb_type_t color, uint8_t n,
                         uint16_t on_ms, uint16_t off_ms) {
    leds_all_off();
    set_slot_light_color(color);
    for (uint8_t i = 0; i < n; i++) {
        leds_all_on();
        bsp_delay_ms(on_ms);
        leds_all_off();
        if (i < n - 1) bsp_delay_ms(off_ms);
    }
}

static void leds_wave(chameleon_rgb_type_t color, bool reverse) {
    uint32_t *p = hw_get_led_array();
    set_slot_light_color(color);
    leds_all_on();
    bsp_delay_ms(40);
    if (reverse) {
        for (int32_t i = (int32_t)RGB_LIST_NUM - 1; i >= 0; i--) {
            nrf_gpio_pin_clear(p[i]);
            bsp_delay_ms(15);
        }
    } else {
        for (uint32_t i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_clear(p[i]);
            bsp_delay_ms(15);
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void standalone_led_init(void) {
    m_initialised = true;
}

void standalone_led_clear(void) {
    leds_all_off();
}

void standalone_led_set_mode_color(standalone_mode_t      mode,
                                   chameleon_rgb_type_t   color) {
    if (mode < STANDALONE_MODE__COUNT) {
        m_palette[mode] = color;
    }
}

void standalone_feedback(standalone_feedback_t fb) {
    standalone_feedback_for_mode(fb, app_standalone_get_mode());
}

void standalone_feedback_for_mode(standalone_feedback_t fb,
                                  standalone_mode_t      mode) {
    if (!m_initialised) return;

    chameleon_rgb_type_t mode_col =
        (mode < STANDALONE_MODE__COUNT) ? m_palette[mode] : RGB_WHITE;

    switch (fb) {
        case SL_FB_ARMED:
            leds_sweep_on(mode_col, 25);
            break;

        case SL_FB_DISARMED:
            leds_sweep_off(20);
            break;

        case SL_FB_DENIED:
            leds_flash_n(RGB_RED, 2, 100, 100);
            break;

        case SL_FB_SUCCESS:
            leds_flash_n(RGB_GREEN, 3, 70, 70);
            leds_all_off();
            set_slot_light_color(mode_col);
            leds_all_on();
            break;

        case SL_FB_ERROR:
            leds_flash_n(RGB_RED, 3, 70, 70);
            leds_all_off();
            set_slot_light_color(mode_col);
            leds_all_on();
            break;

        case SL_FB_BUSY_START:
            leds_wave(mode_col, false);
            set_slot_light_color(mode_col);
            leds_all_on();
            break;

        case SL_FB_BUSY_END:
            leds_wave(mode_col, true);
            set_slot_light_color(mode_col);
            leds_all_on();
            break;
    }
}
