#ifndef BLE_MAIN_H
#define BLE_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int16_t nrf_saadc_value_t;
typedef void (*lf_adc_callback_t)(nrf_saadc_value_t *, size_t);

bool is_nus_working(void);
bool advertising_pause(bool *was_active);
void advertising_start(bool erase_bonds);

#endif
