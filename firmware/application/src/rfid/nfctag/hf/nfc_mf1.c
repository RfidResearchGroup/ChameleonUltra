#include <stdlib.h>

#include "nfc_mf1.h"
#include "nfc_14a.h"
#include "hex_utils.h"
#include "fds_util.h"
#include "tag_persistence.h"

#ifdef NFC_MF1_FAST_SIM
#include "mf1_crypto1.h"
#else
#include "crypto1_helper.h"
#endif

#define NRF_LOG_MODULE_NAME tag_mf1
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define MEM_KEY_A_OFFSET            48        /* Bytes */
#define MEM_KEY_B_OFFSET            58        /* Bytes */
#define MEM_KEY_BIGSECTOR_OFFSET    192
#define MEM_KEY_SIZE                6        /* Bytes */
#define MEM_ACC_GPB_SIZE            4        /* Bytes */
#define MEM_SECTOR_ADDR_MASK        0xFC
#define MEM_BIGSECTOR_ADDR_MASK     0xF0
#define MEM_BYTES_PER_BLOCK         16        /* Bytes */
#define MEM_VALUE_SIZE              4       /* Bytes */

/* NXP Originality check */
/* Sector 18/Block 68..71 is used to store signature data for NXP originality check */
#define MEM_EV1_SIGNATURE_BLOCK     68
#define MEM_EV1_SIGNATURE_TRAILOR   ((MEM_EV1_SIGNATURE_BLOCK + 3 ) * MEM_BYTES_PER_BLOCK)


#define CMD_AUTH_A                  0x60
#define CMD_AUTH_B                  0x61
#define CMD_AUTH_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_AUTH_RB_FRAME_SIZE      4         /* Bytes */
#define CMD_AUTH_AB_FRAME_SIZE      8         /* Bytes */
#define CMD_AUTH_BA_FRAME_SIZE      4         /* Bytes */
#define CMD_HALT                    0x50
#define CMD_HALT_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_READ                    0x30
#define CMD_READ_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_READ_RESPONSE_FRAME_SIZE 16       /* Bytes without CRCA */
#define CMD_WRITE                   0xA0
#define CMD_WRITE_FRAME_SIZE        2         /* Bytes without CRCA */
#define CMD_DECREMENT               0xC0
#define CMD_DECREMENT_FRAME_SIZE    2         /* Bytes without CRCA */
#define CMD_INCREMENT               0xC1
#define CMD_INCREMENT_FRAME_SIZE    2         /* Bytes without CRCA */
#define CMD_RESTORE                 0xC2
#define CMD_RESTORE_FRAME_SIZE      2         /* Bytes without CRCA */
#define CMD_TRANSFER                0xB0
#define CMD_TRANSFER_FRAME_SIZE     2         /* Bytes without CRCA */

#define CMD_CHINESE_UNLOCK          0x40
#define CMD_CHINESE_WIPE            0x41
#define CMD_CHINESE_UNLOCK_RW       0x43

/*
Source: NXP: MF1S50YYX Product data sheet

Access conditions for the sector trailer

Access bits     Access condition for                   Remark
            KEYA         Access bits  KEYB
C1 C2 C3        read  write  read  write  read  write
0  0  0         never key A  key A never  key A key A  Key B may be read[1]
0  1  0         never never  key A never  key A never  Key B may be read[1]
1  0  0         never key B  keyA|B never never key B
1  1  0         never never  keyA|B never never never
0  0  1         never key A  key A  key A key A key A  Key B may be read,
                                                       transport configuration[1]
0  1  1         never key B  keyA|B key B never key B
1  0  1         never never  keyA|B key B never never
1  1  1         never never  keyA|B never never never

[1] For this access condition key B is readable and may be used for data
*/
#define ACC_TRAILOR_READ_KEYA   0x01
#define ACC_TRAILOR_WRITE_KEYA  0x02
#define ACC_TRAILOR_READ_ACC    0x04
#define ACC_TRAILOR_WRITE_ACC   0x08
#define ACC_TRAILOR_READ_KEYB   0x10
#define ACC_TRAILOR_WRITE_KEYB  0x20



/*
Access conditions for data blocks
Access bits Access condition for                 Application
C1 C2 C3     read     write     increment     decrement,
                                                transfer,
                                                restore

0 0 0         key A|B key A|B key A|B     key A|B     transport configuration
0 1 0         key A|B never     never         never         read/write block
1 0 0         key A|B key B     never         never         read/write block
1 1 0         key A|B key B     key B         key A|B     value block
0 0 1         key A|B never     never         key A|B     value block
0 1 1         key B     key B     never         never         read/write block
1 0 1         key B     never     never         never         read/write block
1 1 1         never     never     never         never         read/write block

*/
#define ACC_BLOCK_READ      0x01
#define ACC_BLOCK_WRITE     0x02
#define ACC_BLOCK_INCREMENT 0x04
#define ACC_BLOCK_DECREMENT 0x08

#define KEY_A 0
#define KEY_B 1


/* Decoding table for Access conditions of the sector trailor */
static const uint8_t abTrailorAccessConditions[8][2] = {
    /* 0  0  0 RdKA:never WrKA:key A  RdAcc:key A WrAcc:never  RdKB:key A WrKB:key A      Key B may be read[1] */
    {
        /* Access with Key A */
        ACC_TRAILOR_WRITE_KEYA | ACC_TRAILOR_READ_ACC | ACC_TRAILOR_WRITE_ACC | ACC_TRAILOR_READ_KEYB | ACC_TRAILOR_WRITE_KEYB,
        /* Access with Key B */
        0
    },
    /* 1  0  0 RdKA:never WrKA:key B  RdAcc:keyA|B WrAcc:never RdKB:never WrKB:key B */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC,
        /* Access with Key B */
        ACC_TRAILOR_WRITE_KEYA | ACC_TRAILOR_READ_ACC |  ACC_TRAILOR_WRITE_KEYB
    },
    /* 0  1  0 RdKA:never WrKA:never  RdAcc:key A WrAcc:never  RdKB:key A WrKB:never  Key B may be read[1] */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC | ACC_TRAILOR_READ_KEYB,
        /* Access with Key B */
        0
    },
    /* 1  1  0         never never  keyA|B never never never */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC,
        /* Access with Key B */
        ACC_TRAILOR_READ_ACC
    },
    /* 0  0  1         never key A  key A  key A key A key A  Key B may be read,transport configuration[1] */
    {
        /* Access with Key A */
        ACC_TRAILOR_WRITE_KEYA | ACC_TRAILOR_READ_ACC | ACC_TRAILOR_WRITE_ACC | ACC_TRAILOR_READ_KEYB | ACC_TRAILOR_WRITE_KEYB,
        /* Access with Key B */
        0
    },
    /* 0  1  1         never key B  keyA|B key B never key B */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC,
        /* Access with Key B */
        ACC_TRAILOR_WRITE_KEYA | ACC_TRAILOR_READ_ACC | ACC_TRAILOR_WRITE_ACC | ACC_TRAILOR_WRITE_KEYB
    },
    /* 1  0  1         never never  keyA|B key B never never */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC,
        /* Access with Key B */
        ACC_TRAILOR_READ_ACC | ACC_TRAILOR_WRITE_ACC
    },
    /* 1  1  1         never never  keyA|B never never never */
    {
        /* Access with Key A */
        ACC_TRAILOR_READ_ACC,
        /* Access with Key B */
        ACC_TRAILOR_READ_ACC
    },
};

