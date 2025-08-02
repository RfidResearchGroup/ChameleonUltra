#include "lf_hidprox_data.h"

#include <stdio.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "lf_reader_main.h"
#include "protocols/hidprox.h"
#include "time.h"

#define NRF_LOG_MODULE_NAME lf_read
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define HIDPROX_BUFFER_SIZE (6144)

static circular_buffer cb;

// saadc irq is used to sample ANT GPIO level.
void saadc_cb(int16_t *vals, size_t size) {
    for (int i = 0; i < size; i++) {
        uint16_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

void init_hidprox_hw(void) {
    register_saadc_callback(saadc_cb);
    lf_125khz_radio_saadc_init();
}

void uninit_hidprox_hw(void) {
    unregister_saadc_callback();
}

bool hidprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms) {
    void *codec = hidprox.alloc();
    hidprox.decoder.start(codec, format_hint);

    cb_init(&cb, HIDPROX_BUFFER_SIZE, sizeof(uint16_t));
    init_hidprox_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (hidprox.decoder.feed(codec, val)) {
                memcpy(data, hidprox.get_data(codec), hidprox.data_size);
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_hidprox_hw();
    cb_free(&cb);

    hidprox.free(codec);
    return ok;
}
