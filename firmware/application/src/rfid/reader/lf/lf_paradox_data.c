#include <string.h>

#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "nrfx_saadc.h"
#include "protocols/paradox.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME lf_paradox
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define PARADOX_BUFFER_SIZE (6144)

static circular_buffer cb;

static void paradox_saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (size_t i = 0; i < size; i++) {
        uint16_t val = (uint16_t)vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

static void init_paradox_hw(void) {
    lf_125khz_radio_saadc_enable(paradox_saadc_cb);
}

static void uninit_paradox_hw(void) {
    lf_125khz_radio_saadc_disable();
}

bool paradox_read(uint8_t *data, uint32_t timeout_ms) {
    void *codec = paradox.alloc();
    if (codec == NULL) {
        return false;
    }
    paradox.decoder.start(codec, 0);

    cb_init(&cb, PARADOX_BUFFER_SIZE, sizeof(uint16_t));
    init_paradox_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *timer = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(timer, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(timer, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (paradox.decoder.feed(codec, val)) {
                memcpy(data, paradox.get_data(codec), paradox.data_size);
                ok = true;
            }
        }
    }

    bsp_return_timer(timer);
    stop_lf_125khz_radio();
    uninit_paradox_hw();
    cb_free(&cb);
    paradox.free(codec);
    return ok;
}
