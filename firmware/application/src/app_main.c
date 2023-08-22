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
#include "nrf_drv_lpcomp.h"

#define NRF_LOG_MODULE_NAME app_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "app_cmd.h"
#include "ble_main.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "bsp_wdt.h"
#include "dataframe.h"
#include "fds_util.h"
#include "hex_utils.h"
#include "rfid_main.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "usb_main.h"
#include "rgb_marquee.h"

#include "settings.h"

// Defining soft timers
APP_TIMER_DEF(m_button_check_timer); // Timer for button debounce
static bool m_is_b_btn_press = false;
static bool m_is_a_btn_press = false;

// cpu reset reason
static uint32_t m_reset_source;
static uint32_t m_gpregret_val;

#define GPREGRET_CLEAR_VALUE_DEFAULT (0xFFFFFFFFUL)
#define RESET_ON_LF_FIELD_EXISTS_Msk (1UL)

extern bool g_is_low_battery_shutdown;


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

    // First initialize the official rng management driver api
    err_code = nrf_drv_rng_init(NULL);
    APP_ERROR_CHECK(err_code);

    // Wait for the random number generator to generate enough random numbers to put in the queue
    do {
        nrf_drv_rng_bytes_available(&available);
    } while (available < 4);

    // Note that here we are forcing the address of a uint32_t value to be converted to a uint8_t address
    // to get the pointer to the first byte of uint32
    err_code = nrf_drv_rng_rand(((uint8_t *)(&rand_int)), 4);
    APP_ERROR_CHECK(err_code);

    // Finally initialize the srand seeds in the c standard library
    srand(rand_int);
}

/**@brief Initialize GPIO matrix library
 */
static void gpio_te_init(void) {
    // Initialize GPIOTE
    uint32_t err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Button Matrix Events
 */
static void button_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    device_mode_t mode = get_device_mode();
    // Temporarily allow only the analog card mode to respond to button operations
    if (mode == DEVICE_MODE_TAG) {
        static nrf_drv_gpiote_pin_t pin_static;                                  // Use static internal variables to store the GPIO where the current event occurred
        pin_static = pin;                                                        // Cache the button that currently triggers the event into an internal variable
        app_timer_start(m_button_check_timer, APP_TIMER_TICKS(50), &pin_static); // Start timer anti-shake
    }
}

/** @brief Button anti-shake timer
 * @param None
 * @return None
 */
static void timer_button_event_handle(void *arg) {
    nrf_drv_gpiote_pin_t pin = *(nrf_drv_gpiote_pin_t *)arg;
    // Check here if the current GPIO is at the pressed level
    if (nrf_gpio_pin_read(pin) == 1) {
        if (pin == BUTTON_1) {
            // If button is disable, we can didn't dispatch key event.
            if (settings_get_button_press_config('b') != SettingsButtonDisable) {
                NRF_LOG_INFO("BUTTON_LEFT"); // Button B?
                m_is_b_btn_press = true;
            }
        }
        if (pin == BUTTON_2) {
            if (settings_get_button_press_config('a') != SettingsButtonDisable) {
                NRF_LOG_INFO("BUTTON_RIGHT"); // Button A?
                m_is_a_btn_press = true;
            }
        }
    }
}

/**@brief Function for init button and led.
 */
static void button_init(void) {
    ret_code_t err_code;

    // Non-exact timer for initializing button anti-shake
    err_code = app_timer_create(&m_button_check_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_event_handle);
    APP_ERROR_CHECK(err_code);

    // Configure SENSE mode, select false for sense configuration
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
    in_config.pull = NRF_GPIO_PIN_PULLDOWN; // Pulldown

    // Configure key binding POTR
    err_code = nrf_drv_gpiote_in_init(BUTTON_1, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_1, true);

    err_code = nrf_drv_gpiote_in_init(BUTTON_2, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_2, true);
}

/**@brief The implementation function to enter deep hibernation
 */
