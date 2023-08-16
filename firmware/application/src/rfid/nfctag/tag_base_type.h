#ifndef TAG_BASE_TYPE_H
#define TAG_BASE_TYPE_H


// 场的感应类型
typedef enum  {
    // 无场感应
    TAG_SENSE_NO,
    // 低频125khz场感应
    TAG_SENSE_LF,
    // 高频13.56mhz场感应
    TAG_SENSE_HF,
} tag_sense_type_t;

/**
 *
 * 所有支持模拟的标签的类型的定义
 * 注意，以下所有定义的标签类型，都是应用层的细化的具体的类型统计
 * 不再区分高低频
 */
typedef enum {
    // 特定的且必须存在的标志不存在的类型
    TAG_TYPE_UNKNOWN,
    // 125khz（ID卡）系列
    TAG_TYPE_EM410X      = 0x10000,
    // Mifare系列
    TAG_TYPE_MIFARE_Mini = 0x20000,
    TAG_TYPE_MIFARE_1024,
    TAG_TYPE_MIFARE_2048,
    TAG_TYPE_MIFARE_4096,
    // NTAG系列
    TAG_TYPE_NTAG_213    = 0x20100,
    TAG_TYPE_NTAG_215,
    TAG_TYPE_NTAG_216,
} tag_specific_type_t;


#endif
