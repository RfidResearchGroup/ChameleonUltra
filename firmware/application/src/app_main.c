#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf.h"

#include "app_timer.h"
#include "app_usbd.h"
#include "app_util_platform.h"
#include "nrf_delay.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_rng.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"
#include "nrfx_nfct.h"
#include "nrfx_power.h"

#define NRF_LOG_MODULE_NAME app_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "app_cmd.h"
#include "ble_main.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "dataframe.h"
#include "fds_util.h"
#include "hex_utils.h"
#include "rfid_main.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "usb_main.h"
#include "rgb_marquee.h"



// 定义软定时器
APP_TIMER_DEF(m_button_check_timer); // 用于按钮防抖的定时器
static bool m_is_read_btn_press = false;
static bool m_is_write_btn_press = false;

// cpu reset reason
static uint32_t m_reset_source;


/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyse
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] p_file_name File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    // /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

/**@brief Function for initializing the timer module.
 */
static void app_timers_init(void) {
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void) {
    ret_code_t err_code;

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    // 注意，如果插着jlink或者开着debug，进入低功耗的函数可能会报错，
    // 开启调试时我们应当禁用低功耗状态值检测，或者干脆不进入低功耗
    err_code = sd_power_system_off();

    // OK，此处非常重要，如果开启了日志输出并且使能了RTT，则不去检查低功耗模式的错误
#if !(NRF_LOG_ENABLED && NRF_LOG_BACKEND_RTT_ENABLED)
    APP_ERROR_CHECK(err_code);
#else
    UNUSED_VARIABLE(err_code);
#endif
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void) {
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing power management.
 */
static void power_management_init(void) {
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing power management.
 */
static void rng_drv_and_srand_init(void) {
    ret_code_t err_code;
    uint8_t available;
    uint32_t rand_int;

    // 先初始化官方的rng管理驱动api
    err_code = nrf_drv_rng_init(NULL);
    APP_ERROR_CHECK(err_code);

    // 等待随机数管理器生成足够的随机数放到队列里
    do {
        nrf_drv_rng_bytes_available(&available);
    } while (available < 4);

    // 注意，此处我们是将一个uint32_t的值的地址强制转换为uint8_t的地址
    // 以获得uint32的首个字节的指针的指向
    err_code = nrf_drv_rng_rand(((uint8_t *)(&rand_int)), 4);
    APP_ERROR_CHECK(err_code);

    // 最后初始化c标准库中的srand种子
    srand(rand_int);
}

/**@brief 初始化GPIO矩阵库
 */
static void gpio_te_init(void) {
    // 初始化GPIOTE
    uint32_t err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief 按钮矩阵事件
 */
static void button_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    device_mode_t mode = get_device_mode();
    // 暂时只允许模拟卡模式响应按钮的操作
    if (mode == DEVICE_MODE_TAG) {
        static nrf_drv_gpiote_pin_t pin_static;                                  // 使用静态内部变量去存放当前发生事件的GPIO
        pin_static = pin;                                                        // 缓存当前触发事件的按钮到内部变量中
        app_timer_start(m_button_check_timer, APP_TIMER_TICKS(50), &pin_static); // 启动定时器防抖
    }
}

/** @brief 按钮防抖定时器
 * @param 无
 * @return 无
 */
static void timer_button_event_handle(void *arg) {
    nrf_drv_gpiote_pin_t pin = *(nrf_drv_gpiote_pin_t *)arg;
    // 在此处检查一下当前GPIO是否是处于按下的电平状态
    if (nrf_gpio_pin_read(pin) == 1) {
        if (pin == BUTTON_1) {
            NRF_LOG_INFO("BUTTON_LEFT");
            m_is_read_btn_press = true;
        }
        if (pin == BUTTON_2) {
            NRF_LOG_INFO("BUTTON_RIGHT");
            m_is_write_btn_press = true;
        }
    }
}

/**@brief Function for init button and led.
 */
static void button_init(void) {
    ret_code_t err_code;

    // 初始化按钮防抖的非精确的定时器
    err_code = app_timer_create(&m_button_check_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_event_handle);
    APP_ERROR_CHECK(err_code);

    // 配置SENSE模式，选择fales为sense配置
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
    in_config.pull = NRF_GPIO_PIN_PULLDOWN; // 下拉

    // 配置按键绑定POTR
    err_code = nrf_drv_gpiote_in_init(BUTTON_1, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_1, true);

    err_code = nrf_drv_gpiote_in_init(BUTTON_2, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_2, true);
}

/**@brief 进入深度休眠的实现函数
 */
static void system_off_enter(void) {

    // 先禁用掉HF NFC的事件
    NRF_NFCT->INTENCLR = NRF_NFCT_DISABLE_ALL_INT;
    // 然后再禁用掉LF LPCOMP的事件
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;

    // 配置一下RAM休眠保持
    ret_code_t ret;
    uint32_t ram8_retention = // RAM8 每个 section 都有32KB的容量
                              // POWER_RAM_POWER_S0RETENTION_On << POWER_RAM_POWER_S0RETENTION_Pos ;
                              // POWER_RAM_POWER_S1RETENTION_On << POWER_RAM_POWER_S1RETENTION_Pos |
                              // POWER_RAM_POWER_S2RETENTION_On << POWER_RAM_POWER_S2RETENTION_Pos |
                              // POWER_RAM_POWER_S3RETENTION_On << POWER_RAM_POWER_S3RETENTION_Pos |
                              // POWER_RAM_POWER_S4RETENTION_On << POWER_RAM_POWER_S4RETENTION_Pos |
        POWER_RAM_POWER_S5RETENTION_On << POWER_RAM_POWER_S5RETENTION_Pos;
    ret = sd_power_ram_power_set(8, ram8_retention);
    APP_ERROR_CHECK(ret);


    // 关机动画
    uint8_t slot = tag_emulation_get_slot();
    uint32_t* p_led_array = hw_get_led_array();
    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(p_led_array[i]);
    }
    uint8_t dir = slot > 3 ? 1 : 0;
    uint8_t color = get_color_by_slot(slot);
    if (m_reset_source & (NRF_POWER_RESETREAS_NFC_MASK | NRF_POWER_RESETREAS_LPCOMP_MASK)) {
        if (m_reset_source & NRF_POWER_RESETREAS_NFC_MASK) {
            color = 1;
        } else {
            color = 2;
        }
    }
    ledblink5(color, slot, dir ? 7 : 0);
    ledblink4(color, dir, 7, 99, 75);
    ledblink4(color, !dir, 7, 75, 50);
    ledblink4(color, dir, 7, 50, 25);
    ledblink4(color, !dir, 7, 25, 0);


    // 需要配置为浮空模拟输入且不上下拉的IO
    uint32_t gpio_cfg_default_nopull[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_SPI_SELECT,
        HF_SPI_MISO,
        HF_SPI_MOSI,
        HF_SPI_MOSI,
        LF_OA_OUT,
#endif
        BAT_SENSE,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_default_nopull); i++) {
        nrf_gpio_cfg_default(gpio_cfg_default_nopull[i]);
    }

    // 需要配置为推挽输出且拉高的IO
    uint32_t gpio_cfg_output_high[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_ANT_SEL,
#endif
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_high); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_high[i]);
        nrf_gpio_pin_set(gpio_cfg_output_high[i]);
    }

    // 需要配置为推挽输出且拉低的IO
    uint32_t gpio_cfg_output_low[] = {
        LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8, LED_R, LED_G, LED_B, LF_MOD, 
#if defined(PROJECT_CHAMELEON_ULTRA)
        READER_POWER, LF_ANT_DRIVER
#endif
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_low); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_low[i]);
        nrf_gpio_pin_clear(gpio_cfg_output_low[i]);
    }

    // 等一会儿再休眠，避免GPIO电路配置波动唤醒芯片
    bsp_delay_ms(50);

    // 然后把卡槽配置等数据进行保存
    tag_emulation_save();

    // 然后进行休眠
    NRF_LOG_INFO("Sleep finally, Bye ^.^");
    // 关闭所有的软定时器
    app_timer_stop_all();
    // 调用系统休眠
    sleep_mode_enter();

    // 本不应该进入这里，但是jlink调试模式下可以进入，顶多是无法正常休眠罢了
    // jlink连接的时候，功耗会上升，并且休眠也会卡在这个步骤。
    while (1)
        NRF_LOG_PROCESS();
}

