#include <assert.h>
#include <stdio.h>

#include "button_input.h"

#define COUNTER_MASK 0x00ffffffU
#define DEBOUNCE 50U
#define LONG_PRESS 1000U

static button_input_t input;

static void reset(void) {
    button_input_init(&input, COUNTER_MASK, DEBOUNCE, false, false);
}

static void edge(button_input_id_t button, bool pressed, uint32_t now) {
    button_input_edge(&input, button, pressed, now & COUNTER_MASK);
}

static bool poll(button_input_id_t button, bool pressed, uint32_t now) {
    return button_input_poll(&input, button, pressed, now & COUNTER_MASK, LONG_PRESS);
}

static void stable_edge(button_input_id_t button, bool pressed, uint32_t now) {
    edge(button, pressed, now);
    assert(!poll(button, pressed, now + DEBOUNCE));
}

static void expect_event(button_input_id_t button, bool long_press, uint32_t duration) {
    button_input_event_t event;
    assert(button_input_event_get(&input, &event));
    assert(event.button == button);
    assert(event.long_press == long_press);
    assert(event.duration_ticks == duration);
}

static void expect_empty(void) {
    button_input_event_t event;
    assert(!button_input_event_get(&input, &event));
}

static void test_short_and_long(void) {
    reset();
    stable_edge(ButtonInputA, true, 100);
    expect_empty();
    stable_edge(ButtonInputA, false, 250);
    stable_edge(ButtonInputB, true, 400);
    stable_edge(ButtonInputB, false, 1800);
    expect_event(ButtonInputA, false, 150);
    expect_event(ButtonInputB, true, 1400);
    expect_empty();
}

static void test_overlapping_buttons_keep_their_own_clock(void) {
    reset();
    stable_edge(ButtonInputA, true, 0);
    stable_edge(ButtonInputB, true, 900);
    stable_edge(ButtonInputA, false, 1200);
    stable_edge(ButtonInputB, false, 1300);
    /* Both releases may wait in the queue before the main loop runs. */
    expect_event(ButtonInputA, true, 1200);
    expect_event(ButtonInputB, false, 400);
    expect_empty();
}

static void test_reverse_release_order(void) {
    reset();
    stable_edge(ButtonInputB, true, 0);
    stable_edge(ButtonInputA, true, 900);
    stable_edge(ButtonInputA, false, 1050);
    stable_edge(ButtonInputB, false, 1300);
    expect_event(ButtonInputA, false, 150);
    expect_event(ButtonInputB, true, 1300);
    expect_empty();
}

static void test_two_edges_in_one_debounce_window(void) {
    reset();
    edge(ButtonInputA, true, 100);
    edge(ButtonInputB, true, 110);
    assert(!poll(ButtonInputA, true, 150));
    assert(poll(ButtonInputB, true, 150));
    assert(!poll(ButtonInputB, true, 160));
    edge(ButtonInputA, false, 300);
    edge(ButtonInputB, false, 310);
    assert(!poll(ButtonInputA, false, 350));
    assert(!poll(ButtonInputB, false, 360));
    expect_event(ButtonInputA, false, 200);
    expect_event(ButtonInputB, false, 200);
    expect_empty();
}

static void test_press_and_release_bounce(void) {
    reset();
    edge(ButtonInputA, true, 100);
    edge(ButtonInputA, false, 110);
    edge(ButtonInputA, true, 120);
    assert(poll(ButtonInputA, true, 169));
    assert(!poll(ButtonInputA, true, 170));
    edge(ButtonInputA, false, 300);
    edge(ButtonInputA, true, 310);
    edge(ButtonInputA, false, 320);
    assert(poll(ButtonInputA, false, 369));
    assert(!poll(ButtonInputA, false, 370));
    assert(!poll(ButtonInputA, false, 500));
    expect_event(ButtonInputA, false, 200);
    expect_empty();
}

