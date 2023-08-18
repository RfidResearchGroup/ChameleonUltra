#include "nrf_drv_wdt.h"
#include "hw_connect.h"
#include "nrf_gpio.h"

static nrf_drv_wdt_channel_id m_channel_id;

static void wdt_event_handler(void)
{
    //NOTE: The max amount of time we can spend in WDT interrupt is two cycles of 32768[Hz] clock - after that, reset occurs
    uint32_t* p_led_array = hw_get_led_array();
    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(p_led_array[i]);
    }
}

void bsp_wdt_init(void) {
    ret_code_t err_code;
//    err_code = nrf_drv_clock_init();    // already done by usb_cdc_init() -> app_usbd_init()
//    APP_ERROR_CHECK(err_code);
    nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG; // typo is in the SDK...
    err_code = nrf_drv_wdt_init(&config, wdt_event_handler);
    APP_ERROR_CHECK(err_code);
    err_code = nrf_drv_wdt_channel_alloc(&m_channel_id);
    APP_ERROR_CHECK(err_code);
    nrf_drv_wdt_enable();
}

void bsp_wdt_feed(void) {
    nrf_drv_wdt_channel_feed(m_channel_id);
}
