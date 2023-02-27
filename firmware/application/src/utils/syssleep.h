#ifndef SYS_SLEEP_H__
#define SYS_SLEEP_H__

#include <stdint.h>

//           休眠态唤醒设备
#define SLEEP_DELAY_MS_BUTTON_WAKEUP            4000    // 按钮唤醒的休眠延迟
#define SLEEP_DELAY_MS_FIELD_WAKEUP             4000    // 场唤醒的休眠延迟（包括高低频）
#define SLEEP_DELAY_MS_FRIST_POWER              1000    // 首次供电的休眠延迟（接入电池）

//           运行态重新延迟
#define SLEEP_DELAY_MS_BUTTON_CLICK             4000    // 按钮点击的休眠延迟
#define SLEEP_DELAY_MS_FIELD_NFC_LOST           2000    // 高频模拟卡离开场后的休眠延迟
#define SLEEP_DELAY_MS_FIELD_125KHZ_LOST        2000    // 低频模拟卡离开场后的休眠延迟
#define SLEEP_DELAY_MS_BLE_DISCONNECTED         4000    // BLE断开后的休眠延迟
#define SLEEP_DELAY_MS_USB_POWER_DISCONNECTED   3000    // USB供电断开后的休眠延迟


void sleep_timer_init(void);
void sleep_timer_start(uint32_t time_ms);
void sleep_timer_stop(void);
void sleep_system_run(void (*sysOffSleep)(), void (*sysOnSleep)());

#endif
