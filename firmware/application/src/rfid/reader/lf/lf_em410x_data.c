#include "lf_reader_data.h"

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/em410x.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define EM410X_BUFFER_SIZE (128)

static circular_buffer cb;

// GPIO interrupt recovery function is used to detect the descending edge
void gpio_int0_cb(void) {
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

static void init_em410x_hw(void) {
    register_rio_callback(gpio_int0_cb);
    lf_125khz_radio_gpiote_enable();
}

static void uninit_em410x_hw(void) {
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
}

bool em410x_read(uint8_t *data, uint32_t timeout_ms) {
    void **codecs = malloc(em410x_protocols_size * sizeof(void *));
    for (size_t i = 0; i < em410x_protocols_size; i++) {
        codecs[i] = em410x_protocols[i]->alloc();
        em410x_protocols[i]->decoder.start(codecs[i], 0);
    }

    cb_init(&cb, EM410X_BUFFER_SIZE, sizeof(uint16_t));
    init_em410x_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            for (int i = 0; i < em410x_protocols_size; i++) {
                const protocol *p = em410x_protocols[i];
                if (!p->decoder.feed(codecs[i], val)) {
                    continue;
                }
                data[0] = p->tag_type >> 8;
                data[1] = p->tag_type;
                memcpy(data + 2, p->get_data(codecs[i]), p->data_size);
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_em410x_hw();
    cb_free(&cb);

    for (size_t i = 0; i < em410x_protocols_size; i++) {
        em410x_protocols[i]->free(codecs[i]);
    }
    free(codecs);
    return ok;
}
