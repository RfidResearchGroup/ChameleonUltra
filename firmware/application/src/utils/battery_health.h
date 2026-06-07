#ifndef BATTERY_HEALTH_H
#define BATTERY_HEALTH_H

#include <stdint.h>

typedef enum {
    BATTERY_HEALTH_CRITICAL = 0,
    BATTERY_HEALTH_LOW = 1,
    BATTERY_HEALTH_FAIR = 2,
    BATTERY_HEALTH_GOOD = 3,
    BATTERY_HEALTH_EXCELLENT = 4,
} battery_health_t;

// Compact battery condition hint derived from voltage and percentage.
battery_health_t battery_health_from_measurement(uint16_t voltage, uint8_t percent);
uint8_t battery_health_to_code(battery_health_t health);

#endif
