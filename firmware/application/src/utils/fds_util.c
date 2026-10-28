#include "fds_util.h"
#include "app_error.h"
#include <string.h>

#define NRF_LOG_MODULE_NAME fds_sync
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/* FDS owns submitted buffers until a matching terminal callback. No timeout
 * releases ownership: Nordic FDS has no cancellation contract for a write. */
static struct {
    uint32_t record_id;
    uint16_t id;
    uint16_t key;
    volatile bool success;
    volatile bool done;
    bool waiting;
    uint8_t kind; /* 1 write/update, 2 delete, 3 GC */
} fds_operation_info;

static bool fds_find_record(uint16_t id, uint16_t key, fds_record_desc_t *desc) {
    fds_find_token_t token = {0};
    return fds_record_find(id, key, desc, &token) == NRF_SUCCESS;
}

fds_record_lookup_result_t fds_record_lookup_sync(uint16_t id, uint16_t key) {
    fds_record_desc_t desc;
    fds_find_token_t token = {0};
    ret_code_t result = fds_record_find(id, key, &desc, &token);
    if (result == NRF_SUCCESS) return FDS_LOOKUP_PRESENT;
    if (result == FDS_ERR_NOT_FOUND) return FDS_LOOKUP_ABSENT;
    return FDS_LOOKUP_ERROR;
}

bool fds_is_exists(uint16_t id, uint16_t key) {
    return fds_record_lookup_sync(id, key) == FDS_LOOKUP_PRESENT;
}

int fds_record_count_sync(uint16_t id, uint16_t key) {
    fds_find_token_t token = {0};
    fds_record_desc_t desc;
    for (unsigned count = 0; count < UINT16_MAX; ++count) {
        ret_code_t found = fds_record_find(id, key, &desc, &token);
        if (found == FDS_ERR_NOT_FOUND) return (int)count;
        if (found != NRF_SUCCESS) return -1;
    }
    return -1;
}

bool fds_read_sync(uint16_t id, uint16_t key, uint16_t *length, uint8_t *buffer) {
    if (!length || !buffer) return false;
    fds_record_desc_t desc;
    fds_flash_record_t record;
    uint16_t capacity = *length;
    *length = 0;
    if (!fds_find_record(id, key, &desc) || fds_record_open(&desc, &record) != NRF_SUCCESS)
        return false;
    size_t bytes = (size_t)record.p_header->length_words * 4U;
    bool fits = bytes <= capacity;
    if (fits) memcpy(buffer, record.p_data, bytes);
    /* Every successful open has exactly one close, including a short buffer. */
    ret_code_t closed = fds_record_close(&desc);
    if (!fits || closed != NRF_SUCCESS) return false;
    *length = bytes;
    return true;
}

static void fds_wait_terminal(void) {
    /* Keep the caller's buffer owned until completion or the normal runtime
     * watchdog reset. Feeding it here would turn a lost callback into a hang. */
    while (!fds_operation_info.done) {
        __NOP();
    }
}

static bool fds_gc_run(void) {
    fds_operation_info.kind = 3;
    fds_operation_info.done = fds_operation_info.success = false;
    ret_code_t result = fds_gc();
    if (result != NRF_SUCCESS) return false;
    fds_wait_terminal();
    return fds_operation_info.success;
}

static ret_code_t fds_write_record_nogc(uint16_t id, uint16_t key,
                                       uint16_t words, void *buffer) {
    fds_record_desc_t desc;
    fds_record_t record = {
        .file_id = id, .key = key,
        .data = {.p_data = buffer, .length_words = words}
    };
    fds_find_token_t token = {0};
    ret_code_t found = fds_record_find(id, key, &desc, &token);
    if (found == NRF_SUCCESS) return fds_record_update(&desc, &record);
    if (found == FDS_ERR_NOT_FOUND) return fds_record_write(&desc, &record);
    return found;
}

bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer) {
    if (fds_operation_info.waiting || !buffer || !length) return false;
    fds_operation_info.waiting = true;
    bool success = false;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        fds_operation_info.kind = 1;
        fds_operation_info.id = id;
        fds_operation_info.key = key;
        fds_operation_info.done = fds_operation_info.success = false;
        ret_code_t result = fds_write_record_nogc(id, key, (length + 3U) / 4U, buffer);
        if (result == NRF_SUCCESS) {
            fds_wait_terminal();
            success = fds_operation_info.success;
            break;
        }
        if (result != FDS_ERR_NO_SPACE_IN_FLASH || attempt != 0 || !fds_gc_run()) break;
    }
    fds_operation_info.kind = 0;
    fds_operation_info.waiting = false;
    return success;
}

static bool fds_delete_record_sync(fds_record_desc_t *desc) {
    if (fds_record_id_from_desc(desc, &fds_operation_info.record_id) != NRF_SUCCESS) return false;
    fds_operation_info.kind = 2;
    fds_operation_info.done = fds_operation_info.success = false;
    if (fds_record_delete(desc) != NRF_SUCCESS) return false;
    fds_wait_terminal();
    return fds_operation_info.success;
}

int fds_delete_sync(uint16_t id, uint16_t key) {
    if (fds_operation_info.waiting) return -1;
    fds_operation_info.waiting = true;
    int count = 0;
    fds_record_desc_t desc;
    for (;;) {
        fds_find_token_t token = {0};
        ret_code_t found = fds_record_find(id, key, &desc, &token);
        if (found == FDS_ERR_NOT_FOUND) break;
        if (found != NRF_SUCCESS || !fds_delete_record_sync(&desc)) { count = -1; break; }
        ++count;
    }
    fds_operation_info.kind = 0;
    fds_operation_info.waiting = false;
    return count;
}

static void fds_evt_handler(fds_evt_t const *event) {
    /* Initialization is asynchronous and has no synchronous-operation owner. */
    if (event->id == FDS_EVT_INIT) {
        APP_ERROR_CHECK(event->result);
        return;
    }
    if (!fds_operation_info.waiting) return;
    bool matches = false;
    switch (event->id) {
        case FDS_EVT_WRITE:
        case FDS_EVT_UPDATE:
            matches = fds_operation_info.kind == 1 &&
                      event->write.file_id == fds_operation_info.id &&
                      event->write.record_key == fds_operation_info.key;
            break;
        case FDS_EVT_DEL_RECORD:
            matches = fds_operation_info.kind == 2 &&
                      event->del.record_id == fds_operation_info.record_id;
            break;
        case FDS_EVT_GC:
            matches = fds_operation_info.kind == 3;
            break;
        default:
            break;
    }
    if (matches) {
        fds_operation_info.success = event->result == NRF_SUCCESS;
        fds_operation_info.done = true;
    }
}

void fds_util_init(void) {
    memset(&fds_operation_info, 0, sizeof(fds_operation_info));
    APP_ERROR_CHECK(fds_register(fds_evt_handler));
    APP_ERROR_CHECK(fds_init());
}

void fds_gc_sync(void) {
    if (fds_operation_info.waiting) return;
    fds_operation_info.waiting = true;
    (void)fds_gc_run();
    fds_operation_info.kind = 0;
    fds_operation_info.waiting = false;
}

bool fds_wipe(void) {
    if (fds_operation_info.waiting) return false;
    fds_operation_info.waiting = true;
    bool success = true;
    for (;;) {
        fds_find_token_t token = {0};
        fds_record_desc_t desc;
        ret_code_t result = fds_record_iterate(&desc, &token);
        if (result == FDS_ERR_NOT_FOUND) break;
        if (result != NRF_SUCCESS || !fds_delete_record_sync(&desc)) { success = false; break; }
    }
    if (success) success = fds_gc_run();
    fds_operation_info.kind = 0;
    fds_operation_info.waiting = false;
    return success;
}
