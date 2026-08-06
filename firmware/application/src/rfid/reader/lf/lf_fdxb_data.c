#include "lf_reader_data.h"

#include <stdlib.h>
#include <string.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_main.h"
#include "protocols/fdxb.h"
#include "protocols/t55xx.h"
#include "protocols/protocols.h"

#define NRF_LOG_MODULE_NAME fdxb
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define FDXB_BUFFER_SIZE (256)

static circular_buffer cb;

/* Same falling-edge capture as EM410x: push the carrier-cycle count
 * between edges and reset the counter. */
static void fdxb_gpio_int0_cb(void) {
    uint32_t cntr = get_lf_counter_value();
    uint16_t val = (cntr > 0xff) ? 0xff : (cntr & 0xff);
    cb_push_back(&cb, &val);
    clear_lf_counter_value();
}

static void init_fdxb_hw(void) {
    register_rio_callback(fdxb_gpio_int0_cb);
    lf_125khz_radio_gpiote_enable();
}

static void uninit_fdxb_hw(void) {
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
}

bool fdxb_read(uint8_t *data, uint32_t timeout_ms) {
    void **codecs = malloc(fdxb_protocols_size * sizeof(void *));
    for (size_t i = 0; i < fdxb_protocols_size; i++) {
        codecs[i] = fdxb_protocols[i]->alloc();
        fdxb_protocols[i]->decoder.start(codecs[i], 0);
    }

    cb_init(&cb, FDXB_BUFFER_SIZE, sizeof(uint16_t));

    /* Retune the carrier before enabling capture.  Must happen while the
     * PWM is stopped; lf_radio_set_carrier() re-inits the instance, and the
     * PPI event addresses are register addresses of the same instance so
     * they stay valid across the re-init. */
    lf_radio_set_carrier(LF_CARRIER_134KHZ);

    init_fdxb_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            for (size_t i = 0; i < fdxb_protocols_size; i++) {
                const protocol *p = fdxb_protocols[i];
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
    uninit_fdxb_hw();

    /* Always restore 125 kHz so every other LF command is unaffected. */
    lf_radio_set_carrier(LF_CARRIER_125KHZ);

    cb_free(&cb);

    for (size_t i = 0; i < fdxb_protocols_size; i++) {
        fdxb_protocols[i]->free(codecs[i]);
    }
    free(codecs);
    return ok;
}

uint8_t write_fdxb_to_t55xx(uint8_t *fdxb_data) {
    /**
     * Write FDX-B frame data to T55xx chip.
     * 
     * @param fdxb_data: 13-byte FDX-B destuffed frame from scan
     *   Bytes 0-7:   National ID (38 bits) + Country code (10 bits) + 
     *                App bit (1 bit) + Reserved (14 bits) - all LSB-first
     *   Bytes 8-9:   CRC-16/KERMIT
     *   Bytes 10-12: Trailer / application data
     *
     * @return: Status code (STATUS_LF_TAG_OK on success)
     *
     * Strategy: Use fdxb_t55xx_writer() to encode frame into T55xx block format,
     * then write with standard T55xx infrastructure using Diphase encoding at RF/32.
     */
    
    uint32_t blks[3] = {0x00};
    uint8_t blk_count = fdxb_t55xx_writer(fdxb_data, blks);
    if (blk_count == 0) {
        return STATUS_PAR_ERR;
    }
    
    // Default password for virgin T55xx (0x00000000)
    uint8_t new_passwd[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t old_passwd[4] = {0x00, 0x00, 0x00, 0x00};
    
    // Use write_t55xx with proper block configuration
    return write_t55xx(blks, blk_count, new_passwd, old_passwd, 1);
}
