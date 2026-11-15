#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ble_main.h"
#include "lf_t55xx_read.h"
#include "timeslot.h"

#define DMA_BATCH 2048U
#define MAX_EVENTS 64U

typedef struct {
    char type;
    uint32_t time;
} event_t;

static event_t events[MAX_EVENTS];
static size_t event_count;
static uint32_t time_us;
static uint32_t sample_started_us;
static uint32_t next_dma_us;
static uint32_t reservation_us;
static uint32_t reservation_started_us;
static size_t generated_samples;
static size_t batch_limit;
static unsigned dma_callbacks;
static unsigned prepare_count;
static unsigned disable_count;
static unsigned wdt_feeds;
static unsigned pause_calls;
static unsigned resume_calls;
static unsigned ble_checks;
static bool field_on;
static bool prepared;
static bool sampling;
static bool advertise;
static bool connected;
static bool connect_after_pause;
static bool advertising_pause_error;
static bool flat_signal;
static bool full_signed_range;
static bool nested_capture;
static bool skip_timeslot_callback;
static lf_adc_callback_t adc_callback;

static void event(char type) {
    assert(event_count < MAX_EVENTS);
    events[event_count++] = (event_t){type, time_us};
}

static int16_t input_sample(size_t index) {
    if (flat_signal) return -7;
    if (full_signed_range) {
        if ((index % 4U) == 0U) return INT16_MIN;
        if ((index % 4U) == 1U) return INT16_MAX;
        if ((index % 4U) == 2U) return 0;
        return -1;
    }
    return (int16_t)((int32_t)((index * 73U + 13U) % 1024U) - 512);
}

bool is_nus_working(void) {
    ble_checks++;
    return connected;
}

bool advertising_pause(bool *was_active) {
    assert(was_active != NULL);
    pause_calls++;
    *was_active = false;
    if (advertising_pause_error) {
        return false;
    }
    *was_active = advertise;
    advertise = false;
    if (connect_after_pause) connected = true;
    return true;
}

void advertising_start(bool erase_bonds) {
    assert(!erase_bonds);
    resume_calls++;
    advertise = true;
}

void start_lf_125khz_radio(void) {
    assert(prepared && !advertise && !sampling);
    field_on = true;
    event('N');
}

void stop_lf_125khz_radio(void) {
    field_on = false;
    event('F');
}

void nrf_gpio_cfg_output(uint32_t pin) {
    assert(pin == 17U && !field_on);
    event('G');
}

void nrf_gpio_pin_clear(uint32_t pin) {
    assert(pin == 17U && !field_on);
    event('L');
}

void lf_125khz_radio_saadc_prepare(lf_adc_callback_t callback) {
    assert(!prepared && !sampling && !advertise);
    prepared = true;
    adc_callback = callback;
    prepare_count++;
}

void lf_125khz_radio_saadc_start_sampling(void) {
    assert(prepared && field_on && !sampling);
    assert(event_count == 29U); /* power-on plus seven explicit field gaps */
    event('A');
    sampling = true;
    sample_started_us = time_us;
    next_dma_us = time_us + DMA_BATCH * 8U;
}

void lf_125khz_radio_saadc_stop_sampling(void) {
    assert(prepared);
    sampling = false;
    event('S');
}

void lf_125khz_radio_saadc_disable(void) {
    assert(prepared && !sampling && !field_on);
    adc_callback = NULL;
    prepared = false;
    disable_count++;
}

void bsp_delay_us(uint32_t delay_us) {
    time_us += delay_us;
    if (!sampling || (size_t)dma_callbacks >= batch_limit) {
        return;
    }
    while (time_us >= next_dma_us && (size_t)dma_callbacks < batch_limit) {
        int16_t batch[DMA_BATCH];
        for (size_t i = 0U; i < DMA_BATCH; i++) {
            batch[i] = input_sample(generated_samples + i);
        }
        if (nested_capture && dma_callbacks == 0U) {
            uint8_t unused[LF_T55XX_CAPTURE_MAX_BYTES];
            size_t nested_length = SIZE_MAX;
            size_t old_events = event_count;
            assert(!t55xx_read_block_capture(7U, unused, sizeof(unused), &nested_length));
            assert(nested_length == 0U && event_count == old_events);
        }
        adc_callback(batch, DMA_BATCH);
        generated_samples += DMA_BATCH;
        dma_callbacks++;
        next_dma_us += DMA_BATCH * 8U;
    }
}

void bsp_wdt_feed(void) {
    assert(sampling && prepared);
    wdt_feeds++;
}

void request_timeslot(uint32_t duration_us, timeslot_callback_t callback) {
    assert(prepared && !sampling && !advertise);
    reservation_us = duration_us;
    reservation_started_us = time_us;
    if (!skip_timeslot_callback) callback();
    assert(time_us - reservation_started_us < duration_us);
}

