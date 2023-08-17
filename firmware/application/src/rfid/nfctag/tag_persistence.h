#ifndef TAG_PERSISTENCE_H
#define TAG_PERSISTENCE_H

#include <stdint.h>
#include "tag_base_type.h"


typedef struct {
    uint16_t key;
    uint16_t id;
} fds_slot_record_map_t;

/**
 * 根据指定的卡槽和卡片场类型，获得其对应的卡片数据的FDS信息的映射对象
 */
void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t* map);

/**
 * 根据指定的卡槽和卡片场类型，获得其对应的卡片数据的昵称的FDS信息的映射对象
 */
void get_fds_map_by_slot_sense_type_for_nick(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t* map);

#endif