static void test_bounce_on_b_does_not_delay_a(void) {
    reset();
    edge(ButtonInputA, true, 100);
    edge(ButtonInputB, true, 110);
    edge(ButtonInputB, false, 120);
    edge(ButtonInputB, true, 130);
    assert(!poll(ButtonInputA, true, 150));
    assert(poll(ButtonInputB, true, 150));
    assert(!poll(ButtonInputB, true, 180));
    stable_edge(ButtonInputA, false, 200);
    stable_edge(ButtonInputB, false, 300);
    expect_event(ButtonInputA, false, 100);
    expect_event(ButtonInputB, false, 170);
    expect_empty();
}

static void test_glitch_and_unmatched_release_are_ignored(void) {
    reset();
    edge(ButtonInputA, true, 100);
    edge(ButtonInputA, false, 120);
    assert(!poll(ButtonInputA, false, 170));
    stable_edge(ButtonInputB, false, 300);
    expect_empty();
}

static void test_level_change_without_gpio_callback(void) {
    reset();
    edge(ButtonInputA, true, 100);
    /* The level changed while a GPIO event was coalesced. */
    assert(poll(ButtonInputA, false, 160));
    assert(poll(ButtonInputA, false, 209));
    assert(!poll(ButtonInputA, false, 210));
    expect_empty();
}

static void test_threshold_boundary_and_delayed_callback(void) {
    reset();
    stable_edge(ButtonInputA, true, 100);
    edge(ButtonInputA, false, 1100);
    /* A delayed timer callback must not turn an exact-threshold click into long. */
    assert(!poll(ButtonInputA, false, 1700));
    expect_event(ButtonInputA, false, LONG_PRESS);
    stable_edge(ButtonInputA, true, 2000);
    stable_edge(ButtonInputA, false, 3001);
    expect_event(ButtonInputA, true, LONG_PRESS + 1);
    expect_empty();
}

static void test_configurable_threshold(void) {
    reset();
    stable_edge(ButtonInputA, true, 100);
    edge(ButtonInputA, false, 401);
    assert(!button_input_poll(&input, ButtonInputA, false, 451, 300));
    expect_event(ButtonInputA, true, 301);
    expect_empty();
}

static void test_counter_wrap_on_press_and_release(void) {
    reset();
    uint32_t start = COUNTER_MASK - 20;
    stable_edge(ButtonInputA, true, start);
    stable_edge(ButtonInputA, false, start + 1200);
    expect_event(ButtonInputA, true, 1200);
    expect_empty();
    reset();
    stable_edge(ButtonInputB, true, COUNTER_MASK - 200);
    stable_edge(ButtonInputB, false, COUNTER_MASK - 20);
    expect_event(ButtonInputB, false, 180);
    expect_empty();
}

static void test_boot_held_key_needs_a_fresh_press(void) {
    button_input_init(&input, COUNTER_MASK, DEBOUNCE, true, true);
    stable_edge(ButtonInputA, false, 100);
    stable_edge(ButtonInputB, false, 200);
    expect_empty();
    stable_edge(ButtonInputA, true, 300);
    stable_edge(ButtonInputA, false, 450);
    expect_event(ButtonInputA, false, 150);
    expect_empty();
}

static void test_multiple_clicks_survive_delayed_main_loop(void) {
    reset();
    for (unsigned int i = 0; i < BUTTON_INPUT_QUEUE_SIZE; ++i) {
        button_input_id_t button = i % 2 ? ButtonInputB : ButtonInputA;
        stable_edge(button, true, i * 300);
        stable_edge(button, false, i * 300 + 100);
    }
    for (unsigned int i = 0; i < BUTTON_INPUT_QUEUE_SIZE; ++i) {
        expect_event(i % 2 ? ButtonInputB : ButtonInputA, false, 100);
    }
    assert(input.dropped_events == 0);
    expect_empty();
}

static void test_queue_overload_and_recovery(void) {
    reset();
    for (unsigned int i = 0; i < BUTTON_INPUT_QUEUE_SIZE + 3; ++i) {
        stable_edge(ButtonInputA, true, i * 300);
        stable_edge(ButtonInputA, false, i * 300 + 100);
    }
    assert(input.dropped_events == 3);
    for (unsigned int i = 0; i < BUTTON_INPUT_QUEUE_SIZE; ++i) {
        expect_event(ButtonInputA, false, 100);
    }
    stable_edge(ButtonInputB, true, 10000);
    stable_edge(ButtonInputB, false, 10200);
    expect_event(ButtonInputB, false, 200);
    expect_empty();
}

