#ifndef RGB_MARQUEE_H
#define RGB_MARQUEE_H

#include <stdint.h>
#include "nrf_drv_pwm.h"


void rgb_marquee_init(void);
void rgb_marquee_stop(void);
void rgb_marquee_reset(void);
bool rgb_marquee_is_enabled(void);
void rgb_marquee_usb_open_sweep(uint8_t color, uint8_t dir);
void rgb_marquee_usb_open_symmetric(uint8_t color);
void rgb_marquee_sweep_to(uint8_t color, uint8_t dir, uint8_t end);
void rgb_marquee_slot_switch(uint8_t led_down, uint8_t color_led_down, uint8_t led_up, uint8_t color_led_up);
void rgb_marquee_sweep_fade(uint8_t color, uint8_t dir, uint8_t end, uint8_t start_light, uint8_t stop_light);
void rgb_marquee_sweep_from_to(uint8_t color, uint8_t start, uint8_t stop);
void rgb_marquee_usb_idle(void);
void rgb_marquee_symmetric_out(uint8_t color, uint8_t slot);
void rgb_marquee_symmetric_in(uint8_t color, uint8_t slot);

#endif
