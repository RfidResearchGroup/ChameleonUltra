/*
 * standalone_led.h
 *
 * Visual-only feedback vocabulary for standalone modes.
 *
 * CU has eight RGB slot LEDs (no buzzer, no per-LED color), so the
 * feedback language is built from:
 *   - one of seven enum colors set globally via set_slot_light_color()
 *   - which subset of the 8 LEDs is currently lit (nrf_gpio_pin_set/clear)
 *
 * Effects are short and synchronous (sub-300ms total) using the same
 * bsp_delay_ms() pattern as show_battery() / offline_status_blink_color()
 * in app_main.c.
 */

#ifndef STANDALONE_LED_H
#define STANDALONE_LED_H

#include <stdint.h>
#include <stdbool.h>

#include "app_standalone.h"
#include "hw_connect.h"   /* chameleon_rgb_type_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SL_FB_ARMED,            /* mode armed - mode-colour sweep then solid    */
    SL_FB_DISARMED,         /* mode exited - reverse sweep then all off     */
    SL_FB_DENIED,           /* mode needs opt-in - red double-flash         */

    SL_FB_SUCCESS,          /* successful op - green triple-flash           */
    SL_FB_ERROR,            /* failed op    - red triple-flash              */

    SL_FB_BUSY_START,       /* long op begins - brief mode-colour wave      */
    SL_FB_BUSY_END,         /* long op ends   - brief mode-colour wave-back */
} standalone_feedback_t;

void standalone_led_init(void);
void standalone_feedback(standalone_feedback_t fb);
void standalone_feedback_for_mode(standalone_feedback_t fb,
                                  standalone_mode_t      mode);
void standalone_led_clear(void);
void standalone_led_solid(void); /* solid LEDs in current mode colour */
void standalone_led_set_mode_color(standalone_mode_t       mode,
                                   chameleon_rgb_type_t    color);

#ifdef __cplusplus
}
#endif

#endif /* STANDALONE_LED_H */
