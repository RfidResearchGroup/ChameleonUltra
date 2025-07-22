#ifndef __READER_IO_H__
#define __READER_IO_H__

#include <stddef.h>
#include <stdint.h>

// #define debug410x

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RIO_CALLBACK_S)(void);  // Call the function format
typedef void (*SAADC_CALLBACK_S)(int16_t *, size_t);

void register_rio_callback(RIO_CALLBACK_S P);
void unregister_rio_callback(void);
void register_saadc_callback(SAADC_CALLBACK_S P);
void unregister_saadc_callback(void);
void gpio_int0_irq_handler(void);
void saadc_irq_handler(int16_t *val, size_t);

// Counter
uint32_t get_lf_counter_value(void);
void clear_lf_counter_value(void);

#ifdef __cplusplus
}
#endif

#endif
