#ifdef debugt55xx
#include <stdio.h>
#endif

#include "nrf_sdh_soc.h"
#include "nrf_gpio.h"

#include "timeslot.h"
#include "bsp_delay.h"
#include "lf_t55xx_data.h"
#include "lf_reader_data.h"
#include "lf_125khz_radio.h"


#define NRF_LOG_MODULE_NAME lf_t55xx
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


/*
小机器写卡：
01 00000000000001000010000000010000 0 000

01 1 00010100010010010000110110010010 000 00000000000000000000000000000 000 011

01 1 00010100010010010000110110010110 000
         01010001001001000011011001011000 51243658

10 01010001001001000011011001001000 0 01010001001001000011011001001000 111

|--------------------------------------------------------------------------|
|                      70bit Password write found                          |
|--------------------------------------------------------------------------|
|OP|PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|01010001001001000011011001001000|0|01010001001001000011011001001000|111|    0区 7块 用当前密码写当前密码（密码）
|10|01010001001001000011011001001000|0|01010001001001000111011001001000|111|    0区 7块 用当前密码写当前密码（密码）
|10|01010001001001000011011001001000|0|00000000000101001000000001010000|000|    0区 0块 用当前密码写00148050（控制区）
|10|01010001001001000011011001001000|0|11111111101001010010000000000100|001|    0区 1块 用当前密码写FFA52004（数据）
|11|01010001001001000011011001001000|0|11111111101001010010000000000100|001|    1区 1块 用当前密码写FFA52004（数据）
|10|01010001001001000011011001001000|0|10100101011100011001011101101010|010|    0区 2块 用当前密码写A571976A（数据）
|11|01010001001001000011011001001000|0|10100101011100011001011101101010|010|    1区 2块 用当前密码写A571976A（数据）
|11|01010001001001000011011001001000|0|01100000000000000000100000000000|011|    1区 3块 用当前密码写60000800（射频参数）
|--------------------------------------------------------------------------|
RESET Pack received
|-----------------------------------------|
|       38bit regular write found         |
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|0|00000000000101001000000001000000|000|0区 0块 写00148050（控制区）
|10|0|11111111101001010010000000000100|001|0区 1块 写FFA52004（数据）
|10|0|10100101011100011001011101101010|010|0区 2块 写A571976A（数据）
|-----------------------------------------|
RESET Pack received

拷贝齐写卡：
|-----------------------------------------|
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|0|00011001100100100000010000100111|111|0区 7块 写19920427（密码）
|10|0|00000000000101001000000001010000|000|0区 0块 写00148050（控制区）
|-----------------------------------------|
|--------------------------------------------------------------------------|
|OP|PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|00011001100100100000010000100111|0|11111111101001010010000000000100|001|
|10|00011001100100100000010000100111|0|10100101011111011101110000011010|010|
|10|00011001100100100000010000100111|0|00000000000101001000000001010000|000|
|10|00011001100100100000010000100111|0|11111111101001010010000000000100|001|
|10|00011001100100100000010000100111|0|10100101011111011101110000011010|010|
|--------------------------------------------------------------------------|
|-----------------------------------------|
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|0|11111111101001010010000000000100|001|
|10|0|10100101011111011101110000011010|010|
|-----------------------------------------|
RESET Pack received
*/


static struct {
    uint8_t opcode;
    uint8_t usepassword;
    uint32_t password;
    uint8_t lockBit;
    uint8_t usedata;
    uint32_t data;
    uint8_t blokAddr;
} t55xx_cmd;


// 空函数，t55xx写卡不需要关心读取的数据
void empty_callback() { }


//启动定时器和初始化相关外设，启动低频读卡
void init_t55xx_hw(void)
{
    //注册读卡器io中断回调
    register_rio_callback(empty_callback);
}

