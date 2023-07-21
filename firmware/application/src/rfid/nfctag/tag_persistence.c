#include "tag_persistence.h"


#define NRF_LOG_MODULE_NAME tag_persistence
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();



void get_fds_map_by_slot_auto_inc_id(uint16_t key, uint16_t id, uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t* map) {
    map->key = key + slot;
    uint8_t base_id = 0;
    switch(sense_type) {
        case TAG_SENSE_HF:
            base_id = 0;
            break;
        case TAG_SENSE_LF:
            base_id = 1;
            break;
        case TAG_SENSE_NO:
            // never to here...(if dev wrong, must fix)
            APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
    }
    map->id = id + base_id;
}


/**
 * 根据卡槽和卡槽中指定的场类型获得其在FDS中对应的数据的KEY和ID
 */
void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t* map) {
    // 根据 @see FDS_SLOT_TAG_DUMP_FILE_KEY 的约定，每个slot以其为起点，每个slot都有其单独的key的record，并且每个slot中独特的场类型也有一个数据的id
    get_fds_map_by_slot_auto_inc_id(FDS_SLOT_TAG_DUMP_FILE_KEY, FDS_SLOT_TAG_DUMP_FILE_ID, slot, sense_type, map);
}

/**
 * 根据卡槽和卡槽中指定的场类型获得其在FDS中对应的数据的KEY和ID
 */
void get_fds_map_by_slot_sense_type_for_nick(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t* map) {
    get_fds_map_by_slot_auto_inc_id(FDS_SLOT_TAG_NICK_NAME_KEY, FDS_SLOT_TAG_NICK_NAME_ID, slot, sense_type, map);
}
