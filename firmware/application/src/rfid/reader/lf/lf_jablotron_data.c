#include "lf_reader_data.h"

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/jablotron.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME jablotron_reader
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define JABLOTRON_BUFFER_SIZE (128)

static circular_buffer cb;

static void jablotron_gpio_int0_cb(void) {
    uint32_t cntr = get_lf_counter_value();
    uint16_t val = 0;
    if (cntr > 0xff) {
        val = 0xff;
    } else {
        val = cntr & 0xff;
    }
    cb_push_back(&cb, &val);
    clear_lf_counter_value();
}

static void init_jablotron_hw(void) {
    register_rio_callback(jablotron_gpio_int0_cb);
    lf_125khz_radio_gpiote_enable();
}

static void uninit_jablotron_hw(void) {
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
}

bool jablotron_read(uint8_t *data, uint32_t timeout_ms) {
    void *codec = jablotron.alloc();
    jablotron.decoder.start(codec, 0);

    cb_init(&cb, JABLOTRON_BUFFER_SIZE, sizeof(uint16_t));
    init_jablotron_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (jablotron.decoder.feed(codec, val)) {
                memcpy(data, jablotron.get_data(codec), jablotron.data_size);
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_jablotron_hw();
    cb_free(&cb);

    jablotron.free(codec);
    return ok;
}
