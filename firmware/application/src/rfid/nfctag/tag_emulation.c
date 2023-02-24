#include "crc_utils.h"
#include "nfc_14a.h"
#include "lf_tag_em.h"
#include "nfc_mf1.h"
#include "fds_util.h"
#include "tag_emulation.h"
#include "tag_persistence.h"


#define NRF_LOG_MODULE_NAME tag_emu
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


/*
 * 一个卡槽最多可以同时模拟两种卡，一张ID 125khz em410x，一张IC 13.56mhz 14a。（以后可能可以支持更多）
 * 启动的时候，应当按需启动该启动的场监听器（无数据加载时可不模拟卡，但是需要按需监听场的状态）
 * 如果检索到的卡槽的配置拥有指定的类型的卡片，那么应当进行指定类型的数据的加载，初始化必要的参数
 * 检测到场入场出时，除了需要对相关的LED进行操作外，还需要根据当前数据是否加载来开始模拟卡
 * 在模拟卡，所有的操作应当都是基于RAM中加载的数据进行，在模拟卡结束后，应当将修改的数据进行保存更新到flash
 * 
 *
 *
 * ......
 */

// 标志当前是否在模拟卡中
bool g_is_tag_emulating = false;


// **********************  可持久化参数开始 **********************

/**
 * 标签数据存在于flash中的信息，总长度必须要 4字节（整字）对齐！！！
 */
static uint8_t m_tag_data_buffer_lf[12];      // 低频卡数据缓冲区
static uint16_t m_tag_data_lf_crc;
static tag_data_buffer_t m_tag_data_lf = { sizeof(m_tag_data_buffer_lf), m_tag_data_buffer_lf, &m_tag_data_lf_crc };

static uint8_t m_tag_data_buffer_hf[4500];    // 高频卡数据缓冲区
static uint16_t m_tag_data_hf_crc;
static tag_data_buffer_t m_tag_data_hf = { sizeof(m_tag_data_buffer_hf), m_tag_data_buffer_hf, &m_tag_data_hf_crc };

/**
 * 八个卡槽，每个卡槽都有其特有的配置
 */
static tag_slot_config_t slotConfig ALIGN_U32 = {
    // 配置激活的卡槽，默认激活第0个卡槽（第一张卡）
    .config = { .activated = 0, .reserved1 = 0, .reserved2 = 0, .reserved3 = 0, },
    // 配置卡槽组
    .group = {
        { .enable = true,  .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_MIFARE_1024, .tag_lf = TAG_TYPE_EM410X, },    // 1
        { .enable = true,  .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_EM410X, },    // 2
        { .enable = true,  .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_MIFARE_1024, .tag_lf = TAG_TYPE_UNKNOWN, },   // 3
        { .enable = false, .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_UNKNOWN, },   // 4
        { .enable = false, .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_UNKNOWN, },   // 5
        { .enable = false, .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_UNKNOWN, },   // 6
        { .enable = false, .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_UNKNOWN, },   // 7
        { .enable = false, .reserved1 = 0, .reserved2 = 0, .tag_hf = TAG_TYPE_UNKNOWN,     .tag_lf = TAG_TYPE_UNKNOWN, },   // 8
    },
};
// 卡槽配置特有的CRC，一旦slot配置发生变动，可通过CRC检查出来
static uint16_t m_slot_config_crc;

// **********************  可持久化参数结束 **********************


/**
 * 标签的数据加载到RAM后回调通知的实现操作的映射表，
 * 映射结构为：
 *      场类型         细化的标签类型           加载数据成功后的通知回调      数据保存前的通知回调          初始化数据的实现函数          卡片数据的缓冲区
 */
static tag_base_handler_map_t tag_base_map[] = {
    // 低频ID卡模拟
    { TAG_SENSE_LF,    TAG_TYPE_EM410X,         lf_tag_em410x_data_loadcb,    lf_tag_em410x_data_savecb,    lf_tag_em410x_data_factory,    &m_tag_data_lf },
    // MF1标签模拟
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_Mini,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_1024,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_2048,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_4096,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    // NTAG标签模拟
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_213,       NULL,                         NULL,                         NULL,                          &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_215,       NULL,                         NULL,                         NULL,                          &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_216,       NULL,                         NULL,                         NULL,                          &m_tag_data_hf },
};


