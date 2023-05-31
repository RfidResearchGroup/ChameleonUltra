#include <stdint.h>

#include "lf_tag_em.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "fds_util.h"
#include "tag_persistence.h"
#include "bsp_delay.h"

#include "nrf_gpio.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_lpcomp.h"

#define NRF_LOG_MODULE_NAME tag_em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


// 获取指定的位置的bit
#define GETBIT(v, bit) ((v >> bit) & 0x01)
// 天线控制
#define ANT_TO_MOD()   nrf_gpio_pin_set(LF_MOD)
#define ANT_NO_MOD()  nrf_gpio_pin_clear(LF_MOD)


// usb灯效是否允许使能
extern bool g_usb_led_marquee_enable;

// 承载64位ID号的比特数据
static uint64_t m_id_bit_data = 0;
// 当前发送的卡片ID的bit位置
static uint8_t m_bit_send_position;
// 当前是否在发送第一个沿
static bool m_is_send_first_edge;
// 当前广播ID号总计几次 33ms 一次，一秒钟大概能广播30次
static uint8_t m_send_id_count;
// 当前是否正在广播低频卡号中
static volatile bool m_is_lf_emulating = false;
// 发送卡号的定时器，我们使用定时器 3
const nrfx_timer_t m_timer_send_id = NRFX_TIMER_INSTANCE(3);
// 缓存标签类型
static tag_specific_type_t m_tag_type = TAG_TYPE_UNKNOWN;


/**
 * @brief 将EM410X的卡号转为U64的内存布局，计算奇偶校验位
 *  根据手册的说明 EM4100 使用U64足以容纳
 */
