#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "lf_reader_main.h"
#include "nrfx_saadc.h"
#include "protocols/ioprox.h"
#include "time.h"
#include "rfid_main.h"

#define NRF_LOG_MODULE_NAME lf_ioprox
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define IOPROX_BUFFER_SIZE (6144)

static circular_buffer cb;

// Generation counters used to disarm the SAADC callback without a mutex.
// g_lf_gen is bumped on each new read. g_lf_active_gen is set to the current
// generation before enabling SAADC; the callback drops samples whose generation
// no longer matches.
static volatile uint32_t g_lf_gen        = 0;
static volatile uint32_t g_lf_active_gen = 0;

static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    // Snapshot the generation counter once to avoid repeated volatile reads
    uint32_t gen = g_lf_active_gen;
    if (gen == 0) return; // Not armed, ignore

    for (size_t i = 0; i < size; i++) {
        // Check if we were disarmed mid-batch
        if (g_lf_active_gen != gen) return;
        uint16_t v = (uint16_t)vals[i];
        cb_push_back(&cb, &v);
    }
}

static void init_ioprox_hw(void) {
    uint16_t dump   = 0;
    uint32_t my_gen = 0;

    // Stop the radio and disable SAADC before restarting (discharges the card)
    stop_lf_125khz_radio();
    lf_125khz_radio_saadc_disable();

    // Guard time: allow the card chip to fully reset (20-50 ms suits most 125 kHz cards)
    bsp_delay_ms(50);

    // Flush any stale samples from the circular buffer
    while (cb_pop_front(&cb, &dump)) {}

    // Arm the SAADC callback before enabling RF so no early samples are missed
    my_gen          = ++g_lf_gen;
    g_lf_active_gen = my_gen;

    // Start the RF field and allow the card power to stabilize
    start_lf_125khz_radio();
    bsp_delay_ms(10);

    // Enable SAADC sampling now that the card has powered up
    lf_125khz_radio_saadc_enable(saadc_cb);
}

static void uninit_ioprox_hw(void) {
    uint16_t dump = 0;

    // Disarm first to stop the SAADC callback from pushing new samples
    g_lf_active_gen = 0;

    // Stop RF field and SAADC
    stop_lf_125khz_radio();
    lf_125khz_radio_saadc_disable();

    // Flush samples that arrived between disarm and SAADC stop
    while (cb_pop_front(&cb, &dump)) {}
}

bool ioprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms) {
    void      *codec = NULL;
    autotimer *p_at  = NULL;
    uint16_t   val   = 0;
    bool       ok    = false;

    codec = ioprox.alloc();
    ioprox.decoder.start(codec, format_hint);

    cb_init(&cb, IOPROX_BUFFER_SIZE, sizeof(uint16_t));
    init_ioprox_hw();

    p_at = bsp_obtain_timer(0);

    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (ioprox.decoder.feed(codec, val)) {
                memcpy(data, ioprox.get_data(codec), ioprox.data_size);
                ok = true;
            }
        }
    }

    bsp_return_timer(p_at);
    uninit_ioprox_hw();
    cb_free(&cb);
    ioprox.free(codec);

    return ok;
}
