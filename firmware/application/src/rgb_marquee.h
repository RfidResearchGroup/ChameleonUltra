#ifndef RGB_MARQUEE_H
#define RGB_MARQUEE_H

#include <stdint.h>
#include "nrf_drv_pwm.h"


void rgb_marquee_init(void);
void rgb_marquee_stop(void);
void rgb_marquee_reset(void);
bool is_rgb_marquee_enable(void);
void ledblink1(uint8_t color, uint8_t dir);
void ledblink2(uint8_t color, uint8_t dir, uint8_t end);
void ledblink3(uint8_t led_down, uint8_t color_led_down, uint8_t led_up, uint8_t color_led_up);
void ledblink4(uint8_t color, uint8_t dir, uint8_t end, uint8_t start_light, uint8_t stop_light);
void ledblink5(uint8_t color, uint8_t start, uint8_t stop);
void ledblink6(void);

#endif
