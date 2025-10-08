#include "lf_reader_data.h"

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME lfgeneric
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define CIRCULAR_BUFFER_SIZE (128)
static circular_buffer cb;

// saadc irq is used to sample ANT GPIO.
static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (int i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

static void init_saadc_hw(void) {
    lf_125khz_radio_saadc_enable(saadc_cb);
}

static void uninit_saadc_hw(void) {
    lf_125khz_radio_saadc_disable();
}

bool raw_read_to_buffer(uint8_t *data, size_t maxlen, uint32_t timeout_ms, size_t *outlen) {
    *outlen = 0;

    cb_init(&cb, CIRCULAR_BUFFER_SIZE, sizeof(uint16_t));
    init_saadc_hw();
    start_lf_125khz_radio();

    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, timeout_ms) && *outlen < maxlen) {
        uint16_t val = 0;
        while (cb_pop_front(&cb, &val) && *outlen < maxlen) {
            val = val >> 6; // 14 bit ADC to 8 bit value
            data[*outlen] = val > 0xff ? 0xff : val;
            ++(*outlen);
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_saadc_hw();
    cb_free(&cb);

    return true;
}
