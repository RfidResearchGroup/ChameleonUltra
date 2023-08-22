#ifdef debug410x
#include <stdio.h>
#endif

#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_data.h"
#include "lf_em410x_data.h"
#include "lf_125khz_radio.h"

#define NRF_LOG_MODULE_NAME em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


static RAWBUF_TYPE_S carddata;
static volatile uint8_t dataindex = 0;          //记录变化沿次数
uint8_t cardbufbyte[cardbufbytesize];   //卡片数据

#ifdef debug410x
uint8_t datatest[256] = { 0x00 };
#endif


//处理卡片数据，输入raw buffer的起始位置2的位置（2111。。。。）
//处理完卡片数据放cardbuf，返回5正常解析
//Pdata为rawbuffer
uint8_t mcst(RAWBUF_TYPE_S *Pdata) {
    uint8_t sync = 1;      //当前间隔处理完后，是否在判定线上
    uint8_t cardindex = 0; //记录变化次数
    for (int i = Pdata->startbit; i < rawbufsize * 8; i++) {
        uint8_t thisbit = readbit(Pdata->rawa, Pdata->rawb, i);
        switch (sync) {
            case 1: //同步状态
                switch (thisbit) {
                    case 0: //同步状态的1T，添加1位0，依然同步
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    case 1: //同步状态的1.5T，添加1位1，切换到非同步状态
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        sync = 0;
                        break;
                    case 2: //同步状态的2T，添加2位10，依然同步
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    default:
                        return 0;
                }
                break;
            case 0: //非同步状态
                switch (thisbit) {
                    case 0: //非同步状态的1T，添加1位1，依然非同步
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        break;
                    case 1: //非同步状态的1.5T，添加2位10，切换到同步状态
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        sync = 1;
                        break;
                    case 2: //非同步状态的2T，不可能出现的情况，报错。
                        return 0;
                    default:
                        return 0;
                }
                break;
        }
        if (cardindex >= cardbufsize * 8)
            break;
    }
    return 1;
}

//处理卡片，寻找校验位并且判断是否正常
uint8_t em410x_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut) {
    if (size != 8) {
        //NRF_LOG_INFO("size err %d!\n", size);
        return 0;
    }

    // NRF_LOG_INFO("开始解码数据！\n");

    // 迭代数据的次数，每次 +5 个bit
    uint8_t iteration = 0;
    // 当前合并的数据的存放位置
    uint8_t merge_pos = 0;

    // 快速校验头部
    uint8_t head_check = 1;
    for (int i = 0; i < 9; i++) {
        head_check &= getbit(pData[i / 8], i % 8);
    }
    // 一起快速校验尾部
    if ((!head_check) || getbit(pData[7], 7)) {
        //NRF_LOG_INFO("head or tail err!\n");
        return 0;
    }

    // NRF_LOG_INFO("头部与尾部检测通过！\n");

    // 先校验X轴的数据
    // X轴校验，每隔 5个bit 校验一次，
    // 校验完成后保存为 半个byte
    for (int i = 9; i < size * 8 - 5; i += 5) {
        uint8_t count_bit_x = 0;

        for (int j = i; j < i + 5; j++) {

            // 收集偶数据个数
            if (getbit(pData[j / 8], j % 8) == 1) {
                count_bit_x += 1;
            }

            if (j != i + 4) {
                // 合并bit数据到 uint8缓冲区中
                // 需要加上左对齐坐标，如果是高位部分
                uint8_t first_merge_offset = (iteration % 2) ? 0 : 4;
                uint8_t finally_offset = first_merge_offset + ((i + 5 - 1) - j - 1);
                // NRF_LOG_INFO("需要左移 %d 位。\n", finally_offset);
                getbit(pData[j / 8], j % 8) ? (pOut[merge_pos] |= 1 << finally_offset) : (pOut[merge_pos] &= ~(1 << finally_offset));
            }

            // 如果在第一行，我们还可以直接去校验 Y 轴的校验位
            if (iteration == 0 && j != i + 4) {
                uint8_t count_bit_y = 0;
                for (int m = j; m < j + 51; m += 5) {
                    // NRF_LOG_INFO("当前的m坐标是 %d, 数据是 %d\n", m, pData[m]);
                    if (getbit(pData[m / 8], m % 8) == 1) {
                        count_bit_y += 1;
                    }
                }
                if (count_bit_y % 2) {
                    //NRF_LOG_INFO("bit even parity err at Y-axis from %d to %d!\n", j, j + 51);
                    return 0;
                }
            } // 否则的话，就直接进入下一轮校验
        }

        // 经过一轮校验后，下次就不要校验 Y 轴
        iteration += 1;

        // 如果取余数不为0
        // 说明进入了新的数据处理循环
        // 我们需要递增存放合并字节需要的下标
        if (!(iteration % 2)) {
            merge_pos += 1;
            // NRF_LOG_INFO("\n");
        }

        if (count_bit_x % 2) {
            //NRF_LOG_INFO("bit even parity err at X-axis from %d to %d!\n", i, i + 5);
            return 0;
        }
    }
    return 1;
}