// 保存当前的MF1标准状态
static nfc_tag_mf1_std_state_machine_t m_mf1_state = MF1_STATE_UNAUTH;
// 保存当前的GEN1A状态
static nfc_tag_mf1_gen1a_state_machine_t m_gen1a_state = GEN1A_STATE_DISABLE;
// 指向标签信息的数据结构指针
static nfc_tag_mf1_information_t* m_tag_information = NULL;
// 定义并且使用影子防冲撞资源
static nfc_tag_14a_coll_res_referen_t m_shadow_coll_res;
// 指向标签扇区中的尾部块（控制数据块）
static nfc_tag_mf1_trailer_info_t* m_tag_trailer_info = NULL;
// 定义并且使用mf1专用通信缓冲区
static nfc_tag_mf1_tx_buffer_t m_tag_tx_buffer;
// 保存当前正在模拟的MF1的具体类型
static tag_specific_type_t m_tag_type;

// Fast simulate is enable, we use internal crypto1 instance from 'mf1_crypto1.c'
#ifndef NFC_MF1_FAST_SIM
// mifare classic crypto1
static struct Crypto1State mpcs = {0, 0};
static struct Crypto1State *pcs = &mpcs;
#endif

// 定义指向存放侦测的数据的buffer
// 将此数据放置在休眠保留的RAM中，以节约写入到Flash的时间和空间
#define MF1_AUTH_LOG_MAX_SIZE   1000
static __attribute__((section(".noinit"))) struct nfc_tag_mf1_auth_log_buffer {
    uint32_t count;
    nfc_tag_mf1_auth_log_t logs[MF1_AUTH_LOG_MAX_SIZE];
} m_auth_log;

static uint8_t CardResponse[4];
static uint8_t ReaderResponse[4];
static uint8_t CurrentAddress;
static uint8_t KeyInUse;
static uint8_t m_data_block_buffer[MEM_BYTES_PER_BLOCK];

// MifareClassic crypto1 setup use fixed uid by cascade level
#define UID_BY_CASCADE_LEVEL (m_shadow_coll_res.uid + (*m_shadow_coll_res.size - NFC_TAG_14A_UID_SINGLE_SIZE))

#define BYTE_SWAP(x) (((uint8_t)(x)>>4)|((uint8_t)(x)<<4))
#define NO_ACCESS 0x07


/* decode Access conditions for a block */
uint8_t GetAccessCondition(uint8_t Block) {
    uint8_t  InvSAcc0;
    uint8_t  InvSAcc1;
    uint8_t  Acc0 = m_tag_trailer_info->acs[0];
    uint8_t  Acc1 = m_tag_trailer_info->acs[1];
    uint8_t  Acc2 = m_tag_trailer_info->acs[2];
    uint8_t  ResultForBlock = 0;

    InvSAcc0 = ~BYTE_SWAP(Acc0);
    InvSAcc1 = ~BYTE_SWAP(Acc1);

    /* Check */
    if (((InvSAcc0 ^ Acc1) & 0xf0) ||    /* C1x */
            ((InvSAcc0 ^ Acc2) & 0x0f) ||   /* C2x */
            ((InvSAcc1 ^ Acc2) & 0xf0)) {   /* C3x */
        return (NO_ACCESS);
    }
    /* Fix for MFClassic 4K cards */
    if (Block < 128)
        Block &= 3;
    else {
        Block &= 15;
        if (Block & 15)
            Block = 3;
        else if (Block <= 4)
            Block = 0;
        else if (Block <= 9)
            Block = 1;
        else
            Block = 2;
    }

    Acc0 = ~Acc0;       /* C1x Bits to bit 0..3 */
    Acc1 =  Acc2;       /* C2x Bits to bit 0..3 */
    Acc2 =  Acc2 >> 4;  /* C3x Bits to bit 0..3 */

    if (Block) {
        Acc0 >>= Block;
        Acc1 >>= Block;
        Acc2 >>= Block;
    }
    /* combine the bits */
    ResultForBlock = ((Acc2 & 1) << 2) |
                     ((Acc1 & 1) << 1) |
                     (Acc0 & 1);
    return (ResultForBlock);
}

bool CheckValueIntegrity(uint8_t *Block) {
    // Value Blocks contain a value stored three times, with the middle portion inverted.
    if ((Block[0] == (uint8_t) ~Block[4]) && (Block[0] == Block[8])
            && (Block[1] == (uint8_t) ~Block[5]) && (Block[1] == Block[9])
            && (Block[2] == (uint8_t) ~Block[6]) && (Block[2] == Block[10])
            && (Block[3] == (uint8_t) ~Block[7]) && (Block[3] == Block[11])
            && (Block[12] == (uint8_t) ~Block[13])
            && (Block[12] == Block[14])
            && (Block[14] == (uint8_t) ~Block[15])) {
        return true;
    } else {
        return false;
    }
}

void ValueFromBlock(uint32_t *Value, uint8_t *Block) {
    *Value = 0;
    *Value |= ((uint32_t) Block[0] << 0);
    *Value |= ((uint32_t) Block[1] << 8);
    *Value |= ((uint32_t) Block[2] << 16);
    *Value |= ((uint32_t) Block[3] << 24);
}

void ValueToBlock(uint8_t *Block, uint32_t Value) {
    Block[0] = (uint8_t)(Value >> 0);
    Block[1] = (uint8_t)(Value >> 8);
    Block[2] = (uint8_t)(Value >> 16);
    Block[3] = (uint8_t)(Value >> 24);
    Block[4] = ~Block[0];
    Block[5] = ~Block[1];
    Block[6] = ~Block[2];
    Block[7] = ~Block[3];
    Block[8] = Block[0];
    Block[9] = Block[1];
    Block[10] = Block[2];
    Block[11] = Block[3];
}

/** @brief mf1获取一个随机数
 * @param nonce      随机数的Buffer
 */
void nfc_tag_mf1_random_nonce(uint8_t nonce[4], bool isNested) {
    // 使用rand进行快速产生随机数，性能损耗较小 
    // isNested provides more randomness for hardnested attack
    if (isNested) {
        nonce[0] = rand() & 0xff;
        nonce[1] = rand() & 0xff;
        nonce[2] = rand() & 0xff;
        nonce[3] = rand() & 0xff;
    } else {
        // fast for most readers
        num_to_bytes(rand(), 4, nonce);
    }
}

