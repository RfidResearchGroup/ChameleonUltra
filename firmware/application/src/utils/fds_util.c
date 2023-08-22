#include "fds_util.h"


#define NRF_LOG_MODULE_NAME fds_sync
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


// current write record info
static struct {
    uint32_t record_id; // record id, used for sync delete
    uint16_t id;        // file id
    uint16_t key;       // file key
    bool success;       // task is success
    bool waiting;       // task waiting done.
} fds_operation_info;


/**
 * 查询记录是否存在，并且获得记录的句柄
 */
static bool fds_find_record(uint16_t id, uint16_t key, fds_record_desc_t *desc) {
    fds_find_token_t ftok;
    memset(&ftok, 0x00, sizeof(fds_find_token_t));  // 使用之前需要先清空
    if (fds_record_find(id, key, desc, &ftok) == NRF_SUCCESS) {
        return true;
    }
    return false;
}

/**
 * @brief Determine whether the record exists.
 *
 * @param id record id
 * @param key record key
 * @return true on record exists
 * @return false on no found
 */
bool fds_is_exists(uint16_t id, uint16_t key) {
    fds_record_desc_t record_desc;
    if (fds_find_record(id, key, &record_desc)) {
        return true;
    }
    return false;
}


/**
 * 读取记录
 */
bool fds_read_sync(uint16_t id, uint16_t key, uint16_t max_length, uint8_t *buffer) {
    ret_code_t          err_code;       // 操作的结果码
    fds_flash_record_t  flash_record;   // 指向flash中的实际信息
    fds_record_desc_t   record_desc;    // 记录的句柄
    if (fds_find_record(id, key, &record_desc)) {
        err_code = fds_record_open(&record_desc, &flash_record);            // 打开记录，使之被标记为打开状态
        APP_ERROR_CHECK(err_code);
        if (flash_record.p_header->length_words * 4 <= max_length) {        // 在此处将flash中的数据读取到给定的RAM中
            // 确保缓冲区不会溢出后，读出此记录
            memcpy(buffer, flash_record.p_data, flash_record.p_header->length_words * 4);
            NRF_LOG_INFO("FDS read success.");
            return true;
        } else {
            NRF_LOG_INFO("FDS buffer too small, can't run memcpy, fds size = %d, buffer size = %d", flash_record.p_header->length_words * 4, max_length);
        }
        err_code = fds_record_close(&record_desc);                          // 操作完成后关闭文件
        APP_ERROR_CHECK(err_code);
    }
    // 加载不到正确的数据，可能是不存在这个记录
    return false;
}

/**
 * 没有GC过程的写入操作函数的实现
 */
static ret_code_t fds_write_record_nogc(uint16_t id, uint16_t key, uint16_t data_length_words, void *buffer) {
    ret_code_t          err_code;       // 操作的结果码
    fds_record_desc_t   record_desc;    // 记录的句柄
    fds_record_t record = {             // 记录的实体，写和更新操作时用的上
        .file_id = id, .key = key,
        .data = { .p_data = buffer, .length_words = data_length_words, }
    };
    if (fds_find_record(id, key, &record_desc)) {   // 查找具有指定特征的记录
        // 能找得到这个记录，我们可以进行update操作
        err_code = fds_record_update(&record_desc, &record);
        if (err_code == NRF_SUCCESS) {
            NRF_LOG_INFO("Search FileID: 0x%04x, FileKey: 0x%04x is found, will update.", id, key);
        }
    } else {
        // 无法找到有效的记录，我们进行第一次写入操作
        err_code = fds_record_write(&record_desc, &record);
        if (err_code == NRF_SUCCESS) {
            NRF_LOG_INFO("Search FileID: 0x%04x, FileKey: 0x%04x no found, will create.", id, key);
        }
    }
    return err_code;
}

/**
 * 写入记录
 */
bool fds_write_sync(uint16_t id, uint16_t key, uint16_t data_length_words, void *buffer) {
    // Make only one task running
    APP_ERROR_CHECK_BOOL(!fds_operation_info.waiting);
    // write result
    bool ret = true;
    // write or update record info cache
    fds_operation_info.id = id;
    fds_operation_info.key = key;
    fds_operation_info.success = false;
    fds_operation_info.waiting = true;

    // 调用无自动GC的写实现函数
    ret_code_t err_code = fds_write_record_nogc(id, key, data_length_words, buffer);
    if (err_code == NRF_SUCCESS) {
        while (!fds_operation_info.success) {
            __NOP();
        }; // 等待操作完成
    } else if (err_code == FDS_ERR_NO_SPACE_IN_FLASH) {   // 确保还有空间可以操作，否则需要GC
        // 当前报错是属于空间不足的报错，可能我们需要进行GC
        NRF_LOG_INFO("FDS no space, gc auto start.");
        fds_gc_sync();

        // gc完成后，可以重新进行相应的操作了
        NRF_LOG_INFO("FDS auto gc success, write record continue.");
        fds_operation_info.success = false;
        err_code = fds_write_record_nogc(id, key, data_length_words, buffer);
        if (err_code == NRF_SUCCESS) {
            while (!fds_operation_info.success) {
                __NOP();
            }; // 等待操作完成
        } else if (err_code == FDS_ERR_NO_SPACE_IN_FLASH) {
            // gc了一次之后，发现还是没有空间，那么可能是开发者没有考虑好空间分配导致溢出了
            NRF_LOG_ERROR("FDS no space to write.");
            ret = false;
        } else {
            // 如果不是空间不足的报错，那么我们就需要catch这个错误，开发时就解决
            APP_ERROR_CHECK(err_code);
        }
    } else {
        // 同上
        APP_ERROR_CHECK(err_code);
    }

    // task process finish
    fds_operation_info.waiting = false;
    return ret;
}

