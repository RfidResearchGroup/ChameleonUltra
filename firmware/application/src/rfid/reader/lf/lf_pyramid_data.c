#include <stdio.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "lf_reader_main.h"
#include "nrfx_saadc.h"
#include "protocols/pyramid.h"
#include "time.h"
#include "rfid_main.h"


#define PYRAMID_BUFFER_SIZE (6144)

static circular_buffer cb;

static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (int i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

static void init_pyramid_hw(void) {
    lf_125khz_radio_saadc_enable(saadc_cb);
}

static void uninit_pyramid_hw(void) {
    lf_125khz_radio_saadc_disable();
}

bool pyramid_read(uint8_t *data, uint32_t timeout_ms) {
    void *codec = pyramid.alloc();
    pyramid.decoder.start(codec, 0);

    cb_init(&cb, PYRAMID_BUFFER_SIZE, sizeof(uint16_t));
    init_pyramid_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (pyramid.decoder.feed(codec, val)) {
                memcpy(data, pyramid.get_data(codec), pyramid.data_size);
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_pyramid_hw();
    cb_free(&cb);

    pyramid.free(codec);
    return ok;
}