/** 
 * @brief mf1追加验证日志，步骤一，存放基础信息
 * @param isKeyB: 是否是在验证秘钥B
 * @param isNested: 是否是在进行嵌套验证
 * @param block: 当前正在验证的块
 * @param nonce: 明文随机数
 */
void append_mf1_auth_log_step1(bool isKeyB, bool isNested, uint8_t block, uint8_t *nonce) {
    // 首次上电，重置一下缓冲区信息
    if (m_auth_log.count == 0xFFFFFFFF) {
        m_auth_log.count = 0;
        NRF_LOG_INFO("Mifare Classic auth log buffer ready");
    }
    // 非首次上电，看一下是否记录侦测日志超过大小上限
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        // 超过上限直接跳过此操作。
        NRF_LOG_INFO("Mifare Classic auth log buffer overflow");
        return;
    }
    // 判断这个卡槽是否使能了侦测日志记录
    if (m_tag_information->config.detection_enable) {
        m_auth_log.logs[m_auth_log.count].cmd.is_keyb = isKeyB;
        m_auth_log.logs[m_auth_log.count].cmd.block = block;
        m_auth_log.logs[m_auth_log.count].cmd.is_nested = isNested;
        memcpy(m_auth_log.logs[m_auth_log.count].uid, UID_BY_CASCADE_LEVEL, 4);
        memcpy(m_auth_log.logs[m_auth_log.count].nt, nonce, 4);
    }
}

/** @brief mf1追加验证日志，步骤二，存放读头回应的加密信息
 * @param nr: 读卡器产生的，用秘钥加密的随机数
 * @param ar: 标签产生的，被读头加密的随机数
 */
void append_mf1_auth_log_step2(uint8_t *nr, uint8_t *ar) {
    // 判断到超过上限直接跳过此操作，避免覆盖之前的记录
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        return;
    }
    if (m_tag_information->config.detection_enable) {
        // 缓存加密信息
        memcpy(m_auth_log.logs[m_auth_log.count].nr, nr, 4);
        memcpy(m_auth_log.logs[m_auth_log.count].ar, ar, 4);
    }
}

/** @brief mf1追加验证日志，步骤三，存放最终验证成功或者失败的日志
 * 此步骤完成了最终的统计个数递增
 * @param is_auth_success: 是否验证成功
 */
void append_mf1_auth_log_step3(bool is_auth_success) {
    // 判断到超过上限直接跳过此操作，避免覆盖之前的记录
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        return;
    }
    if (m_tag_information->config.detection_enable) {
        // 然后就可以结束本次记录，统计数量递增
        m_auth_log.count += 1;
        // 打印一下当前记录的日志个数
        NRF_LOG_INFO("Auth log count: %d", m_auth_log.count);
    }
}

/** @brief mf1获得验证日志
 * @param count: 验证日志的统计个数
 */
nfc_tag_mf1_auth_log_t* get_mf1_auth_log(uint32_t* count) {
    // 先传递验证的日志条目总数出去
    *count = m_auth_log.count;
    // 直接返回日志数组的头部指针就好了
    return m_auth_log.logs;
}

static int get_block_max_by_tag_type(tag_specific_type_t tag_type) {
    int block_max;
    switch(tag_type) {
        case TAG_TYPE_MIFARE_Mini:
            block_max = 20;
            break;
        default:
        case TAG_TYPE_MIFARE_1024:
            block_max = 64;
            break;
        case TAG_TYPE_MIFARE_2048:
            block_max = 128;
            break;
        case TAG_TYPE_MIFARE_4096:
            block_max = 256;
            break;
    }
    return block_max;
}

static bool check_block_max_overflow(uint8_t block) {
    uint8_t block_max = get_block_max_by_tag_type(m_tag_type) - 1;
    return block > block_max;
}

#ifndef NFC_MF1_FAST_SIM
void mf1_prng_by_bytes(uint8_t *nonces, uint32_t n) {
    uint32_t nonces_u32 = bytes_to_num(nonces, 4);
    nonces_u32 = prng_successor(nonces_u32, n);
    num_to_bytes(nonces_u32, 4, nonces);
}
#endif

/** @brief mf1状态机
 * @param data      来自读头数据
 * @param szBits    数据的比特流长度
 * @param state     有限状态机
 */
