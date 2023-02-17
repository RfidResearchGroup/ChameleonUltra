#include <nrf_gpio.h>
#include "hw_connect.h"


// Current project run on ultra or lite.
const static chameleon_device_type_t m_device_type =
#if defined(PROJECT_CHAMELEON_ULTRA)
    CHAMELEON_ULTRA;
#elif defined(PROJECT_CHAMELEON_LITE)
    CHAMELEON_LITE;
#else
    "No device define before project build.";
#endif


uint32_t g_led_field;
uint32_t g_led_1;
uint32_t g_led_2;
uint32_t g_led_3;
uint32_t g_led_4;
uint32_t g_led_5;
uint32_t g_led_6;
uint32_t g_led_7;
uint32_t g_led_8;
uint32_t g_led_r;
uint32_t g_led_g;
uint32_t g_led_b;
uint32_t g_led_num;
uint32_t g_rgb_num;
uint32_t g_button1;
uint32_t g_button2;
uint32_t g_lf_mod;
uint32_t g_lf_rssi_pin;
nrf_lpcomp_input_t g_lf_rssi;
uint32_t g_bat_sense;

#if defined(PROJECT_CHAMELEON_ULTRA)
uint32_t g_lf_ant_driver;
uint32_t g_lf_oa_out;
uint32_t g_hf_spi_select;
uint32_t g_hf_spi_miso;
uint32_t g_hf_spi_mosi;
uint32_t g_hf_spi_sck;
uint32_t g_hf_ant_sel;
uint32_t g_reader_power;
#endif


uint32_t m_led_array[MAX_LED_NUM];
#define INIT_LED_ARRAY(num, led)     \
    if (RGB_LIST_NUM >= num) {       \
        m_led_array[num - 1] = led;  \
    }                                \


uint32_t m_rgb_array[MAX_RGB_NUM];
#define INIT_RGB_ARRAY(num, rgb)     \
    if (RGB_CTRL_NUM >= num) {       \
        m_rgb_array[num - 1] = rgb;  \
    }                                \


static uint8_t m_hw_ver;


