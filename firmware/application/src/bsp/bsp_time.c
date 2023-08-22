#include "bsp_time.h"
#include "app_timer.h"


#define TICK_PERIOD APP_TIMER_TICKS(10) // 定时时间

// 定义一个软定时器
APP_TIMER_DEF(m_app_timer);

// 定时器池
autotimer bsptimers[TIMER_BSP_COUNT] = { 0 };
// 定时器迭代位置
static uint8_t g_timer_fori;
// 当前定时器运行状态
static volatile enum {
    UNINIT,
    INIT,
    START,
    STOP,
} bsp_timer_state = UNINIT;


/*
* 获取一个空闲的定时器，这个定时器
* 1、会自动跑滴答
* 2、是空闲的
*/
autotimer* bsp_obtain_timer(uint32_t start_value) {
    uint8_t i;
    for (i = 0; i < TIMER_BSP_COUNT; i++) {
        if (bsptimers[i].busy == 0) {
            bsptimers[i].time = start_value;
            bsptimers[i].busy = 1;
            break;
        }
    }
    return &bsptimers[i];
}

/*
* 设置定时器，该操作会操作目标定时器，修改当前值
*/
inline uint8_t bsp_set_timer(autotimer* timer,uint32_t start_value) {
    if(timer->busy == 0) return 0;
    timer->time = start_value;
    return 1;
}

/*
* 归还定时器，该操作会自动释放定时器
* 并且对定时器归零
*/
inline void bsp_return_timer(autotimer* timer) {
    timer->busy = 0;
    timer->time = 0;
}

/** @brief 测试定时器的回调函数
 * @param arg 回调参数
 * @return 无
 */
void timer_app_callback(void *arg)
{
    UNUSED_PARAMETER(arg);
    for (g_timer_fori = 0; g_timer_fori < TIMER_BSP_COUNT; g_timer_fori++) {
        if (bsptimers[g_timer_fori].busy == 1) {
            bsptimers[g_timer_fori].time += 10;
        }
    }
}

// 初始化定时器
void bsp_timer_init(void) {
    if (bsp_timer_state == UNINIT) {
        bsp_timer_state = INIT;
        // 创建定时器
        ret_code_t err_code = app_timer_create(&m_app_timer, APP_TIMER_MODE_REPEATED, timer_app_callback);
        APP_ERROR_CHECK(err_code);
    }
}

// 反初始化定时器
void bsp_timer_uninit(void) {
    // 暂时无法反初始化软定时器，只能关闭
    bsp_timer_stop();
}

// 启动定时器
void bsp_timer_start(void) {
    if (bsp_timer_state != UNINIT) {
        // 确保定时器没有被启动过
        if (bsp_timer_state != START) {
            app_timer_start(m_app_timer, TICK_PERIOD, NULL);
            bsp_timer_state = START;
        }
    }

}

// 停止定时器
void bsp_timer_stop(void) {
    if (bsp_timer_state != UNINIT) {
        if (bsp_timer_state == START) {
            // 停止定时器
            app_timer_stop(m_app_timer);
            bsp_timer_state = STOP;
        }
    }
}
