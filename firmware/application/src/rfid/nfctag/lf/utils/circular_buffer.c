#include "circular_buffer.h"

#include <stdlib.h>
#include <string.h>

bool cb_init(circular_buffer *cb, size_t capacity, size_t sz)
{
    cb->buffer = malloc(capacity * sz);
    if (cb->buffer == NULL) {
        return false;
    }
    cb->buffer_end = (char *)cb->buffer + capacity * sz;
    cb->capacity = capacity;
    cb->count = 0;
    cb->sz = sz;
    cb->head = cb->buffer;
    cb->tail = cb->buffer;
    return true;
}

void cb_free(circular_buffer *cb)
{
    if (cb != NULL && cb->buffer != NULL) {
        free(cb->buffer);
        cb->buffer = NULL;
    }
}

bool cb_push_back(circular_buffer *cb, const void *item)
{
    if (cb->buffer == NULL || cb->count == cb->capacity) {
        return false;
    }
    memcpy(cb->head, item, cb->sz);
    cb->head = (char *)cb->head + cb->sz;
    if (cb->head == cb->buffer_end) {
        cb->head = cb->buffer;
    }
    cb->count++;
    return true;
}

bool cb_pop_front(circular_buffer *cb, void *item)
{
    if (cb->buffer == NULL || cb->count == 0) {
        return false;
    }
    memcpy(item, cb->tail, cb->sz);
    cb->tail = (char *)cb->tail + cb->sz;
    if (cb->tail == cb->buffer_end) {
        cb->tail = cb->buffer;
    }
    cb->count--;
    return true;
}