static void system_off_enter(void) {
    ret_code_t ret;

    // Disable the HF NFC event first
    NRF_NFCT->INTENCLR = NRF_NFCT_DISABLE_ALL_INT;
    // Then disable the LF LPCOMP event
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;

    // Save tag data
    tag_emulation_save();

    // Configure RAM hibernation hold
    uint32_t ram8_retention = // RAM8 Each section has 32KB capacity
                              // POWER_RAM_POWER_S0RETENTION_On << POWER_RAM_POWER_S0RETENTION_Pos ;
                              // POWER_RAM_POWER_S1RETENTION_On << POWER_RAM_POWER_S1RETENTION_Pos |
                              // POWER_RAM_POWER_S2RETENTION_On << POWER_RAM_POWER_S2RETENTION_Pos |
                              // POWER_RAM_POWER_S3RETENTION_On << POWER_RAM_POWER_S3RETENTION_Pos |
                              // POWER_RAM_POWER_S4RETENTION_On << POWER_RAM_POWER_S4RETENTION_Pos |
        POWER_RAM_POWER_S5RETENTION_On << POWER_RAM_POWER_S5RETENTION_Pos;
    ret = sd_power_ram_power_set(8, ram8_retention);
    APP_ERROR_CHECK(ret);

    if (g_is_low_battery_shutdown) {
        // Don't create too complex animations, just blink LED1 three times.
        rgb_marquee_stop();
        set_slot_light_color(0);
        for (uint8_t i = 0; i <= 3; i++) {
            nrf_gpio_pin_set(LED_1);
            bsp_delay_ms(100);
            nrf_gpio_pin_clear(LED_1);
            bsp_delay_ms(100);
        }
    } else {
        // close all led.
        uint32_t* p_led_array = hw_get_led_array();
        for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_clear(p_led_array[i]);
        }
        uint8_t animation_config = settings_get_animation_config();
        if (animation_config == SettingsAnimationModeFull) {
            uint8_t slot = tag_emulation_get_slot();
            // Power off animation
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
        }
        rgb_marquee_stop();
    }

    // IOs that need to be configured as floating analog inputs ==> no pull-up or pull-down
    uint32_t gpio_cfg_default_nopull[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_SPI_SELECT,
        HF_SPI_MISO,
        HF_SPI_MOSI,
        HF_SPI_MOSI,
        LF_OA_OUT,
#endif
        BAT_SENSE_PIN,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_default_nopull); i++) {
        nrf_gpio_cfg_default(gpio_cfg_default_nopull[i]);
    }

    // IO that needs to be configured as a push-pull output and pulled high
    uint32_t gpio_cfg_output_high[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_ANT_SEL,
#endif
        LED_FIELD, LED_R, LED_G, LED_B,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_high); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_high[i]);
        nrf_gpio_pin_set(gpio_cfg_output_high[i]);
    }

    // IOs that need to be configured as push-pull outputs and pulled low
    uint32_t gpio_cfg_output_low[] = {
        LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8, LF_MOD, 
#if defined(PROJECT_CHAMELEON_ULTRA)
        READER_POWER, LF_ANT_DRIVER
#endif
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_low); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_low[i]);
        nrf_gpio_pin_clear(gpio_cfg_output_low[i]);
    }

    // Wait for a while before hibernating to avoid GPIO circuit configuration fluctuations to wake up the chip
    bsp_delay_ms(50);

    // Print leaving message finally
    NRF_LOG_INFO("Sleep finally, Bye ^.^");
    // Turn off all soft timers
    app_timer_stop_all();

    // 检查是否存在低频场，解决休眠时有非常强的场信号一直使比较器处于高电平输入状态从而无法产生上升沿而无法唤醒系统的问题。
    if(lf_is_field_exists()) {
        // 关闭比较器
        nrf_drv_lpcomp_disable();
        // 设置reset原因，重启后需要拿到此原因，避免误判唤醒源
        sd_power_gpregret_clr(1, GPREGRET_CLEAR_VALUE_DEFAULT);
        sd_power_gpregret_set(1, RESET_ON_LF_FIELD_EXISTS_Msk);
        // 触发reset唤醒系统，重新启动模拟过程
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
        return;
    };

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    // 注意，如果插着jlink或者开着debug，进入低功耗的函数可能会报错，
    // 开启调试时我们应当禁用低功耗状态值检测，或者干脆不进入低功耗
    ret = sd_power_system_off();

    // OK，此处非常重要，如果开启了日志输出并且使能了RTT，则不去检查低功耗模式的错误
