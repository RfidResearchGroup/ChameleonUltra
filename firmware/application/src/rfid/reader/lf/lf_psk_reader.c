#include <string.h>

#include "bsp_delay.h"
#include "bsp_wdt.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_main.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME lf_psk_reader
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define PSK_READER_BUFFER_SIZE (4096)

static circular_buffer cb;

static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (size_t i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

bool psk_generic_read(const protocol *p, uint8_t *data, uint32_t timeout_ms) {
    void *codec = p->alloc();
    if (!codec) {
        NRF_LOG_ERROR("PSK: alloc failed");
        return false;
    }
    p->decoder.start(codec, 0);

    start_lf_125khz_radio();
    bsp_delay_ms(10);  // T55XX POR: let tag power up before sampling
    if (!cb_init(&cb, PSK_READER_BUFFER_SIZE, sizeof(uint16_t))) {
        NRF_LOG_ERROR("PSK: cb_init failed (heap exhaustion)");
        stop_lf_125khz_radio();
        p->free(codec);
        return false;
    }
    lf_125khz_radio_saadc_enable(saadc_cb);

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (p->decoder.feed(codec, val)) {
                memcpy(data, p->get_data(codec), p->data_size);
                ok = true;
                break;
            }
        }
        bsp_wdt_feed();
    }

    NRF_LOG_INFO("PSK: exit ok=%d", ok);

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    lf_125khz_radio_saadc_disable();
    cb_free(&cb);

    p->free(codec);
    return ok;
}
