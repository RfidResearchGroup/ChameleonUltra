#include "nrfx_power.h"
#include "app_timer.h"
#include "syssleep.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


APP_TIMER_DEF(m_app_sleep_timer);       // 用于设备休眠的定时器
static volatile bool m_system_off_enter = false;

extern bool g_is_ble_connected; // 标志BLE的链接状态
extern bool g_is_tag_emulating; // 标志模拟卡的状态


/** @brief 设备休眠定时器事件
 * @param 无
 * @return 无
 */
static void timer_sleep_event_handle(void *arg)
{
    // 休眠条件达到，设置标志位，让main中处理即可
    m_system_off_enter = true;
}

/**@brief 休眠软定时器初始化
 */
void sleep_timer_init(void) {
    ret_code_t err_code;

    // 创建软定时器，等待一段时间后进行休眠
    err_code = app_timer_create(&m_app_sleep_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_sleep_event_handle);
    APP_ERROR_CHECK(err_code);
}

/**@brief 休眠软定时器停止
 */
void sleep_timer_stop() {
    m_system_off_enter = false;
    ret_code_t err_code = app_timer_stop(m_app_sleep_timer);
    APP_ERROR_CHECK(err_code);
}

/**@brief 休眠软定时器启动
 */
void sleep_timer_start(uint32_t time_ms) {
    // 先关闭之前的休眠定时器
    sleep_timer_stop();
    // 非USB供电状态
    if (nrfx_power_usbstatus_get() == NRFX_POWER_USB_STATE_DISCONNECTED) {
        // 如果蓝牙还连接着，或者还在模拟卡状态，就不需要启动休眠
        if (g_is_ble_connected == false && g_is_tag_emulating == false) {
            // 启动定时器
            ret_code_t err_code = app_timer_start(m_app_sleep_timer, APP_TIMER_TICKS(time_ms), NULL);
            APP_ERROR_CHECK(err_code);
        }
    }
}

/**
 *@brief 运行休眠的具体实现
 *@param sysOff: The system off function implment.
 */
void sleep_system_run(void (*sysOffSleep)(), void (*sysOnSleep)()) {
    // No task to process, sleep enter
    if (m_system_off_enter) {
        m_system_off_enter = false;
        // Enter Sleep(System_OFF sleep mode) zzzzz.....
        sysOffSleep();
    } else {
        // Enter sleep(System_ON sleep mode) zzzzz.....
        // If no task can to process, we can sleep cpu sometime.
        sysOnSleep();
    }
}
