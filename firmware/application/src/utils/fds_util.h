#ifndef FDS_UTIL_H__
#define FDS_UTIL_H__

#include "fds.h"


bool fds_read_sync(uint16_t id, uint16_t key, uint16_t *length, uint8_t *buffer);
bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer);
int fds_delete_sync(uint16_t id, uint16_t key);
bool fds_is_exists(uint16_t id, uint16_t key);
void fds_util_init(void);
void fds_gc_sync(void);
bool fds_wipe(void);

#endif
