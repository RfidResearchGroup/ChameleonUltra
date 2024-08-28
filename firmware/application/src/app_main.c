#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

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
#include "nrf_ble_lesc.h"

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
#include "tag_persistence.h"
#include "settings.h"

// Defining soft timers
APP_TIMER_DEF(m_button_check_timer); // Timer for button debounce

static uint32_t m_last_btn_press = 0;

static bool m_is_btn_long_press = false;

static bool m_is_b_btn_press = false;
static bool m_is_a_btn_press = false;

static bool m_is_b_btn_release = false;
static bool m_is_a_btn_release = false;

static bool m_system_off_processing = false;

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
void rng_drv_and_srand_init(void) {
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
    // if button press during shutdown, it's only to wake up quickly
    if (m_system_off_processing) {
        m_system_off_processing = false;
        NRF_LOG_INFO("BUTTON press during shutdown");
        return;
    }
    nrf_drv_gpiote_pin_t pin = *(nrf_drv_gpiote_pin_t *)arg;
    // Check here if the current GPIO is at the pressed level
    if (nrf_gpio_pin_read(pin) == 1) {
        if (pin == BUTTON_1) {
            // If button is disabled, we can't dispatch key event.
            if (settings_get_button_press_config('b') != SettingsButtonDisable) {
                NRF_LOG_INFO("BUTTON_B_PRESS");
                m_is_b_btn_press = true;
                m_last_btn_press = app_timer_cnt_get();
            }
        }
        if (pin == BUTTON_2) {
            if (settings_get_button_press_config('a') != SettingsButtonDisable) {
                NRF_LOG_INFO("BUTTON_A_PRESS");
                m_is_a_btn_press = true;
                m_last_btn_press = app_timer_cnt_get();
            }
        }
    }

    if (nrf_gpio_pin_read(pin) == 0) {
        uint32_t now = app_timer_cnt_get();
        uint32_t ticks = app_timer_cnt_diff_compute(now, m_last_btn_press);

        bool is_long_press = ticks > APP_TIMER_TICKS(1000);

        if (pin == BUTTON_1 && m_is_b_btn_press == true) {
            // If button is disabled, we can't dispatch key event.
            if (settings_get_button_press_config('b') != SettingsButtonDisable) {
                m_is_b_btn_release = true;
                m_is_b_btn_press = false;
                if (!is_long_press) {
                    NRF_LOG_INFO("BUTTON_B_RELEASE_SHORT");
                } else {
                    NRF_LOG_INFO("BUTTON_B_RELEASE_LONG");
                }
                m_is_btn_long_press = is_long_press;
            }
        }
        if (pin == BUTTON_2 && m_is_a_btn_press == true) {
            if (settings_get_button_press_config('a') != SettingsButtonDisable) {
                m_is_a_btn_release = true;
                m_is_a_btn_press = false;
                if (!is_long_press) {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_SHORT");
                } else {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_LONG");
                }
                m_is_btn_long_press = is_long_press;
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
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
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
    m_system_off_processing = true;
    // Save tag data
    tag_emulation_save();

    if (g_is_low_battery_shutdown) {
        // Don't create too complex animations, just blink LED1 three times.
        rgb_marquee_stop();
        set_slot_light_color(RGB_RED);
        for (uint8_t i = 0; i <= 3; i++) {
            nrf_gpio_pin_set(LED_1);
            bsp_delay_ms(100);
            nrf_gpio_pin_clear(LED_1);
            bsp_delay_ms(100);
        }
    } else {
        // close all led.
        uint32_t *p_led_array = hw_get_led_array();
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
            if (m_system_off_processing) ledblink5(color, slot, dir ? 7 : 0);
            if (m_system_off_processing) ledblink4(color, dir, 7, 99, 75);
            if (m_system_off_processing) ledblink4(color, !dir, 7, 75, 50);
            if (m_system_off_processing) ledblink4(color, dir, 7, 50, 25);
            if (m_system_off_processing) ledblink4(color, !dir, 7, 25, 0);
        }
        rgb_marquee_stop();
        if (!m_system_off_processing) {
            for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
                nrf_gpio_pin_clear(p_led_array[i]);
            }
            light_up_by_slot();
            sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
            return;
        }
    }

    // Disable the HF NFC event first
    NRF_NFCT->INTENCLR = NRF_NFCT_DISABLE_ALL_INT;
    // Then disable the LF LPCOMP event
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;

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

    // IOs that need to be configured as floating analog inputs ==> no pull-up or pull-down
    uint32_t gpio_cfg_default_no_pull[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_SPI_SELECT,
        HF_SPI_MISO,
        HF_SPI_MOSI,
        HF_SPI_MOSI,
        LF_OA_OUT,
#endif
        BAT_SENSE_PIN,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_default_no_pull); i++) {
        nrf_gpio_cfg_default(gpio_cfg_default_no_pull[i]);
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

    // Check whether there are low -frequency fields, solving very strong field signals during dormancy have always caused the comparator to be at a high level input state, so that the problem of uprising the rising edge cannot be awakened.
    if (lf_is_field_exists()) {
        // Close the comparator
        nrf_drv_lpcomp_disable();
        // Set the reason for Reset. After restarting, you need to get this reason to avoid misjudgment from the source of wake up.
        sd_power_gpregret_clr(1, GPREGRET_CLEAR_VALUE_DEFAULT);
        sd_power_gpregret_set(1, RESET_ON_LF_FIELD_EXISTS_Msk);
        // Trigger the RESET awakening system, restart the simulation process
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
        return;
    };

    // Last call, gate is closing
    NRF_LOG_FLUSH();

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    // Note that if you insert jlink or drive a Debug, you may report an error when entering the low power consumption.
    // When starting debugging, we should disable low power consumption state values, or simply not enter low power consumption
    ret = sd_power_system_off();

    // OK, here is very important. If you open the log output and enable RTT, you will not check the error of the low power mode
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
        advertising_start(false); // Turn on Bluetooth radio

        // Button wake-up boot animation
        uint8_t animation_config = settings_get_animation_config();
        if (animation_config == SettingsAnimationModeFull) {
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

        // It is currently the wake -up system of the simulation card event, we can make the strong lights on the field first
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
        advertising_start(false);
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
            advertising_start(false);
        } else {
            sleep_timer_start(SLEEP_DELAY_MS_FIRST_POWER); // Wait a while and go straight to hibernation, do nothing
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

static void show_battery(void) {
    rgb_marquee_stop();
    uint32_t *led_pins = hw_get_led_array();
    // if still in the first 4s after boot, blink red while waiting for battery info
    while (percentage_batt_lvl == 0) {
        for (int i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_clear(led_pins[i]);
        }
        bsp_delay_ms(100);
        set_slot_light_color(RGB_RED);
        for (int i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_set(led_pins[i]);
        }
        bsp_delay_ms(100);
    }
    // ok we have data, show level with cyan LEDs
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(led_pins[i]);
    }
    set_slot_light_color(RGB_CYAN);
    uint8_t nleds = (percentage_batt_lvl * 2) / 25; // 0->7 (8 for 100% but this is ignored)
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        if (i <= nleds) {
            nrf_gpio_pin_set(led_pins[i]);
            bsp_delay_ms(50);
        }
    }
    // nothing special to finish, we wait for sleep or slot change
}

#if defined(PROJECT_CHAMELEON_ULTRA)

static void offline_status_blink_color(uint8_t blink_color) {
    uint8_t slot = tag_emulation_get_slot();

    uint8_t color = get_color_by_slot(slot);

    uint32_t *p_led_array = hw_get_led_array();

    set_slot_light_color(blink_color);

    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        if (i == slot) {
            continue;
        }
        nrf_gpio_pin_set(p_led_array[i]);
        bsp_delay_ms(10);
        nrf_gpio_pin_clear(p_led_array[i]);
        bsp_delay_ms(10);
    }

    set_slot_light_color(color);
}

