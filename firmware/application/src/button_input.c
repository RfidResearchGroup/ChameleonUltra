#include "button_input.h"

#include <string.h>

static uint32_t ticks_since(const button_input_t *input, uint32_t now, uint32_t then) {
    return (now - then) & input->counter_mask;
}

static void update_suppression(button_input_t *input) {
    if (!input->suppressed) {
        return;
    }
    for (unsigned int i = 0; i < BUTTON_INPUT_COUNT; ++i) {
        const button_input_key_t *key = &input->keys[i];
        if (key->raw_pressed || key->stable_pressed || key->pending) {
            return;
        }
    }
    input->suppressed = false;
}

void button_input_init(button_input_t *input, uint32_t counter_mask,
                       uint32_t debounce_ticks, bool a_pressed, bool b_pressed) {
    memset(input, 0, sizeof(*input));
    input->counter_mask = counter_mask;
    input->debounce_ticks = debounce_ticks;
    input->keys[ButtonInputA].raw_pressed = a_pressed;
    input->keys[ButtonInputA].stable_pressed = a_pressed;
    input->keys[ButtonInputB].raw_pressed = b_pressed;
    input->keys[ButtonInputB].stable_pressed = b_pressed;
    /* A button held at boot has no matching press in this session. */
}

void button_input_edge(button_input_t *input, button_input_id_t button,
                       bool pressed, uint32_t now) {
    if ((unsigned int)button >= BUTTON_INPUT_COUNT) {
        return;
    }
    button_input_key_t *key = &input->keys[button];
    key->raw_pressed = pressed;
    key->changed_at = now & input->counter_mask;
    key->pending = true;
}

static void enqueue_release(button_input_t *input, button_input_id_t button,
                            uint32_t duration, uint32_t long_press_ticks) {
    if (input->count == BUTTON_INPUT_QUEUE_SIZE) {
        /* Preserve older events and expose overload instead of overwriting them. */
        if (input->dropped_events != UINT32_MAX) {
            ++input->dropped_events;
        }
        return;
    }
    unsigned int tail = (input->head + input->count) % BUTTON_INPUT_QUEUE_SIZE;
    input->events[tail] = (button_input_event_t) {
        .button = button,
        /* Keep the existing strict threshold: equality is still a short press. */
        .long_press = duration > long_press_ticks,
        .duration_ticks = duration,
    };
    ++input->count;
}

bool button_input_poll(button_input_t *input, button_input_id_t button,
                       bool pressed, uint32_t now, uint32_t long_press_ticks) {
    if ((unsigned int)button >= BUTTON_INPUT_COUNT) {
        return false;
    }
    button_input_key_t *key = &input->keys[button];
    /* If an edge was coalesced by GPIO, still require a full quiet interval. */
    if (pressed != key->raw_pressed) {
        button_input_edge(input, button, pressed, now);
    }
    if (!key->pending) {
        return false;
    }
    if (ticks_since(input, now, key->changed_at) < input->debounce_ticks) {
        return true;
    }
    key->pending = false;
    if (key->stable_pressed != key->raw_pressed) {
        key->stable_pressed = key->raw_pressed;
        if (input->suppressed) {
            key->press_active = false;
        } else if (key->stable_pressed) {
            key->press_active = true;
            key->pressed_at = key->changed_at;
        } else if (key->press_active) {
            key->press_active = false;
            /* Classify the physical edges, not the delay of a timer callback. */
            uint32_t duration = ticks_since(input, key->changed_at, key->pressed_at);
            enqueue_release(input, button, duration, long_press_ticks);
        }
    }
    update_suppression(input);
    return false;
}

bool button_input_event_get(button_input_t *input, button_input_event_t *event) {
    if (input->count == 0) {
        return false;
    }
    *event = input->events[input->head];
    input->head = (input->head + 1U) % BUTTON_INPUT_QUEUE_SIZE;
    --input->count;
    return true;
}

void button_input_suppress_until_release(button_input_t *input) {
    input->head = 0;
    input->count = 0;
    input->suppressed = true;
    for (unsigned int i = 0; i < BUTTON_INPUT_COUNT; ++i) {
        input->keys[i].press_active = false;
    }
    update_suppression(input);
}
