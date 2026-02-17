#include <string.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "nrfx_saadc.h"
#include "protocols/pac.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME pac_reader
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define PAC_BUFFER_SIZE (6144)

static circular_buffer cb;

// SAADC callback — push raw ADC samples to circular buffer.
// NRZ/Direct modulation requires ADC sampling (not GPIOTE edge timing)
// because the comparator may not produce clean digital edges for NRZ signals.
static void pac_saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (size_t i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

static void init_pac_hw(void) {
    lf_125khz_radio_saadc_enable(pac_saadc_cb);
}

static void uninit_pac_hw(void) {
    lf_125khz_radio_saadc_disable();
}

bool pac_read(uint8_t *data, uint32_t timeout_ms) {
    void *codec = pac.alloc();
    pac.decoder.start(codec, 0);

    // Start carrier first, then wait for T55XX POR (~5ms) before
    // enabling SAADC.  This ensures the prescan calibration phase
    // sees real NRZ signal levels rather than power-up noise.
    start_lf_125khz_radio();
    bsp_delay_ms(10);

    cb_init(&cb, PAC_BUFFER_SIZE, sizeof(uint16_t));
    init_pac_hw();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (pac.decoder.feed(codec, val)) {
                memcpy(data, pac.get_data(codec), pac.data_size);
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_pac_hw();
    cb_free(&cb);

    pac.free(codec);
    return ok;
}
