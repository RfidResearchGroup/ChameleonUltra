#include "battery_health.h"

battery_health_t battery_health_from_measurement(uint16_t voltage, uint8_t percent) {
    if ((voltage >= 4100U) && (percent >= 80U)) {
        return BATTERY_HEALTH_EXCELLENT;
    }
    if ((voltage >= 3950U) && (percent >= 60U)) {
        return BATTERY_HEALTH_GOOD;
    }
    if ((voltage >= 3750U) && (percent >= 30U)) {
        return BATTERY_HEALTH_FAIR;
    }
    if ((voltage >= 3600U) && (percent >= 15U)) {
        return BATTERY_HEALTH_LOW;
    }
    return BATTERY_HEALTH_CRITICAL;
}

uint8_t battery_health_to_code(battery_health_t health) {
    return (uint8_t)health;
}