void nfc_tag_mf1_state_handler(uint8_t* p_data, uint16_t szDataBits) {
    // 处理特殊指令，比如兼容mifare gen1a标签
    if (szDataBits <= 8) {
        // 只有启用了GEN1A模式的情况下才允许后门指令的响应
        if (m_tag_information->config.mode_gen1a_magic) {
            if (szDataBits == 7 && p_data[0] == CMD_CHINESE_UNLOCK) {
                // 第一步后门卡验证
                // NRF_LOG_INFO("MIFARE_MAGICWUPC1 received.\n");
                m_gen1a_state = GEN1A_STATE_UNLOCKING;
                nfc_tag_14a_tx_nbit_delay_window(ACK_VALUE, 4);
            } else if (szDataBits == 8 && p_data[0] == CMD_CHINESE_UNLOCK_RW) {
                // 第二部后门卡验证
                if (m_gen1a_state == GEN1A_STATE_UNLOCKING) {
                    // NRF_LOG_INFO("MIFARE_MAGICWUPC2 received.\n");
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_ACTIVE);    // 更新外部14A的状态机
                    m_gen1a_state = GEN1A_STATE_UNLOCKED_RW_WAIT;       // 更新GEN1A状态机
                    m_mf1_state = MF1_STATE_UNAUTH;                     // 更新MF1状态机
                    nfc_tag_14a_tx_nbit_delay_window(ACK_VALUE, 4);     // 回复读卡器gen1a标签解锁后门成功
#ifndef NFC_MF1_FAST_SIM
                    crypto1_deinit(pcs);                                // Reset crypto1 handler
#endif
                } else {
                    m_gen1a_state = GEN1A_STATE_DISABLE;                // 如果发现并没有走过第一步的话，直接重置gen1a状态机
                }
            }
        }
        // 记住，无论如何非字节帧都要在此处处理后直接结束
        // 万万不可将非字节帧转交给下方逻辑处理
        return;
    }

    // 处理mifare的状态机
    switch (m_mf1_state) {
        case MF1_STATE_UNAUTH: {    // 未验证状态，通信是开放性的
            if (szDataBits == 32) {    // 32位，可能是指令
                if (nfc_tag_14a_checks_crc(p_data, 4)) {
                    switch(p_data[0]) {
                        case CMD_AUTH_A:
                        case CMD_AUTH_B: {
                            uint8_t BlockAuth = p_data[1];
                            uint8_t CardNonce[4];
                            uint8_t BlockStart;
                            uint8_t BlockEnd;
                            
                            // 获得访问的块对应扇区的起始块，谨记：4K卡有大扇区，以16个block为一个扇区单位
                            // 计算思路：x = (y / n) * n，x = 扇区的起始块,y = 验证的块, n = y所在的扇区内的块的数量
                            // 思路解析：先做除法获取当前所在扇区，然后再做乘法获取所在扇区的块数量
                            if (BlockAuth >= 128) {
                                BlockStart = (BlockAuth / 16) * 16;
                                BlockEnd = BlockStart + 16 - 1;
                            } else {
                                // 非4K卡，以小扇区步进
                                BlockStart = (BlockAuth / 4) * 4;
                                BlockEnd = BlockStart + 4 - 1;
                            }
                            
                            // 当前模拟卡的类型，不足以支撑起读卡器的访问
                            if (check_block_max_overflow(BlockAuth)) {
                                break;
                            }

                            // 将KeyInUse设置为全局使用，以保留有关身份验证的信息
                            KeyInUse = p_data[0] & 1;
                            
                            // 获得指定的扇区访问控制字节，此处我们直接取巧，将内存转为结构体，让编译器帮我们维护指针的指向
                            m_tag_trailer_info = (nfc_tag_mf1_trailer_info_t*)m_tag_information->memory[BlockEnd];

                            // 生成随机数
                            nfc_tag_mf1_random_nonce(CardNonce, false);

                            // 根据卡随机数预先计算读卡器应答
                            for (uint8_t i = 0; i < sizeof(ReaderResponse); i++) {
                                ReaderResponse[i] = CardNonce[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(ReaderResponse, 64);
#else
                            mf1_prng_by_bytes(ReaderResponse, 64);
#endif

                            // 根据读卡器的应答预先计算我们的应答
                            for (uint8_t i = 0; i < sizeof(CardResponse); i++) {
                                CardResponse[i] = ReaderResponse[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(CardResponse, 32);
#else
                            mf1_prng_by_bytes(CardResponse, 32);
#endif

                            // 记录验证日志
                            append_mf1_auth_log_step1(KeyInUse, false, BlockAuth, CardNonce);
                            
                            // 使用随机卡随机数进行响应，并期望在下一帧中从读取器获得进一步的身份验证。
                            m_mf1_state = MF1_STATE_AUTHING;
                            
                            // 首次验证，回应一个明文的随机数，不带CRC
                            m_tag_tx_buffer.tx_raw_buffer[0] = CardNonce[0];
                            m_tag_tx_buffer.tx_raw_buffer[1] = CardNonce[1];
                            m_tag_tx_buffer.tx_raw_buffer[2] = CardNonce[2];
                            m_tag_tx_buffer.tx_raw_buffer[3] = CardNonce[3];

#ifdef NFC_MF1_FAST_SIM
                            Crypto1Setup(
                                // 根据当前的指令类型选择验证A或者B秘钥
                                KeyInUse ? m_tag_trailer_info->keyb : m_tag_trailer_info->keya,
                                // 传入当前使用的防冲撞的UID
                                UID_BY_CASCADE_LEVEL, 
                                // 传入一个明文的随机数，这个随机数将会被用于解密后续的通信
                                CardNonce
                            );
#else
                            // 设置crypto1密钥流，丢弃之前的加密状态
                            crypto1_deinit(pcs);
                            // 加载密钥流
                            crypto1_init(pcs, 
                                // 根据当前的指令类型选择验证A或者B秘钥
                                bytes_to_num(KeyInUse ? m_tag_trailer_info->keyb : m_tag_trailer_info->keya, 6)
                            );
                            // 设置密钥流
                            crypto1_word(pcs, bytes_to_num(UID_BY_CASCADE_LEVEL, 4) ^ bytes_to_num(CardNonce, 4), 0);
#endif
                            // 回应明文随机数给读卡器
                            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_raw_buffer, 4, false);
                            break;
                        }
                        case CMD_READ: {
                            // 在未验证的情况下收到了块相关的读指令，如果后门属于开启状态则直接允许读取
                            if (m_gen1a_state == GEN1A_STATE_UNLOCKED_RW_WAIT) {
                                CurrentAddress = p_data[1];
                                memcpy(m_tag_tx_buffer.tx_raw_buffer, m_tag_information->memory[CurrentAddress], NFC_TAG_MF1_DATA_SIZE);
                                nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_DATA_SIZE, true);
                            } else {
                                nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                            }
                            break;
                        }
                        case CMD_WRITE: {
                            // 解释同上
                            if (m_gen1a_state == GEN1A_STATE_UNLOCKED_RW_WAIT) {
                                // 保存要写入的块和更新状态机
                                CurrentAddress = p_data[1];
                                m_gen1a_state = GEN1A_STATE_WRITING;
                                // 响应ACK，让读头继续下一步发块数据过来
                                nfc_tag_14a_tx_nbit_delay_window(ACK_VALUE, 4);
                            } else {
                                nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                            }
                            break;
                        }
                        default: {
                            // 未验证状态下，除了在后门模式开启的时候直接读写卡
                            // 以及发起验证指令之外，其他的啥也干不了
                            nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                            break;
                        }
                    }
                } else {
                    // crc 校验异常
                    nfc_tag_14a_tx_nbit_delay_window(NAK_CRC_PARITY_ERROR_TBIV, 4);
                    return;
                }
            } else {
                if (szDataBits == 144 && m_gen1a_state == GEN1A_STATE_WRITING) {
                    // 判断到我们是在GEN1A模式下进行写入block操作
                    if (nfc_tag_14a_checks_crc(p_data, NFC_TAG_MF1_FRAME_SIZE)) {
                        // 数据校验通过，我们需要把发过来的数据放到RAM里面
                        memcpy(m_tag_information->memory[CurrentAddress], p_data, NFC_TAG_MF1_DATA_SIZE);
                        // 恢复GEN1A专用状态机为等待操作状态
                        m_gen1a_state = GEN1A_STATE_UNLOCKED_RW_WAIT;
                        // 回复读头ACK，完成写入操作
                        nfc_tag_14a_tx_nbit_delay_window(ACK_VALUE, 4);
                    } else {
                        // 传输过来的CRC校验异常，不能继续写入
                        nfc_tag_14a_tx_nbit_delay_window(NAK_CRC_PARITY_ERROR_TBIV, 4);
                    }
                } else {
                    // 在等待指令状态如果等到非4BYTE的指令则认为异常
                    // 此时需要重置状态机
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                }
            }
            break;            
        }

        case MF1_STATE_AUTHING: {
            if (szDataBits == 64) {
                // 拷贝读卡器回应的NR + AR
                append_mf1_auth_log_step2(p_data, &p_data[4]);
#ifdef NFC_MF1_FAST_SIM
                // Reader delivers an encrypted nonce. We use it to setup the crypto1 LFSR in nonlinear feedback mode. Furthermore it delivers an encrypted answer. Decrypt and check it
                Crypto1Auth(&p_data[0]);
                Crypto1ByteArray(&p_data[4], 4);
#else
                // NR,是读卡器生成的随机数
                uint32_t nr = bytes_to_num(p_data, 4);
                // AR,是卡片加密我们第一步回应的随机数的加密后的数据
                uint32_t ar = bytes_to_num(&p_data[4], 4);
                // --- crypto
                crypto1_word(pcs, nr, 1);
                num_to_bytes(ar ^ crypto1_word(pcs, 0, 0), 4, &p_data[4]);
#endif
                // 验证读卡器返回来的随机数是不是我们发送的
                if ((p_data[4] == ReaderResponse[0]) && (p_data[5] == ReaderResponse[1]) && (p_data[6] == ReaderResponse[2]) && (p_data[7] == ReaderResponse[3])) {
                    // 读取器已通过身份验证。加密预计算的卡应答数据并生成奇偶校验位。
                    m_tag_tx_buffer.tx_raw_buffer[0] = CardResponse[0];
                    m_tag_tx_buffer.tx_raw_buffer[1] = CardResponse[1];
                    m_tag_tx_buffer.tx_raw_buffer[2] = CardResponse[2];
                    m_tag_tx_buffer.tx_raw_buffer[3] = CardResponse[3];
                    // 加密且计算奇偶校验位
#ifdef NFC_MF1_FAST_SIM
                    Crypto1ByteArrayWithParity(m_tag_tx_buffer.tx_raw_buffer, m_tag_tx_buffer.tx_bit_parity, 4);
#else
                    mf_crypto1_encrypt(pcs, m_tag_tx_buffer.tx_raw_buffer, 4, m_tag_tx_buffer.tx_bit_parity);
#endif
                    // 验证成功了，需要进入已经验证成功的状态
                    m_mf1_state = MF1_STATE_AUTHED;
                    // 进行打包，将奇偶校验位进行拼接后返回
                    m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 32, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                    nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                } else {
                    // 暂时只存放验证失败的日志
                    append_mf1_auth_log_step3(false);
                    // 验证失败，重置状态机
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                }
            } else {
                // 读头在验证过程中发送过来的数据长度不对，肯定是有问题的
                // 我们只能是重置状态机，等待重新发起操作指令
                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
            }
            break;
        }
        
        case MF1_STATE_AUTHED: {
            if (szDataBits == 32) {
                // 在这种状态下，所有通信都被加密。因此，我们首先必须解密读头发送过来的数据。
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, 4);
#else
                mf_crypto1_decryptEx(pcs, p_data, 4, p_data);
#endif
                // 解密完成后，检查CRC是否正确，我们必须要确保数据过来的数据无误！
                if (nfc_tag_14a_checks_crc(p_data, 4)) {
                    switch (p_data[0]) {
                        case CMD_READ: {
                            // 保存当前操作的块地址
                            CurrentAddress = p_data[1];
                            // 生成访问控制，用于下面的数据访问控制
                            uint8_t Acc = abTrailorAccessConditions[ GetAccessCondition(CurrentAddress) ][ KeyInUse ];
                            // 读取命令。从内存中读取数据并附加CRCA。注意：读取操作受到控制位的限制，但是目前我们只限制控制位的读取
                            if ((CurrentAddress < 128 && (CurrentAddress & 3) == 3) || ((CurrentAddress & 15) == 15)) {
                                // 清空一下buffer，避免缓存的数据影响到后续操作
                                memset(m_tag_tx_buffer.tx_raw_buffer, 0x00, sizeof(m_tag_tx_buffer.tx_raw_buffer));
                                // 让这块数据区域变成我们需要的尾部块类型
                                nfc_tag_mf1_trailer_info_t* respTrailerInfo = (nfc_tag_mf1_trailer_info_t*)m_tag_tx_buffer.tx_raw_buffer;
                                // 尾部块的读取有以下条件限制：
                                // 1、要始终可以复制GPB（Global Public Byte）也就是控制位最后一个字节
                                // 2、秘钥A永远无法被读取！
                                // 3、根据身份验证期间已读取的访问条件做出控制位读取的本身限制！
                                respTrailerInfo->acs[3] = m_tag_trailer_info->acs[3];
                                // 判断控制位本身是否是允许读取的
                                if (Acc & ACC_TRAILOR_READ_ACC) {
                                    respTrailerInfo->acs[0] = m_tag_trailer_info->acs[0];
                                    respTrailerInfo->acs[1] = m_tag_trailer_info->acs[1];
                                    respTrailerInfo->acs[2] = m_tag_trailer_info->acs[2];
                                }
                                // 在少数情况下，秘钥B是可读的
                                if (Acc & ACC_TRAILOR_READ_KEYB) {
                                    memcpy(respTrailerInfo->keyb, m_tag_trailer_info->keyb, 6);
                                }
                            } else {
                                // 数据的话，直接返回对应位置的扇区即可
                                memcpy(m_tag_tx_buffer.tx_raw_buffer, m_tag_information->memory[CurrentAddress], 16);
                            }
                            // 无论如何，回复的数据都要计算CRC
                            nfc_tag_14a_append_crc(m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_DATA_SIZE);
                            // 加密和计算奇偶校验位后回复给读卡器
#ifdef NFC_MF1_FAST_SIM
                            Crypto1ByteArrayWithParity(m_tag_tx_buffer.tx_raw_buffer, m_tag_tx_buffer.tx_bit_parity, NFC_TAG_MF1_FRAME_SIZE);
#else
                            mf_crypto1_encrypt(pcs, m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_FRAME_SIZE, m_tag_tx_buffer.tx_bit_parity);
#endif
                            // 合并奇偶校验位到数据帧
                            m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 144, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                            // 启动发送
                            nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                            return;
                        }
                        case CMD_WRITE: {
                            //	正常的卡不允许写block0，不然会被CUID防火墙识别到
                            if (p_data[1] == 0x00 && !m_tag_information->config.mode_gen2_magic) {
                                // 直接重置14a的状态机，让标签休眠
                                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_HALTED);
                                // 告知一下读头此操作不被允许
#ifdef NFC_MF1_FAST_SIM
                                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV ^ Crypto1Nibble(), 4);
#else
                                nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, NAK_INVALID_OPERATION_TBIV), 4);
#endif
                            } else {
                                // 正常的写入命令。存储地址并准备接收即将到来的数据。
                                CurrentAddress = p_data[1];
                                m_mf1_state = MF1_STATE_WRITE;
                                // 进行ACK响应，告知读头我们已经准备好了
#ifdef NFC_MF1_FAST_SIM
                                nfc_tag_14a_tx_nbit(ACK_VALUE ^ Crypto1Nibble(), 4);
#else
                                nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, ACK_VALUE), 4);
#endif
                            }
                            return;
                        }
                        // 尽管我觉下面的三个case的代码有点蠢，除了设置状态机不同其他的都相同，但是空间换时间吧算是（心理安慰）
                        case CMD_DECREMENT: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_DECREMENT;