/**
* 对EM410x卡号进行编码
* @param: pData 卡号 - ID，固定5个长度的byte
* @param: pOut 输出缓冲区，固定8个长度的byte
*/
void em410x_encoder(uint8_t *pData, uint8_t *pOut) {
    //#define EM410X_Encoder_NRF_LOG_INFO

    // 为了节省代码空间，我们可以在规律内限制死数据长度
    // 也就是说，总体循环次数不可以超过 0 - 127 个bit
    // 当然，对于正经的EM410来说，这个循环的次数足够了
    int8_t i, j;

    // 某些数据其实可以通过时间换空间
    // 但是这点空间太小了，还不如留着换时间
    // 所以就不要改了。
    uint8_t pos, bit, count1;

    pOut[0] = 0xFF; // 前导码有9个1，所以我们先给第一个字节限定为 11111111
    pOut[1] = 0x80; // 没啥好说的，第二个Byte的msb也要是一个 1 ，这样就凑够了 1 * 9 的前导码

    //重置数据为空
    for (i = 2; i < 8; i++) {
        pOut[i] = 0x00;
    }

    // bit置位为9，因为有 0 - 8 总共 9 个前导码 ( 1 * 9 )
    pos = 9;
    // 重置bit计数
    count1 = 0;

    // X轴 迭代 5个Byte的卡号，拼凑bit到缓冲区中并且计算奇偶校验位
    for (i = 0; i < 5; i++) {
        // 迭代处理每个bit
        for (j = 7; j >= 0; j--) {
            // 取出单个bit
            bit = ((pData[i] >> j) & 0x01);

#ifdef EM410X_Encoder_NRF_LOG_INFO
            NRF_LOG_INFO("%d ", bit);
#endif // EM410X_Encoder_NRF_LOG_INFO

            // 原生数据放入到输出缓冲区
            pOut[pos / 8] |= (bit << (7 - pos % 8));
            pos += 1;

            // 统计偶校验计数
            if (bit) {
                count1 += 1;
            }

            // 奇偶校验位放入到输出缓冲区
            if (j == 4 || j == 0) {

#ifdef EM410X_Encoder_NRF_LOG_INFO
                NRF_LOG_INFO(" <- 比特RAW : 奇偶校验 -> %d\n", count1 % 2);
#endif // EM410X_Encoder_NRF_LOG_INFO

                // 不用说了，肯定是放入一个bit的奇偶校验位啊
                pOut[pos / 8] |= ((count1 % 2) << (7 - pos % 8));
                pos += 1;
                count1 = 0;
            }
        }
    }

#ifdef EM410X_Encoder_NRF_LOG_INFO
    NRF_LOG_INFO("\n");
#endif // EM410X_Encoder_NRF_LOG_INFO

    // Y轴 迭代 5个byte的卡号，生成4个bit的奇偶校验位
    for (i = 0; i < 4; i++) {
        count1 = 0;
        for (j = 0; j < 5; j++) {
            // 高位计数
            bit = ((pData[j] >> (7 - i)) & 0x01);
            if (bit) {
                count1 += 1;
            }
            // 低位计数
            bit = ((pData[j] >> (3 - i)) & 0x01);
            if (bit) {
                count1 += 1;
            }
        }

        // Y轴计算完成，放到最终的bit输出缓冲区中
        pOut[pos / 8] |= ((count1 % 2) << (7 - pos % 8));
        pos += 1;

#ifdef EM410X_Encoder_NRF_LOG_INFO
        NRF_LOG_INFO("%d ", count1 % 2);
#endif // EM410X_Encoder_NRF_LOG_INFO
    }

#ifdef EM410X_Encoder_NRF_LOG_INFO
    NRF_LOG_INFO(" <- 奇偶校验 : 尾导码  -> 0\n\n");
#endif // EM410X_Encoder_NRF_LOG_INFO
}