#if !(NRF_LOG_ENABLED && NRF_LOG_BACKEND_RTT_ENABLED)
    APP_ERROR_CHECK(ret);
#else
    UNUSED_VARIABLE(ret);
#endif

    // It is not supposed to enter here, but jlink debug mode it can be entered, at most is not normal hibernation just
    // jlink connection, power consumption will rise, and hibernation will also be stuck in this step.
    while (1)
        NRF_LOG_PROCESS();
}

/**
 *@brief :Detection of wake-up source
 */
static void check_wakeup_src(void) {
    sd_power_reset_reason_get(&m_reset_source);
    sd_power_reset_reason_clr(m_reset_source);

    sd_power_gpregret_get(1, &m_gpregret_val);
    sd_power_gpregret_clr(1, GPREGRET_CLEAR_VALUE_DEFAULT);


    /*
     * Note: The hibernation described below is deep hibernation, stopping any non-wakeup source peripherals and stopping the CPU to achieve the lowest power consumption
     *
     * If the wake-up source is a button, then you need to turn on BLE broadcast until the button stops clicking after a period of time to hibernate
     * If the wake-up source is the field of the analog card, it is not necessary to turn on BLE broadcast until the analog card ends and then hibernate.
     * If the wake-up source is USB, then keep BLE on and no hibernation until USB is unplugged
     * If the wakeup source is the first time to access the battery, then do nothing and go directly to hibernation
     *
     * Hint: The above; logic is the logic processed in the wake-up phase, the rest of the logic is converted to the runtime processing phase
     */

    uint8_t slot = tag_emulation_get_slot();
    uint8_t dir = slot > 3 ? 1 : 0;
    uint8_t color = get_color_by_slot(slot);
    
    if (m_reset_source & NRF_POWER_RESETREAS_OFF_MASK) {
        NRF_LOG_INFO("WakeUp from button");
        advertising_start(); // Turn on Bluetooth radio

        // Button wake-up boot animation
        uint8_t animation_config = settings_get_animation_config();
        if (animation_config == SettingsAnimationModeFull)
        {
            ledblink2(color, !dir, 11);
            ledblink2(color, dir, 11);
            ledblink2(color, !dir, dir ? slot : 7 - slot);
        } else if (animation_config == SettingsAnimationModeMinimal) {
            ledblink2(color, !dir, dir ? slot : 7 - slot);
        } else {
            set_slot_light_color(color);
        }

        // The indicator of the current card slot lights up at the end of the animation
        light_up_by_slot();

        // If no operation follows, wait for the timeout and then deep hibernate
        sleep_timer_start(SLEEP_DELAY_MS_BUTTON_WAKEUP);
    } else if ((m_reset_source & (NRF_POWER_RESETREAS_NFC_MASK | NRF_POWER_RESETREAS_LPCOMP_MASK)) ||
               (m_gpregret_val & RESET_ON_LF_FIELD_EXISTS_Msk)) {
        NRF_LOG_INFO("WakeUp from rfid field");

        // wake up from hf field.
        if (m_reset_source & NRF_POWER_RESETREAS_NFC_MASK) {
            color = 1;  // HF field show G.
            NRF_LOG_INFO("WakeUp from HF");
        } else {
            color = 2;  // LF filed show B.
            if (m_gpregret_val & RESET_ON_LF_FIELD_EXISTS_Msk) {
                NRF_LOG_INFO("Reset by LF");
            } else {
                NRF_LOG_INFO("WakeUp from LF");
            }
        }

        // 当前是模拟卡事件唤醒系统，我们可以让场强灯先亮起来
        TAG_FIELD_LED_ON();

        uint8_t animation_config = settings_get_animation_config();
        if (animation_config == SettingsAnimationModeFull) {
            // In the case of field wake-up, only one round of RGB is swept as the power-on animation
            ledblink2(color, !dir, dir ? slot : 7 - slot);
        }
        set_slot_light_color(color);
        light_up_by_slot();

        // We can only run tag emulation at field wakeup source.
        sleep_timer_start(SLEEP_DELAY_MS_FIELD_WAKEUP);
    } else if (m_reset_source & NRF_POWER_RESETREAS_VBUS_MASK) {
        // nrfx_power_usbstatus_get() can check usb attach status
        NRF_LOG_INFO("WakeUp from VBUS(USB)");
        
        // USB plugged in and open communication break has its own light effect, no need to light up for the time being
        // set_slot_light_color(color);
        // light_up_by_slot();

        // Start Bluetooth radio with USB plugged in, no deep hibernation required
        advertising_start();
    } else {
        NRF_LOG_INFO("First power system");

        // Reset the noinit ram area
        uint32_t *noinit_addr = (uint32_t *)0x20038000;
        memset(noinit_addr, 0xFF, 0x8000);
        NRF_LOG_INFO("Reset noinit ram done.");

        // Initialize the default card slot data.
        tag_emulation_factory_init();

        // RGB
        ledblink2(0, !dir, 11);
        ledblink2(1, dir, 11);
        ledblink2(2, !dir, 11);

        // Show RGB for slot.
        set_slot_light_color(color);
        light_up_by_slot();

        // If the USB is plugged in when first powered up, we can do something accordingly
        if (nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED) {
            NRF_LOG_INFO("USB Power found.");
            // usb plugged in can broadcast BLE at will
            advertising_start();
        } else {
            sleep_timer_start(SLEEP_DELAY_MS_FRIST_POWER); // Wait a while and go straight to hibernation, do nothing
        }
    }
}

