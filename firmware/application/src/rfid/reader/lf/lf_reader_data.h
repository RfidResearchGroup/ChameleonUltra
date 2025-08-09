#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RIO_CALLBACK_S)(void);  // Call the function format

void register_rio_callback(RIO_CALLBACK_S P);
void unregister_rio_callback(void);
void gpio_int0_irq_handler(void);

// Counter
uint32_t get_lf_counter_value(void);
void clear_lf_counter_value(void);

bool em410x_read(uint8_t *data, uint32_t timeout_ms);
bool hidprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
