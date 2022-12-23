#ifndef NFC_MF1_H
#define NFC_MF1_H

#include "nfc_14a.h"


#define NFC_TAG_MF1_DATA_SIZE   16
#define NFC_TAG_MF1_FRAME_SIZE  (NFC_TAG_MF1_DATA_SIZE + NFC_TAG_14A_CRC_LENGTH)


// mf1标签写入模式
typedef enum {
    NFC_TAG_MF1_WRITE_NORMAL    = 0u,
    NFC_TAG_MF1_WRITE_DENIED    = 1u,
    NFC_TAG_MF1_WRITE_DECEIVE   = 2u,
    NFC_TAG_MF1_WRITE_SHADOW    = 3u,
} nfc_tag_mf1_write_mode_t;

// mf1标签gen1a模式状态机
typedef enum {
    GEN1A_STATE_DISABLE,
    GEN1A_STATE_UNLOCKING,
    GEN1A_STATE_UNLOCKED_RW_WAIT,
    GEN1A_STATE_WRITING,
} nfc_tag_mf1_gen1a_state_machine_t;

// mf1标签标准模式状态机
typedef enum {
    // 验证状态机
    MF1_STATE_UNAUTH,
    MF1_STATE_AUTHING,
    MF1_STATE_AUTHED,
    
    // 操作状态机
    MF1_STATE_WRITE,
    MF1_STATE_INCREMENT,
    MF1_STATE_DECREMENT,
    MF1_STATE_RESTORE
} nfc_tag_mf1_std_state_machine_t;

// mf1配置
typedef struct {
    /**
     *  正常写入模式（根据当前的状态去正常写入，受控制位和后门卡影响）
     *  拒绝写入模式（类似控制位锁死，直接拒绝任何写入，返回nack）
     *  欺诈写入模式（表面上返回ack表示写入成功，其实连RAM都不写入）
     *  影子写入模式（写入到RAM里面，并且返回ack表示成功，但是不保存到flash里面）
     *  @see nfc_tag_mf1_write_mode_t
     */
    nfc_tag_mf1_write_mode_t mode_block_write;
    /**
     * 互通模式，如果启用了互通模式，将会使用m1扇区数据中的部分信息
     * 否则将会使用防冲撞阶段单独定义的信息，此设置仅限4字节 NFC_TAG_14A_UID_SINGLE_SIZE 的卡有效
     * 除非有任何文档表明7字节和10字节的卡的0块有相关SAK的规范说明
     */
    uint8_t use_mf1_coll_res: 1;
    /**
     * Chinese Gen1A 后门卡模式，此模式权限最高
     * 开启后将响应后门卡操作指令，并且所有的操作直接放行，不受 mode_block_write 和控制位影响
     */
    uint8_t mode_gen1a_magic: 1;
    /**
     * 使能侦测，将自动记录mf1的验证日志
     */
    uint8_t detection_enable: 1;
    // 保留
    uint8_t reserved1: 5;
    uint8_t reserved2;
    uint8_t reserved3;
} nfc_tag_mf1_configure_t;

/*
 * mf1标签信息结构，谨记进行4字节对齐
 * 如果不进行字节对齐，在直接将此结构体申明并且保存到flash时将发生访问越界的异常
 */
typedef struct __attribute__((aligned(4))) {
    nfc_tag_14a_coll_res_entity_t res_coll;
    nfc_tag_mf1_configure_t config;
    uint8_t memory[256][16];
} nfc_tag_mf1_information_t;

// 4Byte卡片的出厂固化的0块结构
typedef struct {
    // 例如：
    // 30928E04 28 08 0400 0177A2CC35AFA51D
    uint8_t uid[4];
    uint8_t bcc[1];
    uint8_t sak[1];
    uint8_t atqa[2];
    uint8_t manufacturer[8];
} nfc_tag_mf1_factory_info_t;

// 通用的mf1扇区尾部块数据结构
typedef struct {
    uint8_t keya[6];    // 秘钥A
    uint8_t acs[4];     // 控制位
    uint8_t keyb[6];    // 秘钥B
} nfc_tag_mf1_trailer_info_t;

// 专用于mifare通信的发送缓冲区
typedef struct {
    // 原始buffer，用于承载任何未经加密的指令
    uint8_t tx_raw_buffer[NFC_TAG_MF1_FRAME_SIZE];
    // 经过crypto1加密后，每个字节的奇偶校验位
    uint8_t tx_bit_parity[NFC_TAG_MF1_FRAME_SIZE];
    // 用于承载crypto1加密后的数据与parity合并之后的数据
    // The maximum frame length is 163 bits (16 data bytes + 2 CRC bytes = 16 × 9 + 2 × 9 + 1 start bit).
    uint8_t tx_warp_frame[21];
    // 打包之后的数据的长度，根据上面的消息可知，mf1通信的最大bit数量不超过163个，
    // 因此一个字节足够存放长度值
    uint8_t tx_frame_bit_size;
} nfc_tag_mf1_tx_buffer_t;

// mf1标签验证历史记录
typedef struct {
    // 验证的基础信息
    struct {
        uint8_t block;
        uint8_t is_keyb: 1;
        uint8_t is_nested: 1;
        // 空域，占位置用的
        uint8_t : 6;
    } cmd;
    // mfkey32必要参数
    uint8_t uid[4];
    uint8_t nt[4];
    uint8_t nr[4];
    uint8_t ar[4];
} nfc_tag_mf1_auth_log_t;


nfc_tag_mf1_auth_log_t* get_mf1_auth_log(uint32_t* count);
int nfc_tag_mf1_data_loadcb(tag_specific_type_t type, tag_data_buffer_t* buffer);
int nfc_tag_mf1_data_savecb(tag_specific_type_t type, tag_data_buffer_t* buffer);
bool nfc_tag_mf1_data_factory(uint8_t slot, tag_specific_type_t tag_type);
void nfc_tag_mf1_set_detection_enable(bool enable);
bool nfc_tag_mf1_is_detection_enable(void);
void nfc_tag_mf1_detection_log_clear(void);
uint32_t nfc_tag_mf1_detection_log_count(void);

#endif