/**
 * 根据指定的细化标签类型，获得其处理加载的数据的实现函数
 */
static tag_datas_loadcb_t get_data_loadcb_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_on_load;
        }
    }
    return NULL;
}

/**
 * 根据指定的细化标签类型，获得其处数据保存前的操作函数
 */
static tag_datas_savecb_t get_data_savecb_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_on_save;
        }
    }
    return NULL;
}

/**
 * 根据指定的细化标签类型，获得其处数据工厂初始化的操作函数
 */
static tag_datas_factory_t get_data_factory_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_factory;
        }
    }
    return NULL;
}

/**
 * 根据指定的细化标签类型，获得其基础的场感应类型
 */
tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].sense_type;
        }
    }
    return TAG_SENSE_NO;
}

/**
 * 根据类型获取缓冲区信息
 */
tag_data_buffer_t* get_buffer_by_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_buffer;
        }
    }
    return NULL;
}

/**
* 从内存中加载数据到模拟卡数据之中
 */
bool tag_emulation_load_by_buffer(tag_specific_type_t tag_type, bool update_crc) {
    // 数据已经加载到缓冲区，接下来根据激活的卡槽的配置，
    // 将设定的模拟卡类型（高频卡, 低频卡）指向的场感应配备的BUFFER传递给其
    tag_datas_loadcb_t fn_loadcb = get_data_loadcb_from_tag_type(tag_type);
    if (fn_loadcb == NULL) {    // 确保有实现对应的加载过程
        NRF_LOG_INFO("Tag data loader no impl.");
        return false;
    }
    // 通知对应的实现，我们加载完成数据了
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    int length = fn_loadcb(tag_type, buffer);
    if (length > 0 && update_crc) {
        // 读取完成后，我们先保存一份当前数据的CRC，后面保存的时候可以作为变动对比的参考
        calc_14a_crc_lut(buffer->buffer, length, (uint8_t *)buffer->crc);
        return true;
    }
    return false;
}

/**
 * 根据类型加载数据
 */
static void load_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    // 可能该卡槽未启用该类型的标签的模拟，直接跳过加载此数据
    if (tag_type == TAG_TYPE_UNKNOWN) {
        return;
    }
    // 获取专用缓冲区信息
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        NRF_LOG_ERROR("No buffer valid!");
        return;
    }
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    // 获取专用卡槽FDS记录信息
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // 根据当前激活的卡槽的场类型，加载指定场的数据到缓冲区
    // 提示: 如果数据与buffer长度无法匹配，则可能是固件更新导致，这个时候就要将数据进行删除重建
    bool ret = fds_read_sync(map_info.id, map_info.key, buffer->length, buffer->buffer);
    if (false == ret) {
        NRF_LOG_INFO("Tag slot data no exists.");
        return;
    }
    ret = tag_emulation_load_by_buffer(tag_type, true);
    if (ret) {
        NRF_LOG_INFO("Load tag slot %d, type %d data done.", slot, tag_type);
    }
}

/**
 * 根据类型保存数据
 */
static void save_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    // 可能该卡槽未启用该类型的标签的模拟，直接跳过保存此数据
    if (tag_type == TAG_TYPE_UNKNOWN) {
        return;
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        NRF_LOG_ERROR("No buffer valid!");
        return;
    }
    // 获取用户要保存的数据的长度，这个长度不应该超过全局buffer的大小
    int data_byte_length = 0;
    tag_datas_savecb_t fn_savecb = get_data_savecb_from_tag_type(tag_type);
    if (fn_savecb == NULL) {        // 确保有实现保存过程
        NRF_LOG_INFO("Tag data saver no impl.");
        return;
    } else {
        data_byte_length = fn_savecb(tag_type, buffer);
    }
    // 确保需要保存数据，我们可以通过crc进行判断数据是否发生了变动
    if (data_byte_length <= 0) {
        NRF_LOG_INFO("Tag type %d data no save.", tag_type);
        return;
    }
    // 确保要保存的数据不大于目前的缓冲区大小
    if (data_byte_length > buffer->length) {
        NRF_LOG_ERROR("Tag data save length overflow.", tag_type);
        return;
    }
    uint16_t crc;
    calc_14a_crc_lut(buffer->buffer, data_byte_length, (uint8_t *)&crc);
    // 判断数据是否数据发生了变动
    if (crc == *buffer->crc) {
        NRF_LOG_INFO("Tag slot data no change, length = %d", data_byte_length);
        return;
    }
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    // 获取专用卡槽FDS记录信息
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // 计算要保存的数据的长度（自动填充整字）
    int data_word_length = (data_byte_length / 4) + (data_byte_length % 4 > 0 ? 1 : 0);
    // 调用堵塞式的fds写入函数，将卡槽指定场类型的数据写入到flash中
    bool ret = fds_write_sync(map_info.id, map_info.key, data_word_length, buffer->buffer);
    if (ret) {
        NRF_LOG_INFO("Save tag slot data success.");
    } else {
        NRF_LOG_ERROR("Save tag slot data error.");
    }
    // 保存完成之后，更新对应内存中的buffer的CRC
    *buffer->crc = crc;
}