void T55xx_SendGap(unsigned int tm)
{
    stop_lf_125khz_radio();    // 关闭pwm输出
    bsp_delay_us(tm);
    start_lf_125khz_radio();     // 启动pwm输出
}

void TxBitRfid(uint8_t data)
{
    if (data & 1)
        bsp_delay_us(54 * 8);
    else
        bsp_delay_us(24 * 8);
    T55xx_SendGap(9 * 8); //write gap
}

void TxByteRfid(uint8_t data)
{
    for (uint8_t n_bit = 0; n_bit < 8; n_bit++)
    {
        TxBitRfid(data & 1);
        data = data >> 1;
    }
}

// T55XX高精度时序控制函数
void T55XX_Timeslot_Callback() {
    T55xx_SendGap(30 * 8); // start gap

    //先发送指令
    TxBitRfid(t55xx_cmd.opcode >> 1);
    TxBitRfid(t55xx_cmd.opcode & 1);

    //指令为00时不需要发送后面的东西了
    if (t55xx_cmd.opcode != 0) {
        //指令后如果有需要才发密码
        if (t55xx_cmd.usepassword)
        {
            for (uint8_t i = 0; i < 32; i++)
            {
                TxBitRfid((t55xx_cmd.password >> (31 - i)) & 1);
            }
        }

        //处理锁定位
        if (t55xx_cmd.lockBit == 0 || t55xx_cmd.lockBit == 1)
        {
            TxBitRfid(t55xx_cmd.lockBit & 1);
        }

        //有需求才发数据
        if (t55xx_cmd.usedata)
        {
            for (uint8_t i = 0; i < 32; i++)
            {
                TxBitRfid((t55xx_cmd.data >> (31 - i)) & 1);
            }
        }

        //处理地址位
        if (t55xx_cmd.blokAddr != 255)
        {
            TxBitRfid(t55xx_cmd.blokAddr >> 2);
            TxBitRfid(t55xx_cmd.blokAddr >> 1);
            TxBitRfid(t55xx_cmd.blokAddr & 1);
        }
    }
}

/**
 * @brief 向5577写入指令，这个指令可以是读写
 *
 * @param opcode 操作码，在正常操作模式下一定是1*的格式，只有重置是00
 * @param usepassword 是否使用密码，为1时指令为密码模式
 * @param password 密码，在usepasswd有效的时候发送，32个bit，从下标0开始传输
 * @param lockBit 锁定位，只有可能是1或者0，传入其他值代表不使用lock位（用于密码唤醒模式）
 * @param usedata 是否使用数据区域，为1时传输数据
 * @param data 数据，32个bit，从下标0开始传输
 * @param blokAddr 块编号，3个bit 0-7块，输入255代表不使用该位（用于密码唤醒模式）
 */
void T55xx_Send_Cmd(uint8_t opcode, uint8_t usepassword, uint32_t password, uint8_t lockBit, uint8_t usedata, uint32_t data, uint8_t blokAddr)
{
    //密码读取模式，        2op(1+bck)  32pw    1(0)            3addr
    //密码写入模式，        2op(1+bck)  32pw    1l      32data  3addr
    //密码唤醒模式，        2op(1+0)    32pw

    //直接读取模式，        2op(1+bck)          1(0)            3addr
    //标准写入模式，        2op(1+bck)          1l      32data  3addr

    //这个不实现//标准读取页模式，      2op(1+bck)

    //重置模式，            2op(0+0)

    t55xx_cmd.opcode = opcode;
    t55xx_cmd.usepassword = usepassword;
    t55xx_cmd.password = password;
    t55xx_cmd.lockBit = lockBit;
    t55xx_cmd.usedata = usedata;
    t55xx_cmd.data = data;
    t55xx_cmd.blokAddr = blokAddr;


    // 请求时序，并且等待时序操作完成
    request_timeslot(37 * 1000, T55XX_Timeslot_Callback, true);

    if (opcode != 0) {
        bsp_delay_ms(6);    // 可能继续下次写卡，需要多等一会儿
    } else {
        bsp_delay_ms(1);
    }
}

