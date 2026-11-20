#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_INPUT_COUNT 2U
#define BUTTON_INPUT_QUEUE_SIZE 16U

typedef enum {
    ButtonInputA = 0,
    ButtonInputB,
} button_input_id_t;

typedef struct {
    button_input_id_t button;
    bool long_press;
    uint32_t duration_ticks;
} button_input_event_t;

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool press_active;
    bool pending;
    uint32_t changed_at;
    uint32_t pressed_at;
} button_input_key_t;

/* Platform-independent state; callers serialize access from GPIO, timer and main. */
typedef struct {
    button_input_key_t keys[BUTTON_INPUT_COUNT];
    button_input_event_t events[BUTTON_INPUT_QUEUE_SIZE];
    uint32_t counter_mask;
    uint32_t debounce_ticks;
    uint32_t dropped_events;
    uint8_t head;
    uint8_t count;
    bool suppressed;
} button_input_t;

/* counter_mask must match the hardware's wrapping tick counter (2^n - 1). */
void button_input_init(button_input_t *input, uint32_t counter_mask,
                       uint32_t debounce_ticks, bool a_pressed, bool b_pressed);
void button_input_edge(button_input_t *input, button_input_id_t button,
                       bool pressed, uint32_t now);

/* Returns true while this key still needs debounce polling. */
bool button_input_poll(button_input_t *input, button_input_id_t button,
                       bool pressed, uint32_t now, uint32_t long_press_ticks);
bool button_input_event_get(button_input_t *input, button_input_event_t *event);

/* Discard pending clicks and ignore held keys until both are stably released. */
void button_input_suppress_until_release(button_input_t *input);

#endif
