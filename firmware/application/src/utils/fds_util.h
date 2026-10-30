#ifndef FDS_UTIL_H__
#define FDS_UTIL_H__

#include "fds.h"

typedef enum {
    FDS_LOOKUP_ERROR = -1,
    FDS_LOOKUP_ABSENT = 0,
    FDS_LOOKUP_PRESENT = 1,
} fds_record_lookup_result_t;

/* Only NOT_FOUND proves absence; callers can retain state on lookup errors. */
fds_record_lookup_result_t fds_record_lookup_sync(uint16_t id, uint16_t key);
/* Nonnegative record count, or -1 when lookup/uniqueness cannot be established. */
int fds_record_count_sync(uint16_t id, uint16_t key);

bool fds_read_sync(uint16_t id, uint16_t key, uint16_t *length, uint8_t *buffer);
/* Waits until matching terminal completion without feeding the runtime watchdog;
 * failure never releases a pending FDS buffer. Before watchdog initialization,
 * liveness still depends on the SDK callback. Caller buffers are word-aligned
 * and word-padded. */
bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer);
int fds_delete_sync(uint16_t id, uint16_t key);
bool fds_is_exists(uint16_t id, uint16_t key);
void fds_util_init(void);
void fds_gc_sync(void);
bool fds_wipe(void);

#endif