void hw_connect_init(void) {
    // TODO 请实现此处，实现硬件版本号的读取
    // 测试的时候可以直接改写此版本号
    m_hw_ver = 1;

    
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (m_hw_ver == 1) {
        LED_FIELD    = (NRF_GPIO_PIN_MAP(1, 1));
        LED_R        = (NRF_GPIO_PIN_MAP(0, 24));
        LED_G        = (NRF_GPIO_PIN_MAP(0, 22));
        LED_B        = (NRF_GPIO_PIN_MAP(1, 0));
        LED_8        = (NRF_GPIO_PIN_MAP(0, 20));
        LED_7        = (NRF_GPIO_PIN_MAP(0, 17));
        LED_6        = (NRF_GPIO_PIN_MAP(0, 15));
        LED_5        = (NRF_GPIO_PIN_MAP(0, 13));
        LED_4        = (NRF_GPIO_PIN_MAP(0, 12));
        LED_3        = (NRF_GPIO_PIN_MAP(1, 9));
        LED_2        = (NRF_GPIO_PIN_MAP(0, 8));
        LED_1        = (NRF_GPIO_PIN_MAP(0, 6));
        RGB_LIST_NUM = 8;
        RGB_CTRL_NUM = 3;

        LF_ANT_DRIVER = (NRF_GPIO_PIN_MAP(0, 31));
        LF_OA_OUT     = (NRF_GPIO_PIN_MAP(1, 15));
        LF_MOD        = (NRF_GPIO_PIN_MAP(1, 13));
        LF_RSSI_PIN   = (NRF_GPIO_PIN_MAP(0, 2));
        LF_RSSI       = NRF_LPCOMP_INPUT_0;

        HF_SPI_SELECT = (NRF_GPIO_PIN_MAP(1, 6));
        HF_SPI_MISO   = (NRF_GPIO_PIN_MAP(0, 11));
        HF_SPI_MOSI   = (NRF_GPIO_PIN_MAP(1, 7));
        HF_SPI_SCK    = (NRF_GPIO_PIN_MAP(1, 4));
        HF_ANT_SEL    = (NRF_GPIO_PIN_MAP(1, 10));

        BUTTON_1      = (NRF_GPIO_PIN_MAP(0, 26));
        BUTTON_2      = (NRF_GPIO_PIN_MAP(1, 2));

        BAT_SENSE     = (NRF_GPIO_PIN_MAP(0, 4));
        READER_POWER  = (NRF_GPIO_PIN_MAP(0, 29));
    }
    if (m_hw_ver == 2) {
        LED_FIELD    =   (NRF_GPIO_PIN_MAP(1, 1));
        LED_R        =   (NRF_GPIO_PIN_MAP(0, 24));
        LED_G        =   (NRF_GPIO_PIN_MAP(0, 22));
        LED_B        =   (NRF_GPIO_PIN_MAP(1, 0));
        LED_8        =   (NRF_GPIO_PIN_MAP(0, 20));
        LED_7        =   (NRF_GPIO_PIN_MAP(0, 17));
        LED_6        =   (NRF_GPIO_PIN_MAP(0, 15));
        LED_5        =   (NRF_GPIO_PIN_MAP(0, 13));
        LED_4        =   (NRF_GPIO_PIN_MAP(0, 12));
        LED_3        =   (NRF_GPIO_PIN_MAP(1, 9));
        LED_2        =   (NRF_GPIO_PIN_MAP(0, 8));
        LED_1        =   (NRF_GPIO_PIN_MAP(0, 6));
        RGB_LIST_NUM = 8;
        RGB_CTRL_NUM = 3;

        LF_ANT_DRIVER = (NRF_GPIO_PIN_MAP(0, 31));
        LF_OA_OUT     = (NRF_GPIO_PIN_MAP(0, 29));
        LF_MOD        = (NRF_GPIO_PIN_MAP(1, 13));
        LF_RSSI_PIN   = (NRF_GPIO_PIN_MAP(0, 2));
        LF_RSSI       = NRF_LPCOMP_INPUT_0;

        HF_SPI_SELECT = (NRF_GPIO_PIN_MAP(1, 6));
        HF_SPI_MISO   = (NRF_GPIO_PIN_MAP(0, 11));
        HF_SPI_MOSI   = (NRF_GPIO_PIN_MAP(1, 7));
        HF_SPI_SCK    = (NRF_GPIO_PIN_MAP(1, 4));
        HF_ANT_SEL    = (NRF_GPIO_PIN_MAP(1, 10));

        BUTTON_1      = (NRF_GPIO_PIN_MAP(0, 26));
        BUTTON_2      = (NRF_GPIO_PIN_MAP(1, 2));

        BAT_SENSE     = (NRF_GPIO_PIN_MAP(0, 4));
        READER_POWER  = (NRF_GPIO_PIN_MAP(1, 15));
    }
#endif

#if defined(PROJECT_CHAMELEON_LITE)
    if (m_hw_ver == 1) {
        LED_FIELD      = (NRF_GPIO_PIN_MAP(1, 1));
        LED_1          = (NRF_GPIO_PIN_MAP(0, 22));
        LED_2          = (NRF_GPIO_PIN_MAP(0, 20));
        LED_3          = (NRF_GPIO_PIN_MAP(0, 17));
        LED_4          = (NRF_GPIO_PIN_MAP(0, 15));
        LED_5          = (NRF_GPIO_PIN_MAP(0, 13));
        LED_6          = (NRF_GPIO_PIN_MAP(0, 6));
        LED_7          = (NRF_GPIO_PIN_MAP(0, 4));
        LED_8          = (NRF_GPIO_PIN_MAP(0, 26));
        LED_R          = (NRF_GPIO_PIN_MAP(0, 8));
        LED_G          = (NRF_GPIO_PIN_MAP(0, 12));
        LED_B          = (NRF_GPIO_PIN_MAP(1, 9));
        RGB_LIST_NUM   = 8;
        RGB_CTRL_NUM   = 3;
        
        BUTTON_1       = (NRF_GPIO_PIN_MAP(1, 2));
        BUTTON_2       = (NRF_GPIO_PIN_MAP(1, 6));

        LF_MOD         = (NRF_GPIO_PIN_MAP(1, 4));
        LF_RSSI_PIN    = (NRF_GPIO_PIN_MAP(0, 2));
        LF_RSSI        = NRF_LPCOMP_INPUT_0;
        BAT_SENSE      = (NRF_GPIO_PIN_MAP(0, 29));
    }
#endif


    INIT_LED_ARRAY(1, LED_1);
    INIT_LED_ARRAY(2, LED_2);
    INIT_LED_ARRAY(3, LED_3);
    INIT_LED_ARRAY(4, LED_4);
    INIT_LED_ARRAY(5, LED_5);
    INIT_LED_ARRAY(6, LED_6);
    INIT_LED_ARRAY(7, LED_7);
    INIT_LED_ARRAY(8, LED_8);
    
    INIT_RGB_ARRAY(1, LED_R);
    INIT_RGB_ARRAY(2, LED_G);
    INIT_RGB_ARRAY(3, LED_B);
}

uint32_t* hw_get_led_array(void) {
    return m_led_array;
}

uint32_t* hw_get_rgb_array(void) {
    return m_rgb_array;
}

chameleon_device_type_t hw_get_device_type(void) {
    return m_device_type;
}

uint8_t hw_get_version_code(void) {
    return m_hw_ver;
}
