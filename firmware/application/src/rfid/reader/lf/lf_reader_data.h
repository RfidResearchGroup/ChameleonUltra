#ifndef __READER_IO_H__
#define __READER_IO_H__

#include <stdint.h>

// #define debug410x

#ifdef __cplusplus
 extern "C" {
#endif

typedef void(*RIO_CALLBACK_S)(void); // µ÷ÓÃº¯Êı¸ñÊ½

void register_rio_callback(RIO_CALLBACK_S P);
void blank_function(void);
void unregister_rio_callback(void);
void GPIO_INT0_IRQHandler(void);

// è®¡æ•°å™¨æ“ä½œ
uint32_t get_lf_counter_value(void);
void clear_lf_counter_value(void);

#ifdef __cplusplus
}
#endif

#endif