static void test_queue_indices_wrap_without_reordering(void) {
    reset();
    for (unsigned int i = 0; i < 100; ++i) {
        stable_edge(ButtonInputA, true, i * 400);
        stable_edge(ButtonInputA, false, i * 400 + 100);
        stable_edge(ButtonInputB, true, i * 400 + 200);
        stable_edge(ButtonInputB, false, i * 400 + 300);
        expect_event(ButtonInputA, false, 100);
        expect_event(ButtonInputB, false, 100);
    }
    expect_empty();
}

static void test_dismissal_discards_queue_and_held_partner(void) {
    reset();
    stable_edge(ButtonInputB, true, 0);
    stable_edge(ButtonInputA, true, 100);
    stable_edge(ButtonInputA, false, 200);
    button_input_suppress_until_release(&input);
    expect_empty();
    /* Further clicks are suppressed until the other held key is released too. */
    stable_edge(ButtonInputA, true, 400);
    stable_edge(ButtonInputA, false, 500);
    stable_edge(ButtonInputB, false, 2000);
    expect_empty();
    assert(!input.suppressed);
    stable_edge(ButtonInputA, true, 2200);
    stable_edge(ButtonInputA, false, 2400);
    expect_event(ButtonInputA, false, 200);
    expect_empty();
}

static void test_shutdown_wakeup_suppresses_even_unconfirmed_press(void) {
    reset();
    edge(ButtonInputA, true, 100);
    button_input_suppress_until_release(&input);
    assert(!poll(ButtonInputA, true, 150));
    stable_edge(ButtonInputA, false, 2000);
    expect_empty();
    assert(!input.suppressed);
    stable_edge(ButtonInputB, true, 2200);
    stable_edge(ButtonInputB, false, 2400);
    expect_event(ButtonInputB, false, 200);
}

static void test_suppression_waits_for_release_bounce(void) {
    reset();
    stable_edge(ButtonInputA, true, 100);
    edge(ButtonInputA, false, 200);
    button_input_suppress_until_release(&input);
    edge(ButtonInputA, true, 220);
    edge(ButtonInputA, false, 240);
    assert(poll(ButtonInputA, false, 280));
    assert(input.suppressed);
    assert(!poll(ButtonInputA, false, 290));
    assert(!input.suppressed);
    expect_empty();
}

static void test_suppress_idle_does_not_eat_next_click(void) {
    reset();
    button_input_suppress_until_release(&input);
    assert(!input.suppressed);
    stable_edge(ButtonInputB, true, 100);
    stable_edge(ButtonInputB, false, 300);
    expect_event(ButtonInputB, false, 200);
}

static void test_invalid_key_does_not_touch_valid_state(void) {
    reset();
    edge((button_input_id_t)-1, true, 0);
    assert(!poll((button_input_id_t)BUTTON_INPUT_COUNT, false, 100));
    stable_edge(ButtonInputA, true, 100);
    stable_edge(ButtonInputA, false, 300);
    expect_event(ButtonInputA, false, 200);
    expect_empty();
}

int main(void) {
    test_short_and_long();
    test_overlapping_buttons_keep_their_own_clock();
    test_reverse_release_order();
    test_two_edges_in_one_debounce_window();
    test_press_and_release_bounce();
    test_bounce_on_b_does_not_delay_a();
    test_glitch_and_unmatched_release_are_ignored();
    test_level_change_without_gpio_callback();
    test_threshold_boundary_and_delayed_callback();
    test_configurable_threshold();
    test_counter_wrap_on_press_and_release();
    test_boot_held_key_needs_a_fresh_press();
    test_multiple_clicks_survive_delayed_main_loop();
    test_queue_overload_and_recovery();
    test_queue_indices_wrap_without_reordering();
    test_dismissal_discards_queue_and_held_partner();
    test_shutdown_wakeup_suppresses_even_unconfirmed_press();
    test_suppression_waits_for_release_bounce();
    test_suppress_idle_does_not_eat_next_click();
    test_invalid_key_does_not_touch_valid_state();
    puts("20 button input tests passed");
    return 0;
}
