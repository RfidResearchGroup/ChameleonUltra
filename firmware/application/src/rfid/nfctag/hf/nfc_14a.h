#ifndef NFC_14A_H
#define NFC_14A_H

#include "tag_emulation.h"

#define MAX_NFC_RX_BUFFER_SIZE  64
#define MAX_NFC_TX_BUFFER_SIZE  64

#define NFC_TAG_14A_CRC_LENGTH  2

// 是否使能自动移除奇偶校验位（硬件移除）
#define NFC_TAG_14A_RX_PARITY_AUTO_DEL_ENABLE  0

#define NFC_TAG_14A_CASCADE_CT  0x88

#define NFC_TAG_14A_CMD_REQA    0x26
#define NFC_TAG_14A_CMD_WUPA    0x52
#define NFC_TAG_14A_CMD_HALT    0x50
#define NFC_TAG_14A_CMD_RATS    0xE0

#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_1  0x93
#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_2  0x95
#define NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_3  0x97


// TBV = Transfer Buffer Valid
// TBIV = Transfer Buffer Invalid
#define ACK_NAK_FRAME_SIZE          4         /* Bits */
#define ACK_VALUE                   0x0A
#define NAK_INVALID_OPERATION_TBV   0x00    // 这个不常用
#define NAK_CRC_PARITY_ERROR_TBV    0x01    // 这个不常用
#define NAK_INVALID_OPERATION_TBIV  0x04
#define NAK_CRC_PARITY_ERROR_TBIV   0x05
#define NAK_OTHER_ERROR             0x06    // 这个不在手册中定义，属于变色龙特有（可能需要扇区）


// ISO14443-A 通用状态机
typedef enum {
    NFC_TAG_STATE_14A_IDLE,     // 空闲状态，可等待任何指令
    NFC_TAG_STATE_14A_READY,    // 选卡状态，当前在进行标准的14A防冲撞
    NFC_TAG_STATE_14A_ACTIVE,   // 选卡或者其他指令使其进入工作状态，可接收处理所有的数据
    NFC_TAG_STATE_14A_HALTED,   // 标签中止工作状态，只能由halt或者其他特殊指令（非标）唤醒
} nfc_tag_14a_state_t;

// 枚举规范内的长度的UID
typedef enum {
    NFC_TAG_14A_UID_SINGLE_SIZE = 4u,   ///< Length of single-size NFCID1.
    NFC_TAG_14A_UID_DOUBLE_SIZE = 7u,   ///< Length of double-size NFCID1.
    NFC_TAG_14A_UID_TRIPLE_SIZE = 10u,  ///< Length of triple-size NFCID1.
} nfc_tag_14a_uid_size;

// 枚举规范内的级联等级
typedef enum {
    NFC_TAG_14A_CASCADE_LEVEL_1,
    NFC_TAG_14A_CASCADE_LEVEL_2,
    NFC_TAG_14A_CASCADE_LEVEL_3,
} nfc_tag_14a_cascade_level_t;

// ats封装结构体
typedef struct {
    uint8_t data[0xFF];
    uint8_t length;
} nfc_14a_ats_t;

// 基于bit的防冲撞需要用上的资源实体，占用空间大
typedef struct {
    nfc_tag_14a_uid_size size;          // uid的长度
    uint8_t atqa[2];                    // atqa
    uint8_t sak[1];                     // sak
    uint8_t uid[10];                    // uid，最大十个字节
    nfc_14a_ats_t ats;
} nfc_tag_14a_coll_res_entity_t;

// 防冲突资源的封装引用，纯引用空间占用比较小
typedef struct {
    nfc_tag_14a_uid_size *size;
    uint8_t *atqa;
    uint8_t *sak;
    uint8_t *uid;
    nfc_14a_ats_t *ats;
} nfc_tag_14a_coll_res_referen_t;

// 通信接管需要实现的回调函数
typedef void (*nfc_tag_14a_reset_handler_t)(void);
typedef void (*nfc_tag_14a_state_handler_t)(uint8_t *data, uint16_t szBits);
typedef nfc_tag_14a_coll_res_referen_t *(*nfc_tag_14a_coll_handler_t)(void);

// 14a通信接管者需要实现的接口
typedef struct {
    nfc_tag_14a_reset_handler_t cb_reset;
    nfc_tag_14a_state_handler_t cb_state;
    nfc_tag_14a_coll_handler_t get_coll_res;
} nfc_tag_14a_handler_t;

// 异或校验码
void nfc_tag_14a_create_bcc(uint8_t *pbtData, size_t szLen, uint8_t *pbtBcc);
void nfc_tag_14a_append_bcc(uint8_t *pbtData, size_t szLen);

// 14a循环冗余校验码
void nfc_tag_14a_append_crc(uint8_t *pbtData, size_t szLen);
bool nfc_tag_14a_checks_crc(uint8_t *pbtData, size_t szLen);

// 14a帧组解
uint8_t nfc_tag_14a_wrap_frame(const uint8_t *pbtTx, const size_t szTxBits, const uint8_t *pbtTxPar, uint8_t *pbtFrame);
uint8_t nfc_tag_14a_unwrap_frame(const uint8_t *pbtFrame, const size_t szFrameBits, uint8_t *pbtRx, uint8_t *pbtRxPar);

// 14a通信控制
void nfc_tag_14a_sense_switch(bool enable);
void nfc_tag_14a_set_handler(nfc_tag_14a_handler_t *handler);
void nfc_tag_14a_set_state(nfc_tag_14a_state_t state);
void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool appendCrc);
void nfc_tag_14a_tx_bytes_delay_freerun(uint8_t *data, uint32_t bytes, bool appendCrc);
void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits);
void nfc_tag_14a_tx_nbit_delay_window(uint8_t data, uint32_t bits);
void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits);

// 判断是否是有效的uid长度
bool is_valid_uid_size(uint8_t uid_length);

#endif
