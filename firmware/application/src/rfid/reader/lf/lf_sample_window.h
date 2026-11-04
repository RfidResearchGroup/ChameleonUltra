#ifndef LF_SAMPLE_WINDOW_H
#define LF_SAMPLE_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    volatile size_t count;
} lf_sample_window_t;

static inline void lf_sample_window_init(lf_sample_window_t *window,
                                         uint8_t *data, size_t capacity) {
    window->data = data;
    window->capacity = capacity;
    window->count = 0U;
}

static inline uint8_t lf_sample_u8(int16_t value) {
    /* Keep the positive >>5 encoding; negative ADC noise saturates at zero. */
    if (value <= 0) return 0U;
    uint16_t scaled = (uint16_t)value >> 5U;
    return scaled > UINT8_MAX ? UINT8_MAX : (uint8_t)scaled;
}

static inline void lf_sample_window_feed(lf_sample_window_t *window,
                                         const int16_t *samples, size_t count) {
    size_t offset = window->count;
    size_t available = window->capacity - offset;
    if (count > available) count = available;
    for (size_t i = 0U; i < count; ++i) {
        window->data[offset + i] = lf_sample_u8(samples[i]);
    }
    /* Publish only the prefix whose bytes have all been initialized. */
    window->count = offset + count;
}

static inline bool lf_sample_window_complete(const lf_sample_window_t *window) {
    return window->count == window->capacity;
}

#endif
