#ifndef TAG_PERSISTENCE_H
#define TAG_PERSISTENCE_H

#include <stdint.h>
#include "tag_base_type.h"


/*
 * 卡槽配置，只有一份，一致即可
 */
#define FDS_CONFIG_RECORD_FILE_KEY   0x1066
#define FDS_CONFIG_RECORD_FILE_ID    0x1066

/*
 * 每个卡槽有高低频两种数据，其中key是跟卡槽走的，而id+n就等于数据索引，固定某个索引为指定类型即可
 * 每个slot的file_key都不一样
 * 每个slot有两种类型的卡片，因此有两个数据ID（当前）
 */
#define FDS_SLOT_TAG_DUMP_FILE_KEY   0x1067
#define FDS_SLOT_TAG_DUMP_FILE_ID    0x1067

/*
 * 每个卡槽有高低频两种数据，因此卡槽的昵称也要有两种，其中key是跟卡槽走的，而id+n就等于数据索引，固定某个索引为指定类型即可
 */
#define FDS_SLOT_TAG_NICK_NAME_KEY   0x1068
#define FDS_SLOT_TAG_NICK_NAME_ID    0x1068


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