static void reset_fixture(void) {
    memset(events, 0, sizeof(events));
    event_count = 0U;
    time_us = 0U;
    sample_started_us = 0U;
    next_dma_us = 0U;
    reservation_us = 0U;
    reservation_started_us = 0U;
    generated_samples = 0U;
    batch_limit = SIZE_MAX;
    dma_callbacks = 0U;
    prepare_count = 0U;
    disable_count = 0U;
    wdt_feeds = 0U;
    pause_calls = 0U;
    resume_calls = 0U;
    ble_checks = 0U;
    field_on = false;
    prepared = false;
    sampling = false;
    advertise = true;
    connected = false;
    connect_after_pause = false;
    advertising_pause_error = false;
    flat_signal = false;
    full_signed_range = false;
    nested_capture = false;
    skip_timeslot_callback = false;
    adc_callback = NULL;
}

static void assert_idle(void) {
    assert(!field_on && !sampling && !prepared && adc_callback == NULL);
}

static int16_t read_i16(const uint8_t *bytes) {
    return (int16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static void assert_header(const uint8_t *bytes, int16_t low, int16_t high) {
    assert(bytes[0] == 1U && bytes[1] == 4U);
    assert((((uint16_t)bytes[2] << 8U) | bytes[3]) == LF_T55XX_CAPTURE_MAX_SAMPLES);
    assert(read_i16(&bytes[4]) == low && read_i16(&bytes[6]) == high);
}

static void assert_command(uint8_t block) {
    assert(events[0].type == 'N' && events[0].time == 0U);
    /* Seven F/G/L/N groups: start gap, then precisely six command bits. */
    const uint8_t bits[6] = {
        1U, 0U, 0U, (block >> 2U) & 1U, (block >> 1U) & 1U, block & 1U,
    };
    assert(events[1].type == 'F' && events[1].time == 4000U);
    assert(events[4].type == 'N' && events[4].time - events[1].time == 240U);
    for (size_t i = 0U; i < 6U; i++) {
        size_t previous_on = 4U + i * 4U;
        size_t off = previous_on + 1U;
        size_t on = off + 3U;
        assert(events[off].type == 'F');
        assert(events[off + 1U].type == 'G');
        assert(events[off + 2U].type == 'L');
        assert(events[on].type == 'N');
        assert(events[off].time - events[previous_on].time ==
               (bits[i] != 0U ? 432U : 192U));
        assert(events[on].time - events[off].time == 72U);
    }
    assert(events[29].type == 'A');
    assert(events[29].time == events[28].time);
    assert(sample_started_us == events[29].time);
    assert(reservation_us == 90000U);
}

static void test_read_frame_and_capture_start(void) {
    for (uint8_t block = 0U; block < 8U; block++) {
        reset_fixture();
        uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
        size_t length = 0U;
        assert(t55xx_read_block_capture(block, out, sizeof(out), &length));
        assert(length == sizeof(out));
        assert_command(block);
        assert(dma_callbacks == 4U && generated_samples == 8192U);
        assert(prepare_count == 1U && disable_count == 1U);
        assert(pause_calls == 1U && resume_calls == 1U && advertise);
        assert(wdt_feeds > 0U);
        assert_idle();
    }
}

static void test_quantization_and_capacity(void) {
    reset_fixture();
    struct {
        uint8_t before[8];
        uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
        uint8_t after[8];
    } guarded;
    memset(&guarded, 0xA5, sizeof(guarded));
    size_t length = 0U;
    assert(t55xx_read_block_capture(0U, guarded.out, sizeof(guarded.out), &length));
    assert(length == LF_T55XX_CAPTURE_MAX_BYTES);
    assert_header(guarded.out, -512, 511);
    for (size_t i = 0U; i < LF_T55XX_CAPTURE_MAX_SAMPLES; i++) {
        uint32_t shifted = (uint32_t)((int32_t)input_sample(i) + 512);
        uint8_t quantized = (uint8_t)((shifted * 15U + 511U) / 1023U);
        uint8_t byte = guarded.out[8U + i / 2U];
        uint8_t actual = (i & 1U) == 0U ? byte >> 4U : byte & 15U;
        assert(actual == quantized);
    }
    for (size_t i = 0U; i < 8U; i++) {
        assert(guarded.before[i] == 0xA5U && guarded.after[i] == 0xA5U);
    }
    assert_idle();
}

static void test_full_signed_adc_range(void) {
    reset_fixture();
    full_signed_range = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = 0U;
    assert(t55xx_read_block_capture(1U, out, sizeof(out), &length));
    assert_header(out, INT16_MIN, INT16_MAX);
    for (size_t i = 8U; i < sizeof(out); i += 2U) {
        assert(out[i] == 0x0FU);
        assert(out[i + 1U] == 0x87U);
    }
    assert_idle();
}

static void test_flat_acquisition_is_not_tag_detection(void) {
    reset_fixture();
    flat_signal = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = 0U;
    assert(t55xx_read_block_capture(2U, out, sizeof(out), &length));
    assert_header(out, -7, -7);
    for (size_t i = 8U; i < sizeof(out); i++) assert(out[i] == 0x88U);
    assert_idle();
}

static void test_validation_before_hardware(void) {
    reset_fixture();
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    memset(out, 0x5A, sizeof(out));
    size_t length = SIZE_MAX;
    assert(!t55xx_read_block_capture(8U, out, sizeof(out), &length));
    assert(length == 0U);
    assert(!t55xx_read_block_capture(255U, out, sizeof(out), &length));
    assert(!t55xx_read_block_capture(0U, NULL, sizeof(out), &length));
    assert(!t55xx_read_block_capture(0U, out, sizeof(out) - 1U, &length));
    assert(!t55xx_read_block_capture(0U, out, sizeof(out), NULL));
    assert(pause_calls == 0U && prepare_count == 0U && event_count == 0U);
    for (size_t i = 0U; i < sizeof(out); i++) assert(out[i] == 0x5AU);
    assert_idle();
}

static void test_connected_ble_is_rejected_before_hardware(void) {
    reset_fixture();
    connected = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = SIZE_MAX;
    assert(!t55xx_read_block_capture(0U, out, sizeof(out), &length));
    assert(length == 0U && prepare_count == 0U && event_count == 0U);
    assert(pause_calls == 0U && resume_calls == 0U && advertise);
    assert_idle();
}

static void test_advertising_ownership_and_connection_race(void) {
    reset_fixture();
    advertise = false;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = 0U;
    assert(t55xx_read_block_capture(0U, out, sizeof(out), &length));
    assert(pause_calls == 1U && resume_calls == 0U && !advertise);
    assert_idle();

    reset_fixture();
    connect_after_pause = true;
    assert(!t55xx_read_block_capture(0U, out, sizeof(out), &length));
    assert(length == 0U && prepare_count == 0U && event_count == 0U);
    assert(pause_calls == 1U && resume_calls == 1U);
    assert_idle();
}

static void test_missing_and_partial_dma_timeout_cleanup(void) {
    for (size_t batches = 0U; batches < 4U; batches++) {
        reset_fixture();
        batch_limit = batches;
        uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
        memset(out, 0x5A, sizeof(out));
        size_t length = SIZE_MAX;
        assert(!t55xx_read_block_capture(3U, out, sizeof(out), &length));
        assert(length == 0U && dma_callbacks == batches);
        assert(time_us - sample_started_us == 70000U);
        assert(prepare_count == 1U && disable_count == 1U);
        assert(pause_calls == 1U && resume_calls == 1U && advertise);
        for (size_t i = 0U; i < sizeof(out); i++) assert(out[i] == 0x5AU);
        assert_idle();
    }
}

static void test_advertising_pause_failure_before_rf(void) {
    reset_fixture();
    advertising_pause_error = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    memset(out, 0x5A, sizeof(out));
    size_t length = SIZE_MAX;
    assert(!t55xx_read_block_capture(0U, out, sizeof(out), &length));
    assert(length == 0U && pause_calls == 1U && resume_calls == 0U);
    assert(advertise && prepare_count == 0U && event_count == 0U);
    for (size_t i = 0U; i < sizeof(out); i++) assert(out[i] == 0x5AU);
    assert_idle();
}

static void test_capture_reentrancy_guard(void) {
    reset_fixture();
    nested_capture = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = 0U;
    assert(t55xx_read_block_capture(4U, out, sizeof(out), &length));
    assert(prepare_count == 1U && disable_count == 1U && pause_calls == 1U);
    assert_idle();
}

static void test_missing_timeslot_callback_cleanup(void) {
    reset_fixture();
    skip_timeslot_callback = true;
    uint8_t out[LF_T55XX_CAPTURE_MAX_BYTES];
    size_t length = SIZE_MAX;
    assert(!t55xx_read_block_capture(0U, out, sizeof(out), &length));
    assert(length == 0U && event_count == 0U);
    assert(prepare_count == 1U && disable_count == 1U);
    assert(pause_calls == 1U && resume_calls == 1U);
    assert_idle();
}

int main(void) {
    test_read_frame_and_capture_start();
    test_quantization_and_capacity();
    test_full_signed_adc_range();
    test_flat_acquisition_is_not_tag_detection();
    test_validation_before_hardware();
    test_connected_ble_is_rejected_before_hardware();
    test_advertising_ownership_and_connection_race();
    test_missing_and_partial_dma_timeout_cleanup();
    test_advertising_pause_failure_before_rf();
    test_capture_reentrancy_guard();
    test_missing_timeslot_callback_cleanup();
    puts("T55xx read capture: 11 test groups passed");
    return 0;
}
