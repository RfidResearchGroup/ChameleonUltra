#ifndef NRF_GPIO_H
#define NRF_GPIO_H

#include <stdint.h>

void nrf_gpio_cfg_output(uint32_t pin);
void nrf_gpio_pin_clear(uint32_t pin);

#endif
