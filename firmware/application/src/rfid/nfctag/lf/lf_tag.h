#ifndef __LF_TAG_H
#define __LF_TAG_H

#include <stdbool.h>

#define LF_125KHZ_BROADCAST_MAX     10      // 32.768ms once, about 31 times in one second

bool lf_is_field_exists(void);

#endif
