#ifndef CCID_TRANSPORT_TEST_APP_TIMER_H
#define CCID_TRANSPORT_TEST_APP_TIMER_H

#include <stdint.h>

#define APP_TIMER_TICKS(milliseconds) ((uint32_t)(milliseconds))
uint32_t app_timer_cnt_get(void);
uint32_t app_timer_cnt_diff_compute(uint32_t now, uint32_t previous);

#endif
