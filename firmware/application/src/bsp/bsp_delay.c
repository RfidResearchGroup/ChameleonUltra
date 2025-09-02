#include "bsp_delay.h"

#include "bsp_time.h"
#include "nrf_delay.h"

// Delay NMS
// Pay attention to the range of NMS
void bsp_delay_ms(uint16_t nms) { nrf_delay_us(nms * 1000); }

// Delay NUS
// NUS is the number of US numbers to be delayed.
void bsp_delay_us(uint32_t nus) { nrf_delay_us(nus); }
