#ifndef SYS_SLEEP_H__
#define SYS_SLEEP_H__

#include <stdint.h>

//           Wake up equipment
#define SLEEP_DELAY_MS_BUTTON_WAKEUP            8000    // The sleep delay of the button awakened
#define SLEEP_DELAY_MS_FIELD_WAKEUP             4000    // The sleep delay (including high and low frequencies) of the field wake -up (including high and low frequency)
#define SLEEP_DELAY_MS_FIRST_POWER              1000    // The sleep delay of the first power supply (access to the battery)

//           The operating state is delayed
#define SLEEP_DELAY_MS_BUTTON_CLICK             4000    // The sleep delay of the button click
#define SLEEP_DELAY_MS_FIELD_NFC_LOST           3000    // High -frequency analog card after leaving the field
#define SLEEP_DELAY_MS_FIELD_125KHZ_LOST        3000    // Sleep delay after leaving the field after leaving the field
#define SLEEP_DELAY_MS_BLE_DISCONNECTED         4000    // BLE's sleep delay after disconnection
#define SLEEP_DELAY_MS_USB_POWER_DISCONNECTED   3000    // The sleep delay after the USB power supply is broken
#define SLEEP_NO_BATTERY_SHUTDOWN               1       // Turn off at a low volume


void sleep_timer_init(void);
void sleep_timer_start(uint32_t time_ms);
void sleep_timer_stop(void);
void sleep_system_run(void (*sysOffSleep)(), void (*sysOnSleep)());

#endif
