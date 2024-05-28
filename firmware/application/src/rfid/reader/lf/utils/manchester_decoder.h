#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bitarray.h"

typedef enum {
    ManchesterEvent1T = 0,
    ManchesterEvent15T = 2,
    ManchesterEvent2T = 4,
    ManchesterEventReset = 8
} ManchesterEvent;

typedef enum {
    ManchesterStateSync = 0,
    ManchesterStateNoSync = 1,
    ManchesterResetState = 2,
} ManchesterState;

ManchesterEvent manchester_length_decode(int interval_length, int _1t_length, int _deviation);

bool manchester_advance(
    ManchesterState state,
    ManchesterEvent event,
    ManchesterState* next_state,
    BitArray* data);

#ifdef __cplusplus
}
#endif