#ifdef NFC_MF1_FAST_SIM
                            nfc_tag_14a_tx_nbit(ACK_VALUE ^ Crypto1Nibble(), 4);
#else
                            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, ACK_VALUE), 4);
#endif
                            break;
                        }
                        case CMD_INCREMENT: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_INCREMENT;
#ifdef NFC_MF1_FAST_SIM
                            nfc_tag_14a_tx_nbit(ACK_VALUE ^ Crypto1Nibble(), 4);
#else
                            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, ACK_VALUE), 4);
#endif
                            break;
                        }
                        case CMD_RESTORE: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_RESTORE;
#ifdef NFC_MF1_FAST_SIM
                            nfc_tag_14a_tx_nbit(ACK_VALUE ^ Crypto1Nibble(), 4);
#else
                            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, ACK_VALUE), 4);
#endif
                            break;
                        }
                        case CMD_TRANSFER: {
                            uint8_t status;
                            // 此处先不判断当前的写入模式，以写入模式控制写入
                            if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DENIED) {
                                // 这个模式下直接拒绝操作
                                status = NAK_INVALID_OPERATION_TBIV;
                            } else if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DECEIVE) {
                                // 这个模式下回应ACK，但是不写入到RAM里面
                                status = ACK_VALUE;
                            } else {
                                // 将全局块缓冲区写回指令参数指定的块地址
                                memcpy(m_tag_information->memory[p_data[1]], m_data_block_buffer, MEM_BYTES_PER_BLOCK);
                                status = ACK_VALUE;
                            }
