#ifndef NFC_TAG_H
#define NFC_TAG_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "utils.h"
#include "tag_base_type.h"

// 最多八张卡槽
#define TAG_MAX_SLOT_NUM    8

extern bool g_is_tag_emulating;

// 标签的数据缓冲区
typedef struct {
    uint16_t length;
    uint8_t* buffer;
    uint16_t* crc;
} tag_data_buffer_t;

// 场感应使能与闭能切换函数
typedef void (*tag_sense_switch_t)(bool enable);
// flash数据加载到RAM后通知给注册者
typedef int (*tag_datas_loadcb_t)(tag_specific_type_t type, tag_data_buffer_t* buffer);
// 数据要保存到flash之前通知给注册者
typedef int (*tag_datas_savecb_t)(tag_specific_type_t type, tag_data_buffer_t* buffer);
// 数据的工厂初始化函数
typedef bool (*tag_datas_factory_t)(uint8_t slot, tag_specific_type_t type);

// 标签的数据加载与保存事件的回调函数映射表
typedef struct {
    tag_sense_type_t     sense_type;
    tag_specific_type_t  tag_type;
    tag_datas_loadcb_t   data_on_load;
    tag_datas_savecb_t   data_on_save;
    tag_datas_factory_t  data_factory;
    tag_data_buffer_t    *data_buffer;
} tag_base_handler_map_t;

/**
 * 卡槽内模拟的卡的类型之类的参数的存放配置
 * 此配置可以被持久化保存到Flash
 * 4字节一个Word，谨记进行整字对齐
 */
typedef struct ALIGN_U32 {
    // 基础配置
    struct {
        uint8_t activated;      // 当前激活了哪个卡槽（哪个卡槽被使用了）
        uint8_t reserved1;      // 保留
        uint8_t reserved2;      // 保留
        uint8_t reserved3;      // 保留
    } config;
    // 每个卡槽自身的配置
    struct {
        // 基础配置，占用两个字节
        uint8_t enable: 1;      // 是否使能该卡槽
        uint8_t reserved1: 7;   // 保留
        uint8_t reserved2;      // 保留
        // 具体的正在模拟卡的类型
        tag_specific_type_t tag_hf;
        tag_specific_type_t tag_lf;
    } group[TAG_MAX_SLOT_NUM];
} tag_slot_config_t;


// 最基本的模拟卡初始化程序
void tag_emulation_init(void);
// 标签的一些存放在RAM中的数据可以通过此接口持久化保存到flash
void tag_emulation_save(void);

// 模拟卡的启动与结束
void tag_emulation_sense_run(void);
void tag_emulation_sense_end(void);

// 场感应使能状态切换封装函数
void tag_emulation_sense_switch(tag_sense_type_t type, bool enable);
// 删除卡槽中指定的场类型的卡片
void tag_emulation_delete_data(uint8_t slot, tag_sense_type_t sense_type);
// 将指定卡槽初始化为指定类型的卡片的出厂数据
bool tag_emulation_factory_data(uint8_t slot, tag_specific_type_t tag_type);
// 更改正在模拟的卡片的类型
void tag_emulation_change_type(uint8_t slot, tag_specific_type_t tag_type);
// 从内存中加载数据到模拟卡缓冲区
bool tag_emulation_load_by_buffer(tag_specific_type_t tag_type, bool update_crc);

tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type);
tag_data_buffer_t* get_buffer_by_tag_type(tag_specific_type_t type);

// 设置当前使用的卡槽
void tag_emulation_set_slot(uint8_t index);
// 获取当前使用的卡槽
uint8_t tag_emulation_get_slot(void);
// 切换卡槽，根据传入参数控制是否在切换期间关闭场监听
void tag_emulation_change_slot(uint8_t index, bool sense_disable);
// 获取卡槽使能状态
bool tag_emulation_slot_is_enable(uint8_t slot);
// 设置卡槽使能
void tag_emulation_slot_set_enable(uint8_t slot, bool enable);
// 获取对应卡槽的模拟卡类型
void tag_emulation_get_specific_type_by_slot(uint8_t slot, tag_specific_type_t tag_type[2]);
// 初始化某些出厂数据
void tag_emulation_factory_init(void);

// 在某个方向上查询任何一个使能的卡槽
uint8_t tag_emulation_slot_find_next(uint8_t slot_now);
uint8_t tag_emulation_slot_find_prev(uint8_t slot_now);

#endif