/**
 * 根据类型删除数据
 */
static void delete_data_by_tag_type(uint8_t slot, tag_sense_type_t sense_type) {
    if (sense_type == TAG_SENSE_NO) {
        return;
    }
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int count = fds_delete_sync(map_info.id, map_info.key);
    NRF_LOG_INFO("Slot %d delete senese type %d data, record count: %d", slot, sense_type, count);
}

/**
 * 加载模拟卡卡片数据，注意，加载仅仅是数据操作，
 * 启动模拟卡请调用 tag_emulation_sense_run 函数，否则不会感应场事件
 */
void tag_emulation_load_data(void) {
    uint8_t slot = tag_emulation_get_slot();
    load_data_by_tag_type(slot, slotConfig.group[slot].tag_hf);
    load_data_by_tag_type(slot, slotConfig.group[slot].tag_lf);
}

/**
 * 保存模拟卡配置数据，在合适的时机，应当调用此函数进行数据的保存
 */
void tag_emulation_save_data(void) {
    uint8_t slot = tag_emulation_get_slot();
    save_data_by_tag_type(slot, slotConfig.group[slot].tag_hf);
    save_data_by_tag_type(slot, slotConfig.group[slot].tag_lf);
}

/**
 * @brief 获取模拟卡的标签类型，从对应卡槽中。
 * 
 * @param slot 卡槽
 * @param tag_type 标签类型
 */
void tag_emulation_get_specific_type_by_slot(uint8_t slot, tag_specific_type_t tag_type[2]) {
    tag_type[0] = slotConfig.group[slot].tag_hf;
    tag_type[1] = slotConfig.group[slot].tag_lf;
}

/**
 * 删除某个卡槽指定的场类型的数据，如果是当前的激活的卡槽的数据，我们还需要动态关闭此卡片的模拟
 */
void tag_emulation_delete_data(uint8_t slot, tag_sense_type_t sense_type) {
    // 删除数据
    delete_data_by_tag_type(slot, sense_type);
    // 关闭对应的卡槽的模拟卡类型
    switch(sense_type) {
        case TAG_SENSE_HF: {
            slotConfig.group[slot].tag_hf = TAG_TYPE_UNKNOWN;
        } break;
        case TAG_SENSE_LF: {
            slotConfig.group[slot].tag_lf = TAG_TYPE_UNKNOWN;
        } break;
        default:
            break;
    }
    // 如果删除的卡槽数据是当前激活的卡槽的（正在模拟），我们还需要进行动态关闭
    if (slotConfig.config.activated == slot) {
        tag_emulation_sense_switch(sense_type, false);
    }
    // 如果删除了之后，我们发现这个卡槽两个卡都没了，就得把这个卡槽关闭了。
    if (slotConfig.group[slot].tag_hf == TAG_TYPE_UNKNOWN && slotConfig.group[slot].tag_lf == TAG_TYPE_UNKNOWN) {
        slotConfig.group[slot].enable = false;
    }
}

/**
 * 将某个卡槽的数据设置为出厂的预置数据
 */
bool tag_emulation_factory_data(uint8_t slot, tag_specific_type_t tag_type) {
    tag_datas_factory_t factory = get_data_factory_from_tag_type(tag_type);
    if (factory != NULL) {
        // 执行工厂格式化数据的过程！
        if (factory(slot, tag_type)) {
            // 如果当前设置的初始数据卡槽号是当前激活的卡槽，那么我们需要更新到内存中
            if (tag_emulation_get_slot() == slot) {
                load_data_by_tag_type(slot, tag_type);
            }
            return true;
        }
    }
    return false;
}