uint64_t em410x_id_to_memory64(uint8_t id[5]) {
    // 联合体，所见即所得
    union {
        uint64_t u64;
        struct {
            // 9 header bits
            uint8_t h00: 1; uint8_t h01: 1; uint8_t h02: 1; uint8_t h03: 1; uint8_t h04: 1; uint8_t h05: 1; uint8_t h06: 1; uint8_t h07: 1; uint8_t h08: 1;
            // 8 version bits and 2 bit parity
            uint8_t d00: 1; uint8_t d01: 1; uint8_t d02: 1; uint8_t d03: 1; uint8_t p0: 1;
            uint8_t d10: 1; uint8_t d11: 1; uint8_t d12: 1; uint8_t d13: 1; uint8_t p1: 1;
            // 32 data bits and 8 bit parity
            uint8_t d20: 1; uint8_t d21: 1; uint8_t d22: 1; uint8_t d23: 1; uint8_t p2: 1;
            uint8_t d30: 1; uint8_t d31: 1; uint8_t d32: 1; uint8_t d33: 1; uint8_t p3: 1;
            uint8_t d40: 1; uint8_t d41: 1; uint8_t d42: 1; uint8_t d43: 1; uint8_t p4: 1;
            uint8_t d50: 1; uint8_t d51: 1; uint8_t d52: 1; uint8_t d53: 1; uint8_t p5: 1;
            uint8_t d60: 1; uint8_t d61: 1; uint8_t d62: 1; uint8_t d63: 1; uint8_t p6: 1;
            uint8_t d70: 1; uint8_t d71: 1; uint8_t d72: 1; uint8_t d73: 1; uint8_t p7: 1;
            uint8_t d80: 1; uint8_t d81: 1; uint8_t d82: 1; uint8_t d83: 1; uint8_t p8: 1;
            uint8_t d90: 1; uint8_t d91: 1; uint8_t d92: 1; uint8_t d93: 1; uint8_t p9: 1;
            // 5 bit end.
            uint8_t pc0: 1; uint8_t pc1: 1; uint8_t pc2: 1; uint8_t pc3: 1; uint8_t s0: 1;
        } bit;
    } memory;

    // 好了，到了目前最关键的时候了，现在需要赋值和计算奇偶校验位了
    // 1、先把前导码给赋值了
    memory.bit.h00 = memory.bit.h01 = memory.bit.h02 = 
    memory.bit.h03 = memory.bit.h04 = memory.bit.h05 = 
    memory.bit.h06 = memory.bit.h07 = memory.bit.h08 = 1;
    // 2、把8bit的版本或者自定义ID给赋值了
    memory.bit.d00 = GETBIT(id[0], 7); memory.bit.d01 = GETBIT(id[0], 6); memory.bit.d02 = GETBIT(id[0], 5); memory.bit.d03 = GETBIT(id[0], 4);
    memory.bit.p0 = memory.bit.d00 ^ memory.bit.d01 ^ memory.bit.d02 ^ memory.bit.d03;
    memory.bit.d10 = GETBIT(id[0], 3); memory.bit.d11 = GETBIT(id[0], 2); memory.bit.d12 = GETBIT(id[0], 1); memory.bit.d13 = GETBIT(id[0], 0);
    memory.bit.p1 = memory.bit.d10 ^ memory.bit.d11 ^ memory.bit.d12 ^ memory.bit.d13;
    // 3、把32bit的数据给赋值了
    // - byte1
    memory.bit.d20 = GETBIT(id[1], 7); memory.bit.d21 = GETBIT(id[1], 6); memory.bit.d22 = GETBIT(id[1], 5); memory.bit.d23 = GETBIT(id[1], 4);
    memory.bit.p2 = memory.bit.d20 ^ memory.bit.d21 ^ memory.bit.d22 ^ memory.bit.d23;
    memory.bit.d30 = GETBIT(id[1], 3); memory.bit.d31 = GETBIT(id[1], 2); memory.bit.d32 = GETBIT(id[1], 1); memory.bit.d33 = GETBIT(id[1], 0);
    memory.bit.p3 = memory.bit.d30 ^ memory.bit.d31 ^ memory.bit.d32 ^ memory.bit.d33;
    // - byte2
    memory.bit.d40 = GETBIT(id[2], 7); memory.bit.d41 = GETBIT(id[2], 6); memory.bit.d42 = GETBIT(id[2], 5); memory.bit.d43 = GETBIT(id[2], 4);
    memory.bit.p4 = memory.bit.d40 ^ memory.bit.d41 ^ memory.bit.d42 ^ memory.bit.d43;
    memory.bit.d50 = GETBIT(id[2], 3); memory.bit.d51 = GETBIT(id[2], 2); memory.bit.d52 = GETBIT(id[2], 1); memory.bit.d53 = GETBIT(id[2], 0);
    memory.bit.p5 = memory.bit.d50 ^ memory.bit.d51 ^ memory.bit.d52 ^ memory.bit.d53;
    // - byte3
    memory.bit.d60 = GETBIT(id[3], 7); memory.bit.d61 = GETBIT(id[3], 6); memory.bit.d62 = GETBIT(id[3], 5); memory.bit.d63 = GETBIT(id[3], 4);
    memory.bit.p6 = memory.bit.d60 ^ memory.bit.d61 ^ memory.bit.d62 ^ memory.bit.d63;
    memory.bit.d70 = GETBIT(id[3], 3); memory.bit.d71 = GETBIT(id[3], 2); memory.bit.d72 = GETBIT(id[3], 1); memory.bit.d73 = GETBIT(id[3], 0);
    memory.bit.p7 = memory.bit.d70 ^ memory.bit.d71 ^ memory.bit.d72 ^ memory.bit.d73;
    // - byte4
    memory.bit.d80 = GETBIT(id[4], 7); memory.bit.d81 = GETBIT(id[4], 6); memory.bit.d82 = GETBIT(id[4], 5); memory.bit.d83 = GETBIT(id[4], 4);
    memory.bit.p8 = memory.bit.d80 ^ memory.bit.d81 ^ memory.bit.d82 ^ memory.bit.d83;
    memory.bit.d90 = GETBIT(id[4], 3); memory.bit.d91 = GETBIT(id[4], 2); memory.bit.d92 = GETBIT(id[4], 1); memory.bit.d93 = GETBIT(id[4], 0);
    memory.bit.p9 = memory.bit.d90 ^ memory.bit.d91 ^ memory.bit.d92 ^ memory.bit.d93;
    // 4、计算纵向的偶校验
    memory.bit.pc0 = memory.bit.d00 ^ memory.bit.d10 ^ memory.bit.d20 ^ memory.bit.d30 ^ memory.bit.d40 ^ memory.bit.d50 ^ memory.bit.d60 ^ memory.bit.d70 ^ memory.bit.d80 ^ memory.bit.d90;
    memory.bit.pc1 = memory.bit.d01 ^ memory.bit.d11 ^ memory.bit.d21 ^ memory.bit.d31 ^ memory.bit.d41 ^ memory.bit.d51 ^ memory.bit.d61 ^ memory.bit.d71 ^ memory.bit.d81 ^ memory.bit.d91;
    memory.bit.pc2 = memory.bit.d02 ^ memory.bit.d12 ^ memory.bit.d22 ^ memory.bit.d32 ^ memory.bit.d42 ^ memory.bit.d52 ^ memory.bit.d62 ^ memory.bit.d72 ^ memory.bit.d82 ^ memory.bit.d92;
    memory.bit.pc3 = memory.bit.d03 ^ memory.bit.d13 ^ memory.bit.d23 ^ memory.bit.d33 ^ memory.bit.d43 ^ memory.bit.d53 ^ memory.bit.d63 ^ memory.bit.d73 ^ memory.bit.d83 ^ memory.bit.d93;
    // 5、设置最后一个EOF的位，这波转换就算是结束了
    memory.bit.s0 = 0;
    // 返回联合体中的u64数据，这才是我们最终需要的数据，
    // 后期模拟卡只需要拿出每个bit去发送就行了
    return memory.u64;
}