#ifdef NFC_MF1_FAST_SIM
                                nfc_tag_14a_tx_nbit(status ^ Crypto1Nibble(), 4);
#else
                                nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, status), 4);
#endif
                            break;
                        }
                        case CMD_AUTH_A:
                        case CMD_AUTH_B: {
                            // 在已经加密过的情况下发起第二次验证请求，则是嵌套验证的过程
                            uint8_t BlockAuth = p_data[1];
                            uint8_t CardNonce[4];
                            uint8_t BlockStart;
                            uint8_t BlockEnd;
                            
                            // 获得访问的块对应扇区的起始块，谨记：4K卡有大扇区，以16个block为一个扇区单位
                            // 计算思路：x = (y / n) * n，x = 扇区的起始块,y = 验证的块, n = y所在的扇区内的块的数量
                            // 思路解析：先做除法获取当前所在扇区，然后再做乘法获取所在扇区的块数量
                            if (BlockAuth >= 128) {
                                BlockStart = (BlockAuth / 16) * 16;
                                BlockEnd = BlockStart + 16 - 1;
                            } else {
                                // 非4K卡，以小扇区步进
                                BlockStart = (BlockAuth / 4) * 4;
                                BlockEnd = BlockStart + 4 - 1;
                            }
                            
                            // 当前模拟卡的类型，不足以支撑起读卡器的访问
                            if (check_block_max_overflow(BlockAuth)) {
                                break;
                            }

                            // 将KeyInUse设置为全局使用，以保留有关身份验证的信息
                            KeyInUse = p_data[0] & 1;
                            
                            // 获得指定的扇区访问控制字节，此处我们直接取巧，将内存转为结构体，让编译器帮我们维护指针的指向
                            m_tag_trailer_info = (nfc_tag_mf1_trailer_info_t*)m_tag_information->memory[BlockEnd];
                            
                            // 生成随机数
                            nfc_tag_mf1_random_nonce(CardNonce, true);
                            
                            // 根据卡随机数预先计算读卡器响应
                            for (uint8_t i = 0; i < sizeof(ReaderResponse); i++) {
                                ReaderResponse[i] = CardNonce[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(ReaderResponse, 64);
#else
                            mf1_prng_by_bytes(ReaderResponse, 64);
#endif

                            // 根据读卡器的应答预先计算我们的应答
                            for (uint8_t i = 0; i < sizeof(CardResponse); i++) {
                                CardResponse[i] = ReaderResponse[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(CardResponse, 32);
#else
                            mf1_prng_by_bytes(CardResponse, 32);
#endif
                            
                            // 记录嵌套验证信息
                            append_mf1_auth_log_step1(KeyInUse, true, BlockAuth, CardNonce);
                            
                            // 使用随机卡随机数进行响应，并期望在下一帧中从读取器获得进一步的身份验证。
                            m_mf1_state = MF1_STATE_AUTHING;
                            
                            // 复制一份标签的随机数到缓冲区中
                            m_tag_tx_buffer.tx_raw_buffer[0] = CardNonce[0];
                            m_tag_tx_buffer.tx_raw_buffer[1] = CardNonce[1];
                            m_tag_tx_buffer.tx_raw_buffer[2] = CardNonce[2];
                            m_tag_tx_buffer.tx_raw_buffer[3] = CardNonce[3];

#ifdef NFC_MF1_FAST_SIM
                            /* Setup crypto1 cipher. Discard in-place encrypted CardNonce. */
                            Crypto1SetupNested(
                                // 根据当前的指令类型选择验证A或者B秘钥
                                KeyInUse ? m_tag_trailer_info->keyb : m_tag_trailer_info->keya, 
                                // 传入当前使用的防冲撞的UID
                                UID_BY_CASCADE_LEVEL, 
                                // 传入一个明文的随机数，这个随机数将被加密并通过此缓冲区传出
                                m_tag_tx_buffer.tx_raw_buffer,
                                // 传入一个保存随机数的奇偶校验位的缓冲区
                                m_tag_tx_buffer.tx_bit_parity,
                                // 根据函数解释 Use: Decrypt = false for the tag, Decrypt = true for the reader
                                // 我们目前是标签角色，因此传入false
                                false
                            );
#else
                            // 设置crypto1密钥流，丢弃之前的加密状态
                            crypto1_deinit(pcs);
                            // 加载密钥流
                            crypto1_init(pcs, 
                                // 根据当前的指令类型选择验证A或者B秘钥
                                bytes_to_num(KeyInUse ? m_tag_trailer_info->keyb : m_tag_trailer_info->keya, 6)
                            );
                            // 进行随机数加密
                            uint8_t m_auth_nt_keystream[4];
                            num_to_bytes(bytes_to_num(UID_BY_CASCADE_LEVEL, 4) ^ bytes_to_num(CardNonce, 4), 4, m_auth_nt_keystream);
                            mf_crypto1_encryptEx(pcs, CardNonce, m_auth_nt_keystream, m_tag_tx_buffer.tx_raw_buffer, 4, m_tag_tx_buffer.tx_bit_parity);
#endif
                            // 嵌套验证的情况下，进行组帧后回复一个加密的随机数，带奇偶校验位不带CRC
                            m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 32, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                            nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                            break;
                        }
                        case CMD_HALT: {
                            // 让标签休眠。根据ISO14443协议规定，第二个字节应该是0。
                            if (p_data[1] == 0x00) {
                                // 如果一切正常，那么我们应该直接让卡片休眠，而且不能回应任何消息给读头
                                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_HALTED);
                            } else {
#ifdef NFC_MF1_FAST_SIM
                                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV ^ Crypto1Nibble(), 4);
#else
                                nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, NAK_INVALID_OPERATION_TBIV), 4);
#endif
                            }
                            break;
                        }
                        default: {
                            // 读头发了不知道什么鬼指令，我们没法处理，
                            // 因此任务此次通信异常，需要将状态重置，并且回应读头我们不支持这个指令
                            nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
#ifdef NFC_MF1_FAST_SIM
                            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV ^ Crypto1Nibble(), 4);
