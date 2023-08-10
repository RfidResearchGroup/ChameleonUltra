#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define SETTINGS_ANIMATION_FULL 0
#define SETTINGS_ANIMATION_MINIMAL 1
#define SETTINGS_ANIMATION_NONE 2

/*
 * bits [0-1]: animation config
 * bits [2-31]: reserved
 */
typedef uint32_t settings_data_t;

void settings_load_config(void);
uint8_t settings_save_config(void);
uint8_t settings_get_animation_config();
void settings_set_animation_config(uint8_t value);

#endif