/**
 * @brief T55xx写入EM410x数据
 *
 * @param passwd 用于最后加密的密码（也是卡片当前密码）(是一个指针，4字节宽度小端byte序存储)
 * @param datas em410x运算后的数据，需要调用EM410X_Encoder计算
 */
void T55xx_Write_data(uint8_t *passwd, uint8_t *datas)
{
    uint32_t blk1data = 0, blk2data = 0, u32passwd = 0;
    //提取两个block的数据和密码
    for (uint8_t dataindex = 0; dataindex < 4; dataindex++)
    {
        blk1data = blk1data << 8;
        blk1data |= (uint8_t)datas[dataindex];
        u32passwd = u32passwd << 8;
        u32passwd |= (uint8_t)passwd[dataindex];
    }
    for (uint8_t dataindex = 4; dataindex < 8; dataindex++)
    {
        blk2data = blk2data << 8;
        blk2data |= (uint8_t)datas[dataindex];
    }
                                                                //先写入密码区
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, u32passwd, 7);        //0区 7块 用当前密码写当前密码（密码）
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, u32passwd, 7);        //0区 7块 用当前密码写当前密码（密码）
                                                                //然后写入控制区
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, 0X00148050, 0);       //0区 0块 用当前密码写00148050（控制区）
                                                                //然后写入数据
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, blk1data, 1);         //0区 1块 用当前密码写blk1data（数据）
    T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, blk1data, 1);         //1区 1块 用当前密码写blk1data（数据）
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, blk2data, 2);         //0区 2块 用当前密码写blk2data（数据）
    T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, blk2data, 2);         //1区 2块 用当前密码写blk2data（数据）
                                                                //然后写入射频参数
    // 2021-12-15 fix：写此数据会导致小卡无法重复写
    // T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, 0X60000800, 3);    //1区 3块 用当前密码写60000800（射频参数）

                                                                //然后用无密码指令再写一遍
    T55xx_Send_Cmd(2, 0, 0, 0, 1, 0X00148050, 0);               //0区 0块 写00148050（控制区）
    T55xx_Send_Cmd(2, 0, 0, 0, 1, blk1data, 1);                 //0区 1块 写blk1data（数据）
    T55xx_Send_Cmd(2, 0, 0, 0, 1, blk2data, 2);                 //0区 2块 写blk2data（数据）
    T55xx_Send_Cmd(0, 0, 0, 0, 0, 0, 0);                        //重启卡片
}

/**
 * @brief 重置密码函数，用于将现有已知密码的卡重置为目标密码
 *
 * @param oldpasswd 卡片当前密码(是一个指针，4字节宽度小端byte序存储)
 * @param newpasswd 用于最后加密的密码(是一个指针，4字节宽度小端byte序存储)
 */
void T55xx_Reset_Passwd(uint8_t *oldpasswd, uint8_t *newpasswd){
    uint32_t u32oldpasswd = 0, u32newpasswd = 0;
    //提取两个block的数据和密码
    for (uint8_t dataindex = 0; dataindex < 4; dataindex++)
    {
        u32oldpasswd = u32oldpasswd << 8;
        u32oldpasswd |= (uint8_t)oldpasswd[dataindex];
        u32newpasswd = u32newpasswd << 8;
        u32newpasswd |= (uint8_t)newpasswd[dataindex];
    }

    T55xx_Send_Cmd(2, 1, u32oldpasswd, 0, 1, u32newpasswd, 7);  //0区 7块 用当前密码写新密码（密码）
    T55xx_Send_Cmd(2, 1, u32oldpasswd, 0, 1, u32newpasswd, 7);  //0区 7块 用当前密码写新密码（密码）
    T55xx_Send_Cmd(0, 0, 0, 0, 0, 0, 0);                        //重启卡片
}