/**
 *@brief :检测唤醒源
 */
static void check_wakeup_src(void) {
    sd_power_reset_reason_get(&m_reset_source);
    sd_power_reset_reason_clr(m_reset_source);

    /*
     * 注意：下方描述的休眠是深度休眠，停止任何非唤醒源的外设，停止CPU，达到最低功耗
     *
     * 如果唤醒源是按钮，那么需要开启BLE广播，直到按钮停止点击后一段时间后休眠
     * 如果唤醒源是模拟卡的场，不需要开启BLE广播，直到模拟卡结束后休眠。
     * 如果唤醒源是USB，那么就一直开启BLE，并且不进行休眠，直到USB拔掉
     * 如果唤醒源是首次接入电池，则啥都不干，直接进入休眠
     *
     * 提示：上述；逻辑为唤醒阶段处理的逻辑，剩下的逻辑转换为运行时的处理阶段
     */

    uint8_t slot = tag_emulation_get_slot();
    uint8_t dir = slot > 3 ? 1 : 0;
    uint8_t color = get_color_by_slot(slot);
    
    if (m_reset_source & NRF_POWER_RESETREAS_OFF_MASK) {
        NRF_LOG_INFO("WakeUp from button");
        advertising_start(); // 启动蓝牙广播

        // 按钮唤醒的开机动画
        ledblink2(color, !dir, 11);
        ledblink2(color, dir, 11);
        ledblink2(color, !dir, dir ? slot : 7 - slot);
        // 动画结束后亮起当前卡槽的指示灯
        light_up_by_slot();

        // 如果接下来无操作就等待超时后深度休眠
        sleep_timer_start(SLEEP_DELAY_MS_BUTTON_WAKEUP);
    } else if (m_reset_source & (NRF_POWER_RESETREAS_NFC_MASK | NRF_POWER_RESETREAS_LPCOMP_MASK)) {
        NRF_LOG_INFO("WakeUp from rfid field");

        // wake up from hf field.
        if (m_reset_source & NRF_POWER_RESETREAS_NFC_MASK) {
            color = 1;  // HF field show G.
            NRF_LOG_INFO("WakeUp from HF");
        } else {
            color = 2;  // LF filed show B.
            NRF_LOG_INFO("WakeUp from LF");
        }
        // 场唤醒的情况下，只扫一轮RGB作为开机动画
        ledblink2(color, !dir, dir ? slot : 7 - slot);
        set_slot_light_color(color);
        light_up_by_slot();

        // We can only run tag emulation at field wakeup source.
        sleep_timer_start(SLEEP_DELAY_MS_FIELD_WAKEUP);
    } else if (m_reset_source & NRF_POWER_RESETREAS_VBUS_MASK) {
        // nrfx_power_usbstatus_get() can check usb attach status
        NRF_LOG_INFO("WakeUp from VBUS(USB)");
        
        // USB插入和开启通信断口有自身的灯效，暂时不需要亮灯
        // set_slot_light_color(color);
        // light_up_by_slot();

        // 启动蓝牙广播，USB插入的情况下，不需要进行深度休眠
        advertising_start();
    } else {
        NRF_LOG_INFO("First power system");

        // 重置一下noinit ram区域
        uint32_t *noinit_addr = (uint32_t *)0x20038000;
        memset(noinit_addr, 0xFF, 0x8000);
        NRF_LOG_INFO("Reset noinit ram done.");

        // 初始化默认卡槽数据。
        tag_emulation_factory_init();

        ledblink2(0, !dir, 11);
        ledblink2(1, dir, 11);
        ledblink2(2, !dir, 11);

        // 如果首次上电发现USB正插着，我们可以做一些相应的操作
        if (nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED) {
            NRF_LOG_INFO("USB Power found.");
            // usb插着可以随便广播BLE
            advertising_start();
        } else {
            sleep_timer_start(SLEEP_DELAY_MS_FRIST_POWER); // 等一会儿直接进入休眠，啥都不干
        }
    }
}

