#ifndef RGB_MARQUEE_H
#define RGB_MARQUEE_H

#include <stdint.h>
#include "nrf_drv_pwm.h"


// Slot LED colors as 0xRRGGBB
#define RGB_HEX_RED     0xFF0000
#define RGB_HEX_GREEN   0x00FF00
#define RGB_HEX_BLUE    0x0000FF
#define RGB_HEX_MAGENTA 0xFF00FF
#define RGB_HEX_YELLOW  0xFFFF00
#define RGB_HEX_CYAN    0x00FFFF
#define RGB_HEX_WHITE   0xFFFFFF


void rgb_marquee_init(void);
void rgb_marquee_stop(void);
void rgb_marquee_reset(void);
bool rgb_marquee_is_enabled(void);
void rgb_marquee_usb_open_sweep(uint32_t color, uint8_t dir);
void rgb_marquee_usb_open_symmetric(uint32_t color);
void rgb_marquee_sweep_to(uint32_t color, uint8_t dir, uint8_t end);
void rgb_marquee_slot_switch(uint8_t led_down, uint32_t color_led_down, uint8_t led_up, uint32_t color_led_up);
void rgb_marquee_sweep_fade(uint32_t color, uint8_t dir, uint8_t end, uint8_t start_light, uint8_t stop_light);
void rgb_marquee_sweep_from_to(uint32_t color, uint8_t start, uint8_t stop);
void rgb_marquee_usb_idle(void);
void rgb_marquee_symmetric_out(uint32_t color, uint8_t slot);
void rgb_marquee_symmetric_in(uint32_t color, uint8_t slot);

// Set the common RGB lines of the slot LEDs to any 0xRRGGBB color
void set_slot_light_rgb(uint32_t color);
// Show the color of the active slot; unlike set_slot_light_rgb, it may be refreshed later (HF/LF alternation)
void rgb_slot_indicator_show(uint32_t color);
bool rgb_slot_indicator_is_shown(void);

#endif