/**
* @brief 判断场状态
 */
 bool lf_is_field_exists(void) {
    nrf_drv_lpcomp_enable();
    bsp_delay_us(20);                                   // 延迟一段时间再采样，避免误判
    nrf_lpcomp_task_trigger(NRF_LPCOMP_TASK_SAMPLE);    // 触发一次采样
    return nrf_lpcomp_result_get() == 1;                // 判断LF场状态的采样结果
}

void timer_ce_handler(nrf_timer_event_t event_type, void* p_context) {
    switch (event_type) {
        // 因为我们配置的是使用CC通道2，所以事件回调
        // 函数中判断NRF_TIMER_EVENT_COMPARE0事件
		case NRF_TIMER_EVENT_COMPARE2: {
            if (m_is_send_first_edge) {
                if (GETBIT(m_id_bit_data, m_bit_send_position)) {
                    // 发送 1 的第一个沿
                    ANT_TO_MOD();
                } else {
                    // 发送 0 的第一个沿
                    ANT_NO_MOD();
                }
                m_is_send_first_edge = false;   // 下次发送第二个沿
            } else {
                if (GETBIT(m_id_bit_data, m_bit_send_position)) {
                    // 发送 1 的第二个沿
                    ANT_NO_MOD();
                } else {
                    // 发送 0 的第二个沿
                    ANT_TO_MOD();
                }
                m_is_send_first_edge = true;    // 下次发送第一个沿
                if (++m_bit_send_position >= LF_125KHZ_EM410X_BIT_SIZE) {
                    m_bit_send_position = 0;    // 广播一次成功，bit位置归零
                    ++m_send_id_count;          // 统计广播次数
                }
            }
            // 如果广播次数超过上限次数，则重新比较场状态，根据新的场状态选择是否继续模拟标签
            if (m_send_id_count >= LF_125KHZ_BORADCAST_MAX) {
                m_send_id_count = 0;                                        // 广播次数达到上限，重新识别场状态并且重新统计广播次数
                ANT_NO_MOD();                                               // 确保天线不短路而导致无法获得RSSI状态
                nrfx_timer_disable(&m_timer_send_id);                       // 关闭广播场的定时器
                
                // 我们不需要任何的事件，仅仅需要检测一下场的状态
                NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
                if (lf_is_field_exists()) {
                    nrf_drv_lpcomp_disable();
                    nrfx_timer_enable(&m_timer_send_id);                    // 打开广播场的定时器，继续模拟
                } else {
                    // 开启事件中断，让下次场事件可以正常出入
                    g_is_tag_emulating = false;                             // 重设模拟中的标志位
                    m_is_lf_emulating = false;
                    TAG_FIELD_LED_OFF()                                     // 确保关闭LF的场状态的指示灯
                    NRF_LPCOMP->INTENSET = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
                    sleep_timer_start(SLEEP_DELAY_MS_FIELD_125KHZ_LOST);    // 启动进入休眠的定时器
                    NRF_LOG_INFO("LF FIELD LOST");
                }
            }
            break;
        }
        default: {
            // Nothing to do.
            break;
        }
    }
}

/**
 * @brief LPCOMP event handler is called when LPCOMP detects voltage drop.
 *
 * This function is called from interrupt context so it is very important
 * to return quickly. Don't put busy loops or any other CPU intensive actions here.
 * It is also not allowed to call soft device functions from it (if LPCOMP IRQ
 * priority is set to APP_IRQ_PRIORITY_HIGH).
 */
static void lpcomp_event_handler(nrf_lpcomp_event_t event) {
    // 仅限于未启动低频模拟时，并且是上升沿事件才去启动模拟卡
    if (!m_is_lf_emulating && event == NRF_LPCOMP_EVENT_UP) {
        // 关闭休眠延时
        sleep_timer_stop();
        // 关闭比较器
        nrf_drv_lpcomp_disable();

        // 设置模拟状态标志位
        m_is_lf_emulating = true;
        g_is_tag_emulating = true;
        
        // 模拟卡状态应当关闭USB灯效
        g_usb_led_marquee_enable = false;

        // LED状态更新
        set_slot_light_color(2);
        TAG_FIELD_LED_ON()
        
        // 无论如何，每次场状态发现变化都需要重置发送的bit位置
        m_send_id_count = 0;
        m_bit_send_position = 0;
        m_is_send_first_edge = true;
        
        // 开启精准的硬件定时器去广播卡号
        nrfx_timer_enable(&m_timer_send_id);
        
        NRF_LOG_INFO("LF FIELD DETECTED");
    }
}

