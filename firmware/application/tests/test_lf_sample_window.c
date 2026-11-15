#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lf_sample_window.h"

static int16_t batch[2048];
static uint8_t output[4002];

static void prepare(void) {
    memset(output, 0xA5, sizeof(output));
    for (size_t i = 0U; i < 2048U; ++i) batch[i] = (int16_t)((i % 200U) * 32U);
}

static void check_guards(void) {
    assert(output[0] == 0xA5 && output[4001] == 0xA5);
}

static void test_2048_sample_burst_fills_800_without_ring_loss(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 800);
    lf_sample_window_feed(&window, batch, 2048);
    assert(lf_sample_window_complete(&window) && window.count == 800);
    for (size_t i = 0; i < 800; ++i) assert(output[1 + i] == i % 200);
    assert(output[801] == 0xA5);
    check_guards();
}

static void test_two_dma_batches_preserve_boundary(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 4000);
    lf_sample_window_feed(&window, batch, 2048);
    assert(!lf_sample_window_complete(&window) && window.count == 2048);
    for (size_t i = 0; i < 2048; ++i) batch[i] = (int16_t)(((i + 73) % 200) * 32);
    lf_sample_window_feed(&window, batch, 2048);
    assert(lf_sample_window_complete(&window));
    for (size_t i = 0; i < 2048; ++i) assert(output[1 + i] == i % 200);
    for (size_t i = 0; i < 1952; ++i) assert(output[2049 + i] == (i + 73) % 200);
    check_guards();
}

static void test_complete_window_is_immutable(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 1);
    batch[0] = 32;
    lf_sample_window_feed(&window, batch, 2048);
    batch[0] = 64;
    lf_sample_window_feed(&window, batch, 2048);
    assert(output[1] == 1 && output[2] == 0xA5 && window.count == 1);
}

static void test_signed_and_saturation_boundaries(void) {
    assert(lf_sample_u8(INT16_MIN) == 0);
    assert(lf_sample_u8(-1) == 0);
    assert(lf_sample_u8(0) == 0);
    assert(lf_sample_u8(31) == 0);
    assert(lf_sample_u8(32) == 1);
    assert(lf_sample_u8(8191) == 255);
    assert(lf_sample_u8(8192) == 255);
    assert(lf_sample_u8(INT16_MAX) == 255);
}

static void test_partial_prefix_is_not_complete(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 800);
    lf_sample_window_feed(&window, batch, 512);
    assert(window.count == 512 && !lf_sample_window_complete(&window));
    assert(output[513] == 0xA5);
}

static void test_zero_completion_callback(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 800);
    lf_sample_window_feed(&window, NULL, 0);
    assert(window.count == 0 && !lf_sample_window_complete(&window));
    assert(output[1] == 0xA5);
}

static void test_retired_window_does_not_dereference_buffers(void) {
    lf_sample_window_t window;
    lf_sample_window_init(&window, NULL, 0);
    lf_sample_window_feed(&window, NULL, 2048);
    assert(window.count == 0);
}

static void test_new_acquisition_cannot_inherit_count(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 800);
    lf_sample_window_feed(&window, batch, 2048);
    lf_sample_window_init(&window, output + 1, 4000);
    assert(window.count == 0 && !lf_sample_window_complete(&window));
}

static void test_single_sample_callbacks(void) {
    prepare();
    lf_sample_window_t window;
    lf_sample_window_init(&window, output + 1, 800);
    for (size_t i = 0; i < 800; ++i) lf_sample_window_feed(&window, &batch[i], 1);
    assert(lf_sample_window_complete(&window));
    for (size_t i = 0; i < 800; ++i) assert(output[i + 1] == i % 200);
}

static void test_all_window_lengths_remain_bounded(void) {
    for (size_t length = 1; length <= 4000; ++length) {
        prepare();
        lf_sample_window_t window;
        lf_sample_window_init(&window, output + 1, length);
        lf_sample_window_feed(&window, batch, 2048);
        lf_sample_window_feed(&window, batch, 2048);
        assert(window.count == length && lf_sample_window_complete(&window));
        assert(output[length + 1] == 0xA5);
        check_guards();
    }
}

int main(void) {
    test_2048_sample_burst_fills_800_without_ring_loss();
    test_two_dma_batches_preserve_boundary();
    test_complete_window_is_immutable();
    test_signed_and_saturation_boundaries();
    test_partial_prefix_is_not_complete();
    test_zero_completion_callback();
    test_retired_window_does_not_dereference_buffers();
    test_new_acquisition_cannot_inherit_count();
    test_single_sample_callbacks();
    test_all_window_lengths_remain_bounded();
    puts("10 LF sample window tests passed");
    return 0;
}
