#ifndef TIMESLOT_H
#define TIMESLOT_H

#include <stdint.h>

typedef void (*timeslot_callback_t)(void);
void request_timeslot(uint32_t time_us, timeslot_callback_t callback);

#endif
