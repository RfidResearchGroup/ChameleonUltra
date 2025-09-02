#ifndef _DrvTime2_h_
#define _DrvTime2_h_

#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif
// Define the maximum number of timer that can be used at the same time
#define TIMER_BSP_COUNT 10

// Define a structure
// This structure stores basic clock information
typedef struct {
    // The number of ticks of the current timer
    volatile uint32_t time;
    // Whether it is busy
    uint8_t busy;
} autotimer;

// Realize a grand definition of judgment timeout
#define NO_TIMEOUT_1MS(timer, count) ((((autotimer *)timer)->time <= (count)) ? 1 : 0)

void bsp_timer_init(void);
void bsp_timer_uninit(void);
void bsp_timer_start(void);
void bsp_timer_stop(void);

void bsp_return_timer(autotimer *timer);
autotimer *bsp_obtain_timer(uint32_t start_value);
uint8_t bsp_set_timer(autotimer *timer, uint32_t start_value);

#endif
