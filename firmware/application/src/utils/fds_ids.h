#ifndef FDS_IDS_H
#define FDS_IDS_H

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

/*
 * Slot for settings like LED animation mode and future options
 */
#define FDS_SETTINGS_KEY        0x1069
#define FDS_SETTINGS_ID         0x1069

#endif