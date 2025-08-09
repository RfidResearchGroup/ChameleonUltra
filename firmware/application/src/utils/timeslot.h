#ifndef TIME_SLOT_H__
#define TIME_SLOT_H__

#include <stdint.h>
#include <stdbool.h>

typedef void (*timeslot_callback_t)();
void request_timeslot(uint32_t time_us, timeslot_callback_t callback);
void timeslot_start(uint32_t time_us);
void timeslot_stop(void);
#endif