/*
 * 删除记录
 */
int fds_delete_sync(uint16_t id, uint16_t key) {
    int                 delete_count = 0;
    fds_record_desc_t   record_desc;
    ret_code_t          err_code;
    while (fds_find_record(id, key, &record_desc)) {
        fds_operation_info.success = false;
        fds_record_id_from_desc(&record_desc, &fds_operation_info.record_id);
        err_code = fds_record_delete(&record_desc);
        APP_ERROR_CHECK(err_code);
        delete_count++;
        while (!fds_operation_info.success) {
            __NOP();
        }; // 等待操作完成
    }
    return delete_count;
}

/**
 * FDS事件回调
 */
static void fds_evt_handler(fds_evt_t const *p_evt) {
    // To process fds event
    switch (p_evt->id) {
        case FDS_EVT_INIT: {
            if (p_evt->result == NRF_SUCCESS) {
                NRF_LOG_INFO("NRF52 FDS libraries init success.");
            } else {
                APP_ERROR_CHECK(p_evt->result);
            }
        }
        break;
        case FDS_EVT_WRITE:
        case FDS_EVT_UPDATE: {
            if (p_evt->result == NRF_SUCCESS) {
                NRF_LOG_INFO("Record change: FileID 0x%04x, RecordKey 0x%04x", p_evt->write.file_id, p_evt->write.record_key);
                if (p_evt->write.file_id == fds_operation_info.id && p_evt->write.record_key == fds_operation_info.key) {
                    // 上面的逻辑已经确保是我们当前正在写的任务完成了！
                    fds_operation_info.success = true;
                }
            } else {
                APP_ERROR_CHECK(p_evt->result);
            }
        }
        break;
        case FDS_EVT_DEL_RECORD: {
            if (p_evt->result == NRF_SUCCESS) {
                NRF_LOG_INFO(
                    "Record remove: FileID: 0x%04x, RecordKey: 0x%04x, RecordID: %08x",
                    p_evt->del.file_id, p_evt->del.record_key, p_evt->del.record_id
                );
                if (p_evt->del.record_id == fds_operation_info.record_id) {
                    // Only check record id because fileID and recordKey aren't available
                    // if deleting via fds_record_iterate. record id is guaranteed to be unique.
                    fds_operation_info.success = true;
                }
            } else {
                APP_ERROR_CHECK(p_evt->result);
            }
        }
        break;
        case FDS_EVT_GC: {
            if (p_evt->result == NRF_SUCCESS) {
                fds_operation_info.success = true;
            } else {
                APP_ERROR_CHECK(p_evt->result);
            }
        }
        break;
        default: {
            // nothing to do...
        } break;
    }
}

/**
 * 初始化nrf52的fds库
 */
void fds_util_init() {
    // reset waiting flag
    fds_operation_info.waiting = false;
    // 先注册事件回调
    ret_code_t err_code = fds_register(fds_evt_handler);
    APP_ERROR_CHECK(err_code);
    // 开始初始化fds库
    err_code = fds_init();
    APP_ERROR_CHECK(err_code);
}

void fds_gc_sync(void) {
    fds_operation_info.success = false;
    ret_code_t err_code = fds_gc();
    APP_ERROR_CHECK(err_code);
    while (!fds_operation_info.success) {
        __NOP();
    };
}

static bool fds_next_record_delete_sync() {
    fds_find_token_t  tok   = {0};
    fds_record_desc_t desc  = {0};
    if (fds_record_iterate(&desc, &tok) != NRF_SUCCESS) {
        NRF_LOG_INFO("No more records to delete");
        return false;
    }

    fds_record_id_from_desc(&desc, &fds_operation_info.record_id);
    NRF_LOG_INFO("Deleting record with id=%08x", fds_operation_info.record_id);

    fds_operation_info.success = false;
    ret_code_t rc = fds_record_delete(&desc);
    if (rc != NRF_SUCCESS) {
        NRF_LOG_WARNING("Record id=%08x deletion failed with rc=%d!", fds_operation_info.record_id, rc);
        return false;
    }

    while (!fds_operation_info.success) {
        __NOP();
    }

    NRF_LOG_INFO("Record id=%08x deleted successfully", fds_operation_info.record_id);
    return true;
}

bool fds_wipe(void) {
    NRF_LOG_INFO("Full fds wipe requested");
    while (fds_next_record_delete_sync()) {}
    fds_gc_sync();
    return true;
}
