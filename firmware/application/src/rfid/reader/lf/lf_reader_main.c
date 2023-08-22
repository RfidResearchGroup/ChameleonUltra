#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_main.h"
#include "lf_125khz_radio.h"


#define NRF_LOG_MODULE_NAME lf_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


// 默认寻卡有 N 毫秒超时
uint32_t g_timeout_readem_ms = 500;


/**
* 搜索EM410X标签
*/
uint8_t PcdScanEM410X(uint8_t *uid) {
    uint8_t ret = EM410X_TAG_NO_FOUND;
    init_em410x_hw();
    if (em410x_read(uid, g_timeout_readem_ms) == 1) {
        ret = LF_TAG_OK;
    }
    return ret;
}

/**
* 检测当前的场内是否有指定的UID的标签
*/
uint8_t check_write_ok(uint8_t *uid, uint8_t *newuid, uint8_t on_uid_diff_return) {
    // 写卡完成后，我们需要进行一次回读，
    // 如果回读的数据不正确，说明写入失败
    if (PcdScanEM410X(newuid) != LF_TAG_OK) {
        return EM410X_TAG_NO_FOUND;
    }
    // 如果回读到的卡号一样
    // 说明写入成功了（或许吧）
    if (
        uid[0] == newuid[0] &&
        uid[1] == newuid[1] &&
        uid[2] == newuid[2] &&
        uid[3] == newuid[3] &&
        uid[4] == newuid[4]) {
        return LF_TAG_OK;
    }
    // 如果发现卡，但是卡号不对，
    // 那我们就将传入的异常值返回
    return on_uid_diff_return;
}

/**
* 写T55XX标签
*/
uint8_t PcdWriteT55XX(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count) {
    uint8_t datas[8] = { 255 };
    uint8_t i;

    init_t55xx_hw();
    start_lf_125khz_radio();

    bsp_delay_ms(1);    // 启动场后延迟一段时间

    // keys 至少需要两个，一个newkey，一个oldkey
    // 一个 key 的长度是 4 个字节
    // uid newkey oldkeys * n

    // 迭代传输进来的密钥，
    // 进行T55XX标签重置
    // printf("The old keys count: %d\r\n", old_key_count);
    for (i = 0; i < old_key_count; i++) {
        T55xx_Reset_Passwd(old_keys + (i * 4), newkey);
        /*
        printf("oldkey is: %02x%02x%02x%02x\r\n",
            (old_keys + (i * 4))[0],
            (old_keys + (i * 4))[1],
            (old_keys + (i * 4))[2],
            (old_keys + (i * 4))[3]
        );*/
    }

    // 为了避免遇到特殊的控制区的标签，
    // 我们这里用新密钥来重置一下控制区
    T55xx_Reset_Passwd(newkey, newkey);

    // 编码410x的数据为block数据，为写卡做准备
    em410x_encoder(uid, datas);

    // 密钥重置完成后，进行写卡操作
    /*
    printf("newkey is: %02x%02x%02x%02x\r\n",
        newkey[0],
        newkey[1],
        newkey[2],
        newkey[3]
    );
    */
    T55xx_Write_data(newkey, datas);

    stop_lf_125khz_radio();

    // 回读验证并且返回写卡结果
    // 此处不回读，由上位机校验
    return LF_TAG_OK;
}

/**
* 设置EM卡的寻卡超时的时间值
*/
void SetEMScanTagTimeout(uint32_t ms) {
    g_timeout_readem_ms = ms;
}