static void offline_status_error(void) {
    offline_status_blink_color(0);
}

static void offline_status_ok(void) {
    offline_status_blink_color(1);
}

// fast detect a 14a tag uid to sim
static void btn_fn_copy_ic_uid(void) {
    bool lf_copy_succeeded = false;
    bool hf_copy_succeeded = false;
    uint8_t status;
    uint8_t id_buffer[5] = { 0x00 };
    // get 14a tag res buffer;
    uint8_t slot_now = tag_emulation_get_slot();
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(slot_now, &tag_types);

    nfc_tag_14a_coll_res_entity_t *antres = NULL;

    bool is_reader_mode_now = get_device_mode() == DEVICE_MODE_READER;
    // first, we need switch to reader mode.
    if (!is_reader_mode_now) {
        // enter reader mode
        reader_mode_enter();
        bsp_delay_ms(8);
        NRF_LOG_INFO("Start reader mode to offline copy.")
    }

    switch (tag_types.tag_lf) {
        case TAG_TYPE_EM410X:
            status = PcdScanEM410X(id_buffer);

            if (status == STATUS_LF_TAG_OK) {
                tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_EM410X);
                memcpy(buffer->buffer, id_buffer, LF_EM410X_TAG_ID_SIZE);
                tag_emulation_load_by_buffer(TAG_TYPE_EM410X, false);
                NRF_LOG_INFO("Offline LF uid copied")
                lf_copy_succeeded = true;
                offline_status_ok();
            } else {
                NRF_LOG_INFO("No LF tag found");
                offline_status_error();
            }
            break;
        case TAG_TYPE_UNDEFINED:
            // empty LF slot, nothing to do, move on to HF
            break;
        default:
            NRF_LOG_ERROR("Unsupported LF tag type")
            offline_status_error();
    }

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_types.tag_hf);
    switch (tag_types.tag_hf) {
        case TAG_TYPE_MIFARE_Mini:
        case TAG_TYPE_MIFARE_1024:
        case TAG_TYPE_MIFARE_2048:
        case TAG_TYPE_MIFARE_4096: {
            nfc_tag_mf1_information_t *p_info = (nfc_tag_mf1_information_t *)buffer->buffer;
            antres = &(p_info->res_coll);
            break;
        }

        case TAG_TYPE_NTAG_210:
        case TAG_TYPE_NTAG_212:
        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216:
        case TAG_TYPE_MF0ICU1:
        case TAG_TYPE_MF0ICU2:
        case TAG_TYPE_MF0UL11:
        case TAG_TYPE_MF0UL21: {
            nfc_tag_mf0_ntag_information_t *p_info = (nfc_tag_mf0_ntag_information_t *)buffer->buffer;
            antres = &(p_info->res_coll);
            break;
        }

        case TAG_TYPE_UNDEFINED:
            // empty HF slot, nothing to do
            break;

        default:
            NRF_LOG_ERROR("Unsupported HF tag type")
            offline_status_error();
            break;
    }
    if (antres != NULL) {
        if (!is_reader_mode_now) {
            // finish HF reader initialization
            pcd_14a_reader_reset();
        }
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
        // select a tag
        picc_14a_tag_t tag;

        status = pcd_14a_reader_scan_auto(&tag);
        pcd_14a_reader_antenna_off();
        if (status == STATUS_HF_TAG_OK) {
            // copy uid
            antres->size = tag.uid_len;
            memcpy(antres->uid, tag.uid, tag.uid_len);
            // copy atqa
            memcpy(antres->atqa, tag.atqa, 2);
            // copy sak
            antres->sak[0] = tag.sak;
            // copy ats
            antres->ats.length = tag.ats_len;
            memcpy(antres->ats.data, tag.ats, tag.ats_len);
            NRF_LOG_INFO("Offline HF uid copied")
            hf_copy_succeeded = true;
            offline_status_ok();
        } else {
            NRF_LOG_INFO("No HF tag found");
            offline_status_error();
        }
    }
    if (lf_copy_succeeded || hf_copy_succeeded) {
        fds_slot_record_map_t map_info;
        char *nick = "cloned";
        uint8_t buffer[36];
        buffer[0] = strlen(nick);
        memcpy(buffer + 1, nick, buffer[0]);
        if (lf_copy_succeeded) {
            get_fds_map_by_slot_sense_type_for_nick(slot_now, TAG_SENSE_LF, &map_info);
            fds_write_sync(map_info.id, map_info.key, sizeof(buffer), buffer);
        }
        if (hf_copy_succeeded) {
            get_fds_map_by_slot_sense_type_for_nick(slot_now, TAG_SENSE_HF, &map_info);
            fds_write_sync(map_info.id, map_info.key, sizeof(buffer), buffer);
        }
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
    switch (sbf) {
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

        case SettingsButtonShowBattery:
            show_battery();

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
    if (m_is_b_btn_release || m_is_a_btn_release) {
        if (m_is_a_btn_release) {
            if (!m_is_btn_long_press) {
                run_button_function_by_settings(settings_get_button_press_config('a'));
            } else {
                run_button_function_by_settings(settings_get_long_button_press_config('a'));
            }
            m_is_a_btn_release = false;
        }
        if (m_is_b_btn_release) {
            if (!m_is_btn_long_press) {
                run_button_function_by_settings(settings_get_button_press_config('b'));
            } else {
                run_button_function_by_settings(settings_get_long_button_press_config('b'));
            }
            m_is_b_btn_release = false;
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

static void lesc_event_process(void) {
    if (settings_get_ble_pairing_enable_first_load()) {
        ret_code_t err_code;
        err_code = nrf_ble_lesc_request_handler();
        APP_ERROR_CHECK(err_code);
    }
}

static void ble_passkey_init(void) {
    if (settings_get_ble_pairing_enable_first_load()) {
        set_ble_connect_key(settings_get_ble_connect_key());
    }
}

/**@brief Application main function.
 */
int main(void) {
    hw_connect_init();        // Remember to initialize the pins first

    fds_util_init();          // Initialize fds tool
    settings_load_config();   // Load settings from flash

    init_leds();              // LED initialization
    log_init();               // Log initialization
    gpio_te_init();           // Initialize GPIO matrix library
    app_timers_init();        // Initialize soft timer
    power_management_init();  // Power management initialization
    usb_cdc_init();           // USB cdc emulation initialization
    ble_slave_init();         // Bluetooth protocol stack initialization

    rng_drv_and_srand_init(); // Random number generator initialization
    bsp_timer_init();         // Initialize timeout timer
    bsp_timer_start();        // Start BSP TIMER and prepare it for processing business logic
    button_init();            // Button initialization for handling business logic
    sleep_timer_init();       // Soft timer initialization for hibernation
    tag_emulation_init();     // Analog card initialization
    rgb_marquee_init();       // Light effect initialization

    ble_passkey_init();       // init ble connect key.

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
        // process lesc event
        lesc_event_process();
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
