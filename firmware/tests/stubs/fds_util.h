// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef TEST_FDS_UTIL_H
#define TEST_FDS_UTIL_H

#include <stdbool.h>
#include <stdint.h>

bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer);

#endif