/**@brief button press event process
 */
extern bool g_usb_led_marquee_enable;
static void button_press_process(void) {
    // 确保AB按钮其中一个发生了点击事件
    if (m_is_read_btn_press || m_is_write_btn_press) {
        // 无论如何，发生了按钮事件，我们需先获得当前激活的卡槽
        uint8_t slot_now = tag_emulation_get_slot();
        uint8_t slot_new = slot_now;
        // 处理某个按钮的事件
        if (m_is_read_btn_press) {
            // Button left press
            m_is_read_btn_press = false;
            slot_new = tag_emulation_slot_find_prev(slot_now);
        }
        if (m_is_write_btn_press) {
            // Button right press
            m_is_write_btn_press = false;
            slot_new = tag_emulation_slot_find_next(slot_now);
        }
        // 仅在新卡槽切换有效的情况下更新状态
        if (slot_new != slot_now) {
            tag_emulation_change_slot(slot_new, true); // 告诉模拟卡模块我们需要切换卡槽
            g_usb_led_marquee_enable = false;
            // 回去场使能类型对应的颜色
            uint8_t color_now = get_color_by_slot(slot_now);
            uint8_t color_new = get_color_by_slot(slot_new);
            
            // 切换卡槽的灯效
            ledblink3(slot_now, color_now, slot_new, color_new);
            // 切换了卡槽，我们需要重新亮灯
            light_up_by_slot();
            // 然后重新切换灯的颜色
            set_slot_light_color(color_new);
        }
        // 重新延迟进入休眠
        sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
    }
}

