#include "battery_health.h"

battery_health_t battery_health_from_measurement(uint16_t voltage, uint8_t percent) {
    (void)voltage;

    // Keep the condition hint aligned with the user-facing battery bar:
    // we classify primarily by estimated remaining capacity instead of raw
    // voltage so the hint stays stable across cells and hardware variants.
    if (percent >= 88U) {
        return BATTERY_HEALTH_EXCELLENT;
    }
    if (percent >= 63U) {
        return BATTERY_HEALTH_GOOD;
    }
    if (percent >= 38U) {
        return BATTERY_HEALTH_FAIR;
    }
    if (percent >= 13U) {
        return BATTERY_HEALTH_LOW;
    }
    return BATTERY_HEALTH_CRITICAL;
}

uint8_t battery_health_to_code(battery_health_t health) {
    return (uint8_t)health;
}
