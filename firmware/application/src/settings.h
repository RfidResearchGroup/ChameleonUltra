#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#include "utils.h"

#define SETTINGS_ANIMATION_FULL 0
#define SETTINGS_ANIMATION_MINIMAL 1
#define SETTINGS_ANIMATION_NONE 2


typedef struct ALIGN_U32 {
    uint16_t version;
    uint16_t animation_config : 2;
} settings_data_t;

void settings_load_config(void);
uint8_t settings_save_config(void);
uint8_t settings_get_animation_config();
void settings_set_animation_config(uint8_t value);

#endif