extern bool g_usb_port_opened;
static void blink_usb_led_status(void) {
    uint8_t slot = tag_emulation_get_slot();
    uint8_t color = get_color_by_slot(slot);
    uint8_t dir = slot > 3 ? 1 : 0;
    static bool is_working = false;
    if (nrfx_power_usbstatus_get() == NRFX_POWER_USB_STATE_DISCONNECTED) {
        if (is_working) {
            rgb_marquee_stop();
            set_slot_light_color(color);
            light_up_by_slot();
            is_working = false;
        }
    } else {

        // 灯效是使能状态，可以进行显示
        if (is_rgb_marquee_enable()) {
            is_working = true;
            if (g_usb_port_opened) {
                ledblink1(color, dir);
            } else {
                ledblink6();
            }
        } else {
            if (is_working) {
                is_working = false;
                rgb_marquee_stop();
                set_slot_light_color(color);
                light_up_by_slot();
            }
        }
    }
}


/**@brief Application main function.
 */
int main(void) {
    hw_connect_init();        // 记得先把引脚初始化一下
    log_init();               // 日志初始化
    gpio_te_init();           // 初始化GPIO矩阵库
    app_timers_init();        // 初始化软定时器
    fds_util_init();          // 初始化fds工具封装
    bsp_timer_init();         // 初始化超时定时器
    bsp_timer_start();        // 启动BSP TIMER，准备用于处理业务逻辑
    button_init();            // 按钮初始化
    init_leds();              // LED初始化
    sleep_timer_init();       // 休眠用的软定时器初始化
    rng_drv_and_srand_init(); // 随机数生成器初始化
    power_management_init();  // 电源管理初始化
    usb_cdc_init();           // USB cdc模拟初始化
    ble_slave_init();         // 蓝牙协议栈初始化
    tag_emulation_init();     // 模拟卡初始化
    rgb_marquee_init();       // 灯效初始化

    // cmd callback register
    on_data_frame_complete(on_data_frame_received);
    
    check_wakeup_src();       // 检测唤醒源，根据唤醒源决定BLE广播与后续休眠动作
    tag_mode_enter();         // 默认进入卡模拟模式

    // usbd event listener
    APP_ERROR_CHECK(app_usbd_power_events_enable());

    // Enter main loop.
    NRF_LOG_INFO("Chameleon working");
    while (1) {
        // Button event process
        button_press_process();
        // Led blink at usb status
        blink_usb_led_status();
        // Data pack process
        data_frame_process();
        // Log print process
        while (NRF_LOG_PROCESS());
        // USB event process
        while (app_usbd_event_queue_process());
        // No task to process, system sleep enter.
        // If system idle sometime, we can enter deep sleep state.
        // Some task process done, we can enter cpu sleep state.
        sleep_system_run(system_off_enter, nrf_pwr_mgmt_run);
    }
}