//读卡函数,需要不停调用，返回0为没读到卡，1为读到了
uint8_t em410x_acquire(void) {
    if (dataindex >= rawbufsize * 8) {
#ifdef debug410x
        {
            for (int i = 0; i < rawbufsize * 8; i++) {
                NRF_LOG_INFO("%d ", readbit(carddata.rawa, carddata.rawb, i));
            }
            NRF_LOG_INFO("///raw data\r\n");
            for (int i = 0; i < rawbufsize * 8; i++) {
                NRF_LOG_INFO("%d ", datatest[i]);
            }
            NRF_LOG_INFO("///time data\r\n");
        }
#endif
        //寻找目标0 1111 1111
        carddata.startbit = 255;
        for (int i = 0; i < (rawbufsize * 8) - 8; i++) {
            if (readbit(carddata.rawa, carddata.rawb, i) == 1) {
                carddata.startbit = 0;
                for (int j = 1; j < 8; j++) {
                    carddata.startbit += (uint8_t)readbit(carddata.rawa, carddata.rawb, i + j);
                }
                if (carddata.startbit == 0) {
                    carddata.startbit = i;
                    break;
                } else {
                    carddata.startbit = 255;
                }
            }
        }
        // 如果找到了合适的开头，进行处理
        if (carddata.startbit != 255 && carddata.startbit < (rawbufsize * 8) - 64) {
            //保证卡片数据可以完整解析
            //NRF_LOG_INFO("do mac,start: %d\r\n",startbit);
            if (mcst(&carddata) == 1) {
                //卡片正常解析
#ifdef debug410x
                {
                    for (int i = 0; i < cardbufsize; i++) {
                        NRF_LOG_INFO("%02X", carddata.hexbuf[i]);
                    }
                    NRF_LOG_INFO("///card data\r\n");
                }
#endif
                if (em410x_decoder(carddata.hexbuf, cardbufsize, cardbufbyte)) {
                    //卡片数据检查通过
#ifdef debug410x
                    for (int i = 0; i < 5; i++) {
                        NRF_LOG_INFO("%02X", (int)cardbufbyte[i]);
                    }
                    NRF_LOG_INFO("///card dataBYTE\r\n");
#endif
                    dataindex = 0;
                    return 1;
                }
            }
        }
        // 启动新的一个周期
        dataindex = 0;
    }
    return 0;
}

//gpio中断回调函数，用于检测下降沿
void GPIO_INT0_callback(void) {
    static uint32_t thistimelen = 0;
    thistimelen = get_lf_counter_value();
    if (thistimelen > 47) {
        static uint8_t cons_temp = 0;
        if (dataindex < rawbufsize * 8) {
            if (48 <= thistimelen && thistimelen <= 80) {
                cons_temp = 0;
            } else if (80 <= thistimelen && thistimelen <= 112) {
                cons_temp = 1;
            } else if (112 <= thistimelen && thistimelen <= 144) {
                cons_temp = 2;
            } else {
                cons_temp = 3;
            }
            writebit(carddata.rawa, carddata.rawb, dataindex, cons_temp);
#ifdef debug410x
            datatest[dataindex] = thistimelen;
#endif
            dataindex++;
        }
        clear_lf_counter_value();
    }

    uint16_t counter = 0;
    do {
        __NOP();
    } while (counter++ > 1000);
}

//启动定时器和初始化相关外设，启动低频读卡
void init_em410x_hw(void) {
    //注册读卡器io中断回调
    register_rio_callback(GPIO_INT0_callback);
}

/**
* 在指定的超时内读取EM410X卡的卡号
*/
uint8_t em410x_read(uint8_t *uid, uint32_t timeout_ms) {
    uint8_t ret = 0;

    init_em410x_hw();           // 初始化下降沿采样回调函数
    start_lf_125khz_radio();    // 启动125khz调制

    // 在超时中读卡
    autotimer *p_at = bsp_obtain_timer(0);
    // NO_TIMEOUT_1MS(p_at, timeout_ms)
    while (NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        //执行读卡，读到就退出
        if (em410x_acquire()) {
            stop_lf_125khz_radio();
            uid[0] = cardbufbyte[0];
            uid[1] = cardbufbyte[1];
            uid[2] = cardbufbyte[2];
            uid[3] = cardbufbyte[3];
            uid[4] = cardbufbyte[4];
            ret = 1;
            break;
        }
    }

    if (ret != 1) {  // 如果没有搜索到卡，说明超时了，我们这里要手动结束读卡器
        stop_lf_125khz_radio();
    }

    dataindex = 0;  // 结束后谨记重置采集的数据的索引

    bsp_return_timer(p_at);
    p_at = NULL;

    return ret;
}
