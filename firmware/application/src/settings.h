#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#include "utils.h"

#define SETTINGS_CURRENT_VERSION 2

typedef enum {
    SettingsAnimationModeFull = 0U,
    SettingsAnimationModeMinimal = 1U,
    SettingsAnimationModeNone = 2U,
} settings_animation_mode_t;

typedef enum {
    // Set this button to have no function
    //    (But always can wakeup device, why didn't to disable this function? i dont known, you can ask chatgpt.)
    SettingsButtonDisable = 0U,
    // Card slot number sequence will increase after pressing
    SettingsButtonCycleSlot = 1U,
    // Card slot number sequence decreases after pressing
    SettingsButtonCycleSlotDec = 2U,
    // Read the UID card number immediately after pressing, continue searching, and simulate immediately after reading the card
    SettingsButtonCloneIcUid = 3U,
} settings_button_function_t;

typedef struct ALIGN_U32 {
    uint16_t version;
    
    // 1 byte
    uint8_t animation_config : 2;
    uint8_t reserved0 : 6;

    // 1 byte
    uint8_t button_a_press : 4;
    uint8_t button_b_press : 4;
    
    // 8 byte
    uint32_t reserved1;
    uint32_t reserved2;
} settings_data_t;

void settings_init_config(void);
void settings_migrate(void);
void settings_load_config(void);
uint8_t settings_save_config(void);
uint8_t settings_get_animation_config(void);
void settings_set_animation_config(uint8_t value);
uint8_t settings_get_button_press_config(char which);
void settings_set_button_press_config(char which, uint8_t value);
bool is_settings_button_type_valid(char type);

#endif