/**
 * 切换场感应监听状态
 * @param enable: 是否使能场感应
 */
static void tag_emulation_sense_switch_all(bool enable) {
    uint8_t slot = tag_emulation_get_slot();
    // NRF_LOG_INFO("Slot %d tag type hf %d, lf %d", slot, slotConfig.group[slot].tag_hf, slotConfig.group[slot].tag_lf);
    if (slotConfig.group[slot].tag_hf != TAG_TYPE_UNKNOWN) {
        nfc_tag_14a_sense_switch(enable);
    } else {
        nfc_tag_14a_sense_switch(false);
    }
    if (slotConfig.group[slot].tag_lf != TAG_TYPE_UNKNOWN) {
        lf_tag_125khz_sense_switch(enable);
    } else {
        lf_tag_125khz_sense_switch(false);
    }
}

/**
 * 切换场感应监听状态
 * @param type: 场感应类型
 * @param enable: 是否使能该类型的场感应
 */
void tag_emulation_sense_switch(tag_sense_type_t type, bool enable) {
    // 检查参数，不允许切换非正常场
    if (type == TAG_SENSE_NO) APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
    // 切换高频
    if (type == TAG_SENSE_HF) nfc_tag_14a_sense_switch(enable);
    // 切换低频
    if (type == TAG_SENSE_LF) lf_tag_125khz_sense_switch(enable);
}

/**
 * 加载模拟卡配置数据，注意，加载仅仅是卡槽配置
 */
void tag_emulation_load_config(void) {
    // 读取卡槽配置数据
    bool ret = fds_read_sync(FDS_CONFIG_RECORD_FILE_ID, FDS_CONFIG_RECORD_FILE_KEY, sizeof(slotConfig), (uint8_t *)&slotConfig);
    if (ret) {
        // 读取完成后，我们先保存一份当前配置的BCC，后面保存的时候可以作为变动对比的参考
        calc_14a_crc_lut((uint8_t *)&slotConfig, sizeof(slotConfig), (uint8_t *)&m_slot_config_crc);
        NRF_LOG_INFO("Load tag slot config done.");
    } else {
        NRF_LOG_INFO("Tag slot config no exists.");
    }
}

/**
 * 保存模拟卡配置数据
 */
void tag_emulation_save_config(void) {
    // 我们正在保存卡槽配置，需要先计算当前的卡槽配置的crc码，用于下面的数据是否更新的判断
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&slotConfig, sizeof(slotConfig), (uint8_t *)&new_calc_crc);
    if (new_calc_crc != m_slot_config_crc) {    // 在保存之前，先确保卡槽配置有变动了
        NRF_LOG_INFO("Save tag slot config start.");
        bool ret = fds_write_sync(FDS_CONFIG_RECORD_FILE_ID, FDS_CONFIG_RECORD_FILE_KEY, sizeof(slotConfig) / 4, (uint8_t *)&slotConfig);
        if (ret) {
            NRF_LOG_INFO("Save tag slot config success.");
        } else {
            NRF_LOG_ERROR("Save tag slot config error.");
        }
    } else {
        NRF_LOG_INFO("Tag slot config no change.");
    }
}

/**
 * 启动标签模拟
 */
void tag_emulation_sense_run(void) {
    tag_emulation_sense_switch_all(true);
}

/**
 * 停止标签模拟，注意，此函数会绝对屏蔽NFC相关的事件，包括唤醒MCU
 * 如果需要休眠MCU后依旧能通过NFC唤醒，请勿调用此函数
 */
void tag_emulation_sense_end(void) {
    TAG_FIELD_LED_OFF();
    tag_emulation_sense_switch_all(false);
}

/**
 * 初始化标签模拟
 */
void tag_emulation_init(void) {
    tag_emulation_load_config();    // 加载模拟卡的卡槽的配置
    tag_emulation_load_data();      // 加载模拟卡的数据
}

/**
 * 保存标签的数据（从RAM中写入到flash）
 */
void tag_emulation_save(void) {
    tag_emulation_save_config();    // 保存卡槽配置
    tag_emulation_save_data();      // 保存卡槽数据
}

