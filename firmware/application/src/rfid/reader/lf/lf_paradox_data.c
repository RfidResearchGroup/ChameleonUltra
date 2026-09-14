#include <string.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "bsp_wdt.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/paradox.h"
#include "utils/fskdemod.h"

#define PARADOX_BUFFER_SIZE 6144
#define PARADOX_SAMPLE_BUFFER_SIZE 10000

static circular_buffer cb;
static volatile bool sampling;
static uint16_t sample_buffer[PARADOX_SAMPLE_BUFFER_SIZE];
static uint8_t last_demod[29];
static size_t last_demod_length;

static void saadc_cb(nrf_saadc_value_t *values, size_t size) {
    if (!sampling) {
        return;
    }
    for (size_t i = 0; i < size && sampling; i++) {
        uint16_t value = (uint16_t)values[i];
        cb_push_back(&cb, &value);
    }
}

static void save_best_demodulation(size_t sample_count) {
    uint8_t best_errors = UINT8_MAX;
    last_demod_length = 0;

    for (uint8_t phase = 0; phase < FSK_BITRATE_HID; phase++) {
        fsk_t *modem = fsk_alloc(FSK_BITRATE_HID);
        if (modem == NULL) {
            return;
        }

        uint8_t bits[200] = {0};
        uint8_t bit_count = 0;
        for (size_t i = phase; i < sample_count && bit_count < sizeof(bits); i++) {
            bool bit;
            if (fsk_feed(modem, sample_buffer[i], &bit)) {
                bits[bit_count++] = bit ? 1u : 0u;
            }
        }
        fsk_free(modem);

        for (uint8_t pairing = 0; pairing < 2; pairing++) {
            uint8_t errors = 0;
            for (uint8_t i = pairing; (uint16_t)i + 1u < bit_count; i += 2) {
                errors += bits[i] == bits[i + 1];
            }
            if (errors >= best_errors) {
                continue;
            }

            best_errors = errors;
            memset(last_demod, 0, sizeof(last_demod));
            last_demod[0] = phase;
            last_demod[1] = pairing;
            last_demod[2] = errors;
            last_demod[3] = bit_count;
            for (uint8_t i = 0; i < bit_count; i++) {
                last_demod[4 + i / 8] |= (uint8_t)(bits[i] << (7 - (i % 8)));
            }
            last_demod_length = 4 + (bit_count + 7) / 8;
        }
        bsp_wdt_feed();
    }
}

size_t paradox_get_last_demod(uint8_t *data, size_t maximum) {
    size_t length = last_demod_length < maximum ? last_demod_length : maximum;
    memcpy(data, last_demod, length);
    return length;
}

bool paradox_read(uint8_t *data, uint32_t timeout_ms) {
    void *codec = paradox.alloc();
    if (codec == NULL) {
        return false;
    }
    paradox.decoder.start(codec, 0);

    cb_init(&cb, PARADOX_BUFFER_SIZE, sizeof(uint16_t));
    stop_lf_125khz_radio();
    lf_125khz_radio_saadc_disable();
    bsp_delay_ms(50);

    sampling = true;
    start_lf_125khz_radio();
    bsp_delay_ms(10);
    lf_125khz_radio_saadc_enable(saadc_cb);

    size_t sample_count = 0;
    autotimer *timer = bsp_obtain_timer(0);
    while (sample_count < PARADOX_SAMPLE_BUFFER_SIZE &&
           NO_TIMEOUT_1MS(timer, timeout_ms)) {
        uint16_t value;
        while (sample_count < PARADOX_SAMPLE_BUFFER_SIZE &&
               cb_pop_front(&cb, &value)) {
            sample_buffer[sample_count++] = value;
        }
        bsp_wdt_feed();
    }

    bsp_return_timer(timer);
    sampling = false;
    stop_lf_125khz_radio();
    lf_125khz_radio_saadc_disable();
    cb_free(&cb);

    save_best_demodulation(sample_count);

    bool found = false;
    for (uint8_t phase = 0; phase < FSK_BITRATE_HID && !found; phase++) {
        paradox.decoder.start(codec, 0);
        for (size_t i = phase; i < sample_count; i++) {
            if (paradox.decoder.feed(codec, sample_buffer[i])) {
                memcpy(data, paradox.get_data(codec), paradox.data_size);
                found = true;
                break;
            }
        }
        bsp_wdt_feed();
    }

    paradox.free(codec);
    return found;
}
