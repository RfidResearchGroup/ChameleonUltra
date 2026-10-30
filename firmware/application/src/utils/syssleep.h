#ifndef SYS_SLEEP_H__
#define SYS_SLEEP_H__

#include <stdint.h>

// Fixed delays for special cases; everything else uses settings_get_sleep_timeout()
#define SLEEP_DELAY_MS_FIRST_POWER              1000    // The sleep delay of the first power supply (access to the battery)
#define SLEEP_NO_BATTERY_SHUTDOWN               1       // Turn off at a low volume


void sleep_timer_init(void);
void sleep_timer_start(uint32_t time_ms);
void sleep_timer_stop(void);
void sleep_system_run(void (*sysOffSleep)(), void (*sysOnSleep)());

#endif