#else
                            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, NAK_INVALID_OPERATION_TBIV), 4);
#endif
                            break;
                        }
                    }
                } else {
                    // crc有误，返回错误码告知
#ifdef NFC_MF1_FAST_SIM
                    nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV ^ Crypto1Nibble(), 4);
#else
                    nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, NAK_INVALID_OPERATION_TBIV), 4);
#endif
                    break;
                }
            } else {
                // 已验证秘钥空闲状态但是没有收到正常的4BYTE指令，我们需要重置状态机
                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                break;
            }
            break;
        }
        
        case MF1_STATE_WRITE: {
            uint8_t status;
            // 当前处于写入状态机，我们需要确保接收到的数据是足够的长度的
            if (szDataBits == 144) {
                // 解密我们接收到的16字节的待写入数据和2字节的CRCA
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, NFC_TAG_MF1_FRAME_SIZE);
#else
                mf_crypto1_decryptEx(pcs, p_data, NFC_TAG_MF1_FRAME_SIZE, p_data);
#endif
                // 校验数据的CRC，再次确保收到的数据无误
                if (nfc_tag_14a_checks_crc(p_data, NFC_TAG_MF1_FRAME_SIZE)) {
                    // 此处先不判断当前的写入模式，以写入模式控制写入
                    if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DENIED) {
                        // 这个模式下直接拒绝操作
                        status = NAK_INVALID_OPERATION_TBIV;
                    } else if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DECEIVE) {
                        // 这个模式下回应ACK，但是不写入到RAM里面
                        status = ACK_VALUE;
                    } else {
                        // 其他剩余的模式都可以更新数据到标签的RAM中
                        memcpy(m_tag_information->memory[CurrentAddress], p_data, NFC_TAG_MF1_DATA_SIZE);
                        status = ACK_VALUE;
                    }
                } else {
                    status = NAK_CRC_PARITY_ERROR_TBIV;
                }
            } else {
                status = NAK_CRC_PARITY_ERROR_TBIV;
            }
            // 无论如何，操作结束后都将让标签回到验证空闲状态
            m_mf1_state = MF1_STATE_AUTHED;
#ifdef NFC_MF1_FAST_SIM
            nfc_tag_14a_tx_nbit(status ^ Crypto1Nibble(), 4);
#else
            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, status), 4);
#endif
            break;
        }
        
        case MF1_STATE_DECREMENT:
        case MF1_STATE_INCREMENT:
        case MF1_STATE_RESTORE: {
            uint8_t status;
            if (szDataBits == (MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH) * 8) {
                // 当我们到达这里时，前面已经发出了递减、递增或恢复命令，读取器现在正在发送数据。
                // 首先，解密数据并检查CRC。将请求的块地址中的数据读取到全局块缓冲器中，并检查完整性。
                // 然后，如果需要，根据发出的命令进行加或减，并将块存储回全局块缓冲区。
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH);
#else
                mf_crypto1_decryptEx(pcs, p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH, p_data);
#endif
                // 解密后必须要校验CRC，避免使用了出错的数据
                if (nfc_tag_14a_checks_crc(p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH)) {
                    // 先复制一份操作的块数据到全局缓冲区中
                    memcpy(m_data_block_buffer, m_tag_information->memory[CurrentAddress], MEM_BYTES_PER_BLOCK);
                    // 检查值块是否有效
                    if (CheckValueIntegrity(m_data_block_buffer)) {
                        // 获取当前的参数值和块中存放的值
                        uint32_t value_param, value_block;
                        ValueFromBlock(&value_param, p_data);
                        ValueFromBlock(&value_block, m_data_block_buffer);
                        // 进行对应的增减操作
                        if (m_mf1_state == MF1_STATE_DECREMENT) {
                            value_block -= value_param;
                        } else if (m_mf1_state == MF1_STATE_INCREMENT) {
                            value_block += value_param;
                        } else if (m_mf1_state == MF1_STATE_RESTORE) {
                            // 啥也不做
                        }
                        // 将值转换为block数据
                        ValueToBlock(m_data_block_buffer, value_block);
                        // 这三个操作的第二步，也就是本步不需要回应读头
                        // 因此当程序执行到这一步时，就可以回到已验证可以等待指令的状态了
                        break;
                    } else {
                        // 这里的应答码或许是错误的，或许根本不需要应答
                        status = NAK_OTHER_ERROR;
                    }
                } else {
                    // CRC错误
                    status = NAK_CRC_PARITY_ERROR_TBIV;
                }
            } else {
                // 长度错误，但是也算到CRC错误里面
                status = NAK_CRC_PARITY_ERROR_TBIV;
            }
            m_mf1_state = MF1_STATE_AUTHED;
#ifdef NFC_MF1_FAST_SIM
            nfc_tag_14a_tx_nbit(status ^ Crypto1Nibble(), 4);
#else
            nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, status), 4);
#endif
            break;
        }
        
        default: {
            // 未知状态？这永远不会发生，除非开发者脑子有问题！
            NRF_LOG_INFO("Unknown MF1 State");
            break;
        }
    }
}

/**
 * @brief 提供mifare标签必要的防冲突资源（仅提供指针）
 */
nfc_tag_14a_coll_res_referen_t* get_mifare_coll_res() {
    // 根据当前的互通配置，选择性的返回其中配置的数据，假设开启了数据互通，那么我们还需要确保当前模拟的卡是4BYTE的
    if (m_tag_information->config.use_mf1_coll_res && m_tag_information->res_coll.size == NFC_TAG_14A_UID_SINGLE_SIZE) {
        // 获得数据区域的厂商信息
        nfc_tag_mf1_factory_info_t* block0_factory_info = (nfc_tag_mf1_factory_info_t*)m_tag_information->memory[0];
        m_shadow_coll_res.sak = block0_factory_info->sak;               // 替换sak 
        m_shadow_coll_res.atqa = block0_factory_info->atqa;             // 替换atqa
        m_shadow_coll_res.uid = block0_factory_info->uid;               // 替换uid
        m_shadow_coll_res.size = &(m_tag_information->res_coll.size);   // 复用类型
        m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);     // 复用ats
    } else {
        // 使用单独的防冲突信息，而不是使用扇区中的信息
        m_shadow_coll_res.sak = m_tag_information->res_coll.sak;
        m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
        m_shadow_coll_res.uid = m_tag_information->res_coll.uid;
        m_shadow_coll_res.size = &(m_tag_information->res_coll.size);
        m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);
    }
    // 最终返回一个只带引用，不带实体的影子数据结构指针
    return &m_shadow_coll_res;
}

/**
 * @brief 需要重置的参数标签时回调
 */
void nfc_tag_mf1_reset_handler() {
    m_mf1_state = MF1_STATE_UNAUTH;
    m_gen1a_state = GEN1A_STATE_DISABLE;

#ifndef NFC_MF1_FAST_SIM
    // Must to reset pcs handler
    crypto1_deinit(pcs);    
#endif
}