/**@brief change slot
 */
static void cycle_slot(bool dec) {
    // In any case, a button event occurs and we need to get the currently active card slot first
    uint8_t slot_now = tag_emulation_get_slot();
    uint8_t slot_new = slot_now;
    // Handle the events of a button
    if (dec) {
        slot_new = tag_emulation_slot_find_prev(slot_now);
    } else {
        slot_new = tag_emulation_slot_find_next(slot_now);
    }
    // Update status only if the new card slot switch is valid
    tag_emulation_change_slot(slot_new, true); // Tell the analog card module that we need to switch card slots
    // Go back to the color corresponding to the field enablement type
    uint8_t color_now = get_color_by_slot(slot_now);
    uint8_t color_new = get_color_by_slot(slot_new);
    // Switching the light effect of the card slot
    ledblink3(slot_now, color_now, slot_new, color_new);
    // Switched the card slot, we need to re-light
    light_up_by_slot();
    // Then switch the color of the light again
    set_slot_light_color(color_new);
}

#if defined(PROJECT_CHAMELEON_ULTRA)

// fast detect a 14a tag uid to sim
static void btn_fn_copy_ic_uid(void) {
    // get 14a tag res buffer;
    uint8_t slot_now = tag_emulation_get_slot();
    tag_specific_type_t tag_type[2];
    tag_emulation_get_specific_type_by_slot(slot_now, tag_type);
    tag_data_buffer_t* buffer = get_buffer_by_tag_type(tag_type[0]);

    nfc_tag_14a_coll_res_entity_t* antres;

    if(tag_type[1] == TAG_TYPE_EM410X) {
        uint8_t status;

        bool is_reader_mode_now = get_device_mode() == DEVICE_MODE_READER;
        // first, we need switch to reader mode.
        if (!is_reader_mode_now) {
            // enter reader mode
            reader_mode_enter();
            bsp_delay_ms(8);
            NRF_LOG_INFO("Start reader mode to offline copy.")
        }

        uint8_t id_buffer[5] = { 0x00 };
        status = PcdScanEM410X(id_buffer);

        if(status == LF_TAG_OK) {
            tag_data_buffer_t* buffer = get_buffer_by_tag_type(TAG_TYPE_EM410X);
            memcpy(buffer->buffer, id_buffer, LF_EM410X_TAG_ID_SIZE);
            tag_emulation_load_by_buffer(TAG_TYPE_EM410X, false);

            // keep reader mode or exit reader mode.
        }

        if (!is_reader_mode_now) {
            tag_mode_enter();
        }
    }

    switch(tag_type[0]) {
        case TAG_TYPE_MIFARE_Mini:
        case TAG_TYPE_MIFARE_1024:
        case TAG_TYPE_MIFARE_2048:
        case TAG_TYPE_MIFARE_4096: {
            nfc_tag_mf1_information_t *p_info = (nfc_tag_mf1_information_t *)buffer->buffer;
            antres = &(p_info->res_coll);
            break;
        }

        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216: {
            nfc_tag_ntag_information_t *p_info = (nfc_tag_ntag_information_t *)buffer->buffer;
            antres = &(p_info->res_coll);
            break;
        }

        default:
            NRF_LOG_ERROR("Unsupported tag type")
            return;
    }

    bool is_reader_mode_now = get_device_mode() == DEVICE_MODE_READER;
    // first, we need switch to reader mode.
    if (!is_reader_mode_now) {
        // enter reader mode
        reader_mode_enter();
        bsp_delay_ms(8);
        pcd_14a_reader_reset();
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
        NRF_LOG_INFO("Start reader mode to offline copy.")
    }

    // select a tag
    picc_14a_tag_t tag;
    uint8_t status;

    status = pcd_14a_reader_scan_auto(&tag);
    if (status == HF_TAG_OK) {
        // copy uid
        memcpy(antres->uid, tag.uid, tag.uid_len);
        // copy atqa
        memcpy(antres->atqa, tag.atqa, 2);
        // copy sak
        antres->sak[0] = tag.sak;
        NRF_LOG_INFO("Offline uid copied")
    } else {
        NRF_LOG_INFO("No tag found: %d", status);
    }

    // keep reader mode or exit reader mode.
    if (!is_reader_mode_now) {
        tag_mode_enter();
    }
}

