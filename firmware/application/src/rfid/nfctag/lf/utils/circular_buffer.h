#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct circular_buffer {
    void *buffer;      // data buffer
    void *buffer_end;  // end of data buffer
    size_t capacity;   // maximum number of items in the buffer
    size_t count;      // number of items in the buffer
    size_t sz;         // size of each item in the buffer
    void *head;        // pointer to head
    void *tail;        // pointer to tail
} circular_buffer;

extern bool cb_init(circular_buffer *cb, size_t capacity, size_t sz);
extern void cb_free(circular_buffer *cb);
extern bool cb_push_back(circular_buffer *cb, const void *item);
extern bool cb_pop_front(circular_buffer *cb, void *item);

#ifdef __cplusplus
}
#endif