/** @brief 获得信息结构体存放有效的信息的长度
 * @param type      细化的标签类型
 * @return 假设 type == TAG_TYPE_MIFARE_1024，
 *  那么信息的长度应当是防冲撞信息加上配置信息再加上扇区大小的长度
 */
static int get_information_size_by_tag_type(tag_specific_type_t type, bool auth_align) {
    int size_raw = sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_mf1_configure_t) + (get_block_max_by_tag_type(type) * NFC_TAG_MF1_DATA_SIZE);
    int size_align = size_raw + (size_raw % 4);
    return auth_align ? size_align : size_raw;
}

/** @brief mf1保存数据之前的回调
 * @param type      细化的标签类型
 * @param buffer    数据缓冲区
 * @return 需要保存的数据的长度，为0时表示不保存
 */
int nfc_tag_mf1_data_savecb(tag_specific_type_t type, tag_data_buffer_t* buffer) {
    if (m_tag_type != TAG_TYPE_UNKNOWN) {
        if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_SHADOW) {
            NRF_LOG_INFO("The mf1 is shadow write mode.");
            return 0;
        }
        // 根据当前标签类型保存对应大小的数据
        return get_information_size_by_tag_type(type, false);
    } else {
        return 0;
    }
}

/** @brief mf1加载数据
 * @param type      细化的标签类型
 * @param buffer    数据缓冲区
 */
int nfc_tag_mf1_data_loadcb(tag_specific_type_t type, tag_data_buffer_t* buffer) {
    // 确保外部容量足够转换为信息结构体
    int info_size = get_information_size_by_tag_type(type, false);
    if (buffer->length >= info_size) {
        // 将数据缓冲区强转为mf1结构类型
        m_tag_information = (nfc_tag_mf1_information_t *)buffer->buffer;
        // 缓存正在模拟的MF1的具体类型
        m_tag_type = type;
        // 注册14a通信管理接口
        nfc_tag_14a_handler_t handler_for_14a = {
            .get_coll_res = get_mifare_coll_res,
            .cb_state = nfc_tag_mf1_state_handler, 
            .cb_reset = nfc_tag_mf1_reset_handler,
        };
        nfc_tag_14a_set_handler(&handler_for_14a);
        NRF_LOG_INFO("HF mf1 data load finish.");
    } else {
        NRF_LOG_ERROR("nfc_tag_mf1_information_t too big.");
    }
    return info_size;
}

// 初始化mf1的工厂数据
bool nfc_tag_mf1_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default mf1 data
    uint8_t default_blk0[] = { 0xDE, 0xAD, 0xBE, 0xFF, 0x32, 0x08, 0x04, 0x00, 0x01, 0x77, 0xA2, 0xCC, 0x35, 0xAF, 0xA5, 0x1D };
    uint8_t default_data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t default_trail[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x80, 0x69, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    
    // default mf1 info
    nfc_tag_mf1_information_t mf1_tmp_information;
    nfc_tag_mf1_information_t *p_mf1_information;
    p_mf1_information = &mf1_tmp_information;
    int block_max = get_block_max_by_tag_type(tag_type);
    for (int block = 0; block < block_max; block++) {
        if (block == 0) {
            memcpy(p_mf1_information->memory[block], default_blk0, sizeof(default_blk0));
        } else if ((block < 128 && (block & 3) == 3) || ((block & 15) == 15)) {
            memcpy(p_mf1_information->memory[block], default_trail, sizeof(default_trail));
        } else {
            memcpy(p_mf1_information->memory[block], default_data, sizeof(default_data));
        }
    }
    
    // default mf1 auto ant-collision res 
    p_mf1_information->res_coll.atqa[0] = 0x04;
    p_mf1_information->res_coll.atqa[1] = 0x00;
    p_mf1_information->res_coll.sak[0] = 0x08;
    p_mf1_information->res_coll.uid[0] = 0xDE;
    p_mf1_information->res_coll.uid[1] = 0xAD;
    p_mf1_information->res_coll.uid[2] = 0xBE;
    p_mf1_information->res_coll.uid[3] = 0xFF;
    p_mf1_information->res_coll.size = NFC_TAG_14A_UID_SINGLE_SIZE;
    p_mf1_information->res_coll.ats.length = 0;
    
    // default mf1 config
    p_mf1_information->config.mode_gen1a_magic = false;
    p_mf1_information->config.mode_gen2_magic = false;
    p_mf1_information->config.use_mf1_coll_res = false;
    p_mf1_information->config.mode_block_write = NFC_TAG_MF1_WRITE_NORMAL;
    p_mf1_information->config.detection_enable = false;
    
    // save data to flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int info_size = get_information_size_by_tag_type(tag_type, true);   // auto 4 byte align.
    NRF_LOG_INFO("MF1 info size: %d", info_size);
    bool ret = fds_write_sync(map_info.id, map_info.key, info_size / 4, p_mf1_information);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}

// 设置是否使能侦测
void nfc_tag_mf1_set_detection_enable(bool enable) {
    m_tag_information->config.detection_enable = enable;
}

// 当前是否使能侦测
bool nfc_tag_mf1_is_detection_enable(void) {
    return m_tag_information->config.detection_enable;
}

// 清除侦测记录
void nfc_tag_mf1_detection_log_clear(void) {
    m_auth_log.count = 0;
}

// 获得侦测记录的统计次数
uint32_t nfc_tag_mf1_detection_log_count(void) {
    return m_auth_log.count;
}

// Set gen1a magic mode
void nfc_tag_mf1_set_gen1a_magic_mode(bool enable) {
    m_tag_information->config.mode_gen1a_magic = enable;
}

// Is in gen1a magic mode?
bool nfc_tag_mf1_is_gen1a_magic_mode(void) {
    return m_tag_information->config.mode_gen1a_magic;
}

// Set gen2 magic mode
void nfc_tag_mf1_set_gen2_magic_mode(bool enable) {
    m_tag_information->config.mode_gen2_magic = enable;
}

// Is in gen2 magic mode?
bool nfc_tag_mf1_is_gen2_magic_mode(void) {
    return m_tag_information->config.mode_gen2_magic;
}

// Set anti collision data from block 0
void nfc_tag_mf1_set_use_mf1_coll_res(bool enable) {
    m_tag_information->config.use_mf1_coll_res = enable;
}

// Get is anti collision data from block 0
bool nfc_tag_mf1_is_use_mf1_coll_res(void) {
    return m_tag_information->config.use_mf1_coll_res;
}

// Set write mode
void nfc_tag_mf1_set_write_mode(nfc_tag_mf1_write_mode_t write_mode) {
    m_tag_information->config.mode_block_write = write_mode;
}

// Get write mode
nfc_tag_mf1_write_mode_t nfc_tag_mf1_get_write_mode(void) {
    return m_tag_information->config.mode_block_write;
}