#endif

/**@brief Execute the corresponding logic based on the functional settings of the buttons.
 */
static void run_button_function_by_settings(settings_button_function_t sbf) {
    switch (sbf)
    {
    case SettingsButtonCycleSlot:
        cycle_slot(false);
        break;
    case SettingsButtonCycleSlotDec:
        cycle_slot(true);
        break;

#if defined(PROJECT_CHAMELEON_ULTRA)
    case SettingsButtonCloneIcUid:
        btn_fn_copy_ic_uid();
        break;
#endif

    default:
        NRF_LOG_ERROR("Unsupported button function")
        break;
    }
}

/**@brief button press event process
 */
extern bool g_usb_led_marquee_enable;
static void button_press_process(void) {
    // Make sure that one of the AB buttons has a click event
    if (m_is_b_btn_press || m_is_a_btn_press) {
        if (m_is_a_btn_press) {
            run_button_function_by_settings(settings_get_button_press_config('a'));
            m_is_a_btn_press = false;
        }
        if (m_is_b_btn_press) {
            run_button_function_by_settings(settings_get_button_press_config('b'));
            m_is_b_btn_press = false;
        }
        // Disable led marquee for usb at button pressed.
        g_usb_led_marquee_enable = false;
        // Re-delay into hibernation
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
        // The light effect is enabled and can be displayed
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
    hw_connect_init();        // Remember to initialize the pins first
    init_leds();              // LED initialization

    log_init();               // Log initialization
    gpio_te_init();           // Initialize GPIO matrix library
    app_timers_init();        // Initialize soft timer
    fds_util_init();          // Initialize fds tool package
    bsp_timer_init();         // Initialize timeout timer
    bsp_timer_start();        // Start BSP TIMER and prepare it for processing business logic
    button_init();            // Button initialization for handling business logic
    sleep_timer_init();       // Soft timer initialization for hibernation
    rng_drv_and_srand_init(); // Random number generator initialization
    power_management_init();  // Power management initialization
    usb_cdc_init();           // USB cdc emulation initialization
    ble_slave_init();         // Bluetooth protocol stack initialization
    tag_emulation_init();     // Analog card initialization
    rgb_marquee_init();       // Light effect initialization

    settings_load_config();   // Load settings from flash

    // cmd callback register
    on_data_frame_complete(on_data_frame_received);
    
    check_wakeup_src();       // Detect wake-up source and decide BLE broadcast and subsequent hibernation action according to the wake-up source
    tag_mode_enter();         // Enter card simulation mode by default

    // usbd event listener
    APP_ERROR_CHECK(app_usbd_power_events_enable());

    bsp_wdt_init();
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
        // WDT refresh
        bsp_wdt_feed();
        // No task to process, system sleep enter.
        // If system idle sometime, we can enter deep sleep state.
        // Some task process done, we can enter cpu sleep state.
        sleep_system_run(system_off_enter, nrf_pwr_mgmt_run);
    }
}
