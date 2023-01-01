#ifndef _DrvTime2_h_
#define _DrvTime2_h_

#include <stdint.h>

#ifndef NULL
#define NULL        ((void *)0)
#endif
//定义可以同时使用的计时器的最多数量
#define TIMER_BSP_COUNT 10

// 定义一个结构体
// 这个结构体存放了基本的时钟信息
typedef struct {
    // 当前定时器的滴答数
    volatile uint32_t time;
    // 是否繁忙
    uint8_t busy;
} autotimer;


// 实现一个判断超时的宏定义
#define NO_TIMEOUT_1MS(timer, count)    ((((autotimer*)timer)->time <= (count))?  1: 0)

void bsp_timer_init(void);
void bsp_timer_uninit(void);
void bsp_timer_start(void);
void bsp_timer_stop(void);

void bsp_return_timer(autotimer* timer);
autotimer* bsp_obtain_timer(uint32_t start_value);
uint8_t bsp_set_timer(autotimer* timer,uint32_t start_value);


#endif