/**
 * 获取当前激活的卡槽索引
 */
uint8_t tag_emulation_get_slot(void) {
    return slotConfig.config.activated;
}

/**
 * 设置当前激活的卡槽索引
 */
void tag_emulation_set_slot(uint8_t index) {
    slotConfig.config.activated = index;    // 重设到新切换的卡槽上
}

/**
 * 切换到指定索引的卡槽上，此函数将自动完成数据加载
 */
void tag_emulation_change_slot(uint8_t index, bool sense_disable) {
    if (sense_disable) {
        // 关闭模拟卡，避免切换卡槽的时候触发模拟
        tag_emulation_sense_end();
    }
    tag_emulation_save_data();      // 保存当前卡片的数据，如果有变动的情况下
    g_is_tag_emulating = false;     // 重设标志位
    tag_emulation_set_slot(index);  // 更新激活的卡槽的索引
    tag_emulation_load_data();      // 然后重新加载卡槽的数据
    if (sense_disable) {
        // 根据新的卡槽的配置，我们更新场的监听状态
        tag_emulation_sense_run();
    }
}

/**
 * 判断指定卡槽是否启用了
 */
bool tag_emulation_slot_is_enable(uint8_t slot) {
    // 直接返回对应卡槽的使能状态
    return slotConfig.group[slot].enable;
}

/**
 * 设置指定卡槽是否启用
 */
void tag_emulation_slot_set_enable(uint8_t slot, bool enable) {
    // 直接设置对应卡槽的使能状态
    slotConfig.group[slot].enable = enable;
}

/**
 * 寻找下一个有效使能的卡槽
 */
uint8_t tag_emulation_slot_find_next(uint8_t slot_now) {
    uint8_t start_slot = (slot_now + 1 >= TAG_MAX_SLOT_NUM) ? 0 : slot_now + 1;
    for (uint8_t i = start_slot; i < sizeof(slotConfig.group);) {
        if (i == slot_now) return slot_now;         // 一次轮回之后没有发现其他被激活的卡槽
        if (slotConfig.group[i].enable) return i;   // 查看当前遍历的卡槽是否使能，使能则认定当前卡槽为有效使能的卡槽
        if (i + 1 >= TAG_MAX_SLOT_NUM) {            // 继续下一个轮回
            i = 0;
        } else {
            i += 1;
        }
    }
    return slot_now;    // 无法搜索到的情况下默认返回传入的指定的返回值
}

/**
 * 寻找上一个有效使能的卡槽
 */
uint8_t tag_emulation_slot_find_prev(uint8_t slot_now) {
    uint8_t start_slot = (slot_now - 1 < 0) ? (TAG_MAX_SLOT_NUM - 1) : slot_now - 1;
    for (uint8_t i = start_slot; i < sizeof(slotConfig.group);) {
        if (i == slot_now) return slot_now;         // 一次轮回之后没有发现其他被激活的卡槽
        if (slotConfig.group[i].enable) return i;   // 查看当前遍历的卡槽是否使能，使能则认定当前卡槽为有效使能的卡槽
        if (i - 1 < 0) {    // 继续下一个轮回
            i = (TAG_MAX_SLOT_NUM - 1); 
        } else {
            i -= 1;
        }
    }
    return slot_now;    // 无法搜索到的情况下默认返回传入的指定的返回值
}

/**
 * 将指定的卡槽的卡槽指定的场类型的卡设置为指定的类型
 */
void tag_emulation_change_type(uint8_t slot, tag_specific_type_t tag_type) {
    tag_sense_type_t sense_type =  get_sense_type_from_tag_type(tag_type);
    NRF_LOG_INFO("sense type = %d", sense_type);
    switch (sense_type) {
        case TAG_SENSE_LF: {
            slotConfig.group[slot].tag_lf = tag_type;
            break;
        }
        case TAG_SENSE_HF: {
            slotConfig.group[slot].tag_hf = tag_type;
            break;
        }
        default: break; // 永远不能发生
    }
    NRF_LOG_INFO("tag type = %d", tag_type);
    // 更新完成之后，我们需要通知更新内存中的相关数据
    if (sense_type != TAG_SENSE_NO) {
        load_data_by_tag_type(slot, tag_type);
        NRF_LOG_INFO("reload data success.");
    }
}