static void lf_sense_enable(void) {
    ret_code_t err_code;
    
    nrf_drv_lpcomp_config_t config = NRF_DRV_LPCOMP_DEFAULT_CONFIG;
    config.hal.reference = NRF_LPCOMP_REF_SUPPLY_1_16;
    config.input = LF_RSSI;
    config.hal.detection = NRF_LPCOMP_DETECT_UP;
    config.hal.hyst = NRF_LPCOMP_HYST_50mV;

    err_code = nrf_drv_lpcomp_init(&config, lpcomp_event_handler);
    APP_ERROR_CHECK(err_code);

    // TAG id broadcast
    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    err_code = nrfx_timer_init(&m_timer_send_id, &timer_cfg, timer_ce_handler);
    APP_ERROR_CHECK(err_code);
    nrfx_timer_extended_compare(&m_timer_send_id, NRF_TIMER_CC_CHANNEL2, nrfx_timer_us_to_ticks(&m_timer_send_id, LF_125KHZ_EM410X_BIT_CLOCK), NRF_TIMER_SHORT_COMPARE2_CLEAR_MASK, true);

    if (lf_is_field_exists() && !m_is_lf_emulating) {
        lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    }
}

static void lf_sense_disable(void) {
    nrfx_timer_uninit(&m_timer_send_id);    // 反初始化定时器
    nrfx_lpcomp_uninit();                   // 反初始化比较器
    m_is_lf_emulating = false;              // 设置为非模拟中状态
}

static enum  {
    LF_SENSE_STATE_NONE,
    LF_SENSE_STATE_DISABLE,
    LF_SENSE_STATE_ENABLE,
} m_lf_sense_state = LF_SENSE_STATE_NONE;

/**
 * @brief 切换LF场感应使能状态
 */
void lf_tag_125khz_sense_switch(bool enable) {
    // 初始化调制脚为输出
    nrf_gpio_cfg_output(LF_MOD);
    // 默认不短路天线（短路会导致RSSI无法判断）
    ANT_NO_MOD();
    
    // 首次执行或者是禁用状态，只允许初始化
    if (m_lf_sense_state == LF_SENSE_STATE_NONE || m_lf_sense_state == LF_SENSE_STATE_DISABLE) {
        if (enable) {
            m_lf_sense_state = LF_SENSE_STATE_ENABLE;
            lf_sense_enable();
        }
    } else {    // 其他情况只允许反初始化
        if (!enable) {
            m_lf_sense_state = LF_SENSE_STATE_DISABLE;
            lf_sense_disable();
        }
    }
}

/** @brief em410x加载数据
 * @param type      细化的标签类型
 * @param buffer    数据缓冲区
 */
int lf_tag_em410x_data_loadcb(tag_specific_type_t type, tag_data_buffer_t* buffer) {
    // 确保外部容量足够转换为信息结构体
    if (buffer->length >= LF_EM410X_TAG_ID_SIZE) {
        // 此处直接转换ID卡号为对应的bit数据流
        m_tag_type = type;
        m_id_bit_data = em410x_id_to_memory64(buffer->buffer);
        NRF_LOG_INFO("LF Em410x data load finish.");
    } else {
        NRF_LOG_ERROR("LF_EM410X_TAG_ID_SIZE too big.");
    }
    return LF_EM410X_TAG_ID_SIZE;
}

/** @brief ID卡保存卡号之前的回调
 * @param type      细化的标签类型
 * @param buffer    数据缓冲区
 * @return 需要保存的数据的长度，为0时表示不保存
 */
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t* buffer) {
    // 确保加载了此标签才允许保存
    if (m_tag_type != TAG_TYPE_UNKNOWN) {
        // 直接保存原本的卡包即可
        return LF_EM410X_TAG_ID_SIZE;
    } else {
        return 0;
    }
}

/** @brief ID卡保存卡号之前的回调
 * @param slot      卡槽号码
 * @param tag_type  细化的标签类型
 * @return 是否格式化成功，如果格式化成功，将返回true，否则返回false
 */
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id, must to align(4), more word...
    uint8_t tag_id[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x88 };
    // 将数据写进去flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info; // 获取专用卡槽FDS记录信息
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // 调用堵塞式的fds写入函数，将卡槽指定场类型的数据写入到flash中
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(tag_id) / 4, (uint8_t *)tag_id);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}
