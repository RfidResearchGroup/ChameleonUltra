#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_gpio.h"

#define NRF_LOG_MODULE_NAME timeslot
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "timeslot.h"


/**Constants for timeslot API
*/
static nrf_radio_request_t  m_timeslot_request;
static uint32_t             m_slot_length;
static nrf_radio_signal_callback_return_param_t signal_callback_return_param;
static timeslot_callback_t  m_callback = NULL;
static volatile bool m_is_timeslot_running = false;
static volatile bool m_is_timeslot_working = false;


/**@brief Configure next timeslot event in earliest configuration
 */
void configure_next_event_earliest(void)
{
    m_timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;       // 首次请求timeslot必须要
    m_timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_NO_GUARANTEE;  // 不必自动使能外部高频晶振
    m_timeslot_request.params.earliest.priority    = NRF_RADIO_PRIORITY_HIGH;           // 必须使用高优先级
    m_timeslot_request.params.earliest.length_us   = m_slot_length;                     // timeslot时长
    m_timeslot_request.params.earliest.timeout_us  = NRF_RADIO_EARLIEST_TIMEOUT_MAX_US; // 等待timeslot超时
}

/**@brief Request next timeslot event in earliest configuration
 */
uint32_t request_next_event_earliest(void)
{
    configure_next_event_earliest();
    return sd_radio_request(&m_timeslot_request);
}


/**@brief Timeslot signal handler
 */
static void t55xx_soc_evt_handler(uint32_t evt_id, void * p_context)
{
    //NRF_LOG_INFO("t55xx_soc_evt_handler: %d", evt_id);
    uint32_t err_code;
    switch (evt_id) {
        case NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN:
            // No implementation needed
            break;
        case NRF_EVT_RADIO_SESSION_IDLE:    // 会话空闲，没有时序需要穿插处理
            //NRF_LOG_INFO("NRF_EVT_RADIO_SESSION_IDLE");
            break;
        case NRF_EVT_RADIO_SESSION_CLOSED:  // 会话被关闭
            // No implementation needed, session ended
            //NRF_LOG_INFO("NRF_EVT_RADIO_SESSION_CLOSED");
            m_is_timeslot_working = false;
            break;
        case NRF_EVT_RADIO_BLOCKED: // Fall through
        case NRF_EVT_RADIO_CANCELED:
            err_code = request_next_event_earliest();
            APP_ERROR_CHECK(err_code);
            break;
        default:
            break;
    }
}
/* Define a nrf_sdh_soc event observer to receive SoftDevice system events. */
NRF_SDH_SOC_OBSERVER(m_sys_obs, 0, t55xx_soc_evt_handler, NULL);


/**@brief Timeslot event handler
 */
nrf_radio_signal_callback_return_param_t * radio_callback(uint8_t signal_type)
{
    //NRF_LOG_INFO("radio_callback: %d", signal_type);
    switch(signal_type) {
        case NRF_RADIO_CALLBACK_SIGNAL_TYPE_START:
            signal_callback_return_param.params.request.p_next = NULL;
            signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE;

            // 请求成功，设置一下标志位
            m_is_timeslot_working = true;
            break;

        default:
            // No implementation needed
            break;
    }
    return (&signal_callback_return_param);
}

/**
 * 请求一段时序进行高精度操作
 */
void request_timeslot(uint32_t time_us, timeslot_callback_t callback, bool wait_end) {
    ret_code_t err_code;

    // 确保同一时间只有一个
    APP_ERROR_CHECK_BOOL(!m_is_timeslot_running);
    m_is_timeslot_running = true;

    m_slot_length = time_us;    // 配置申请的时间
    m_callback    = callback;   // 配置时间申请到之后执行的操作

    // 打开会话
    err_code = sd_radio_session_open(radio_callback);
    APP_ERROR_CHECK(err_code);

    // 请求时序
    err_code = request_next_event_earliest();
    APP_ERROR_CHECK(err_code);

    // 堵塞等待时序请求成功
    while(!m_is_timeslot_working) {
        NRF_LOG_PROCESS();
    }

    // 进入临界点
	NVIC_DisableIRQ(RADIO_IRQn);
	NVIC_DisableIRQ(TIMER0_IRQn);
	NVIC_DisableIRQ(TIMER2_IRQn);
	NVIC_DisableIRQ(GPIOTE_IRQn);
	NVIC_DisableIRQ(MWU_IRQn);
	NVIC_DisableIRQ(RTC1_IRQn);

    // 请求时序成功，快处理任务
    if (m_callback != NULL) {
        m_callback();       // 执行任务
        m_callback = NULL;  // 销毁记录
    }

    // 退出临界点
	NVIC_EnableIRQ(GPIOTE_IRQn);
	NVIC_EnableIRQ(RTC1_IRQn);
	NVIC_EnableIRQ(MWU_IRQn);

    // 关闭会话并且等待关闭完成
    err_code = sd_radio_session_close();
    APP_ERROR_CHECK(err_code);
    while(m_is_timeslot_working) {
        __NOP();
    }

    // 任务处理完成，进行收尾工作
    m_is_timeslot_running = false;

    //NRF_LOG_INFO("request timeslot done.");
}

/**
 * 开始进行高精度操作
 */
void timeslot_start(uint32_t time_us) {
    ret_code_t err_code;

    // 确保同一时间只有一个
    APP_ERROR_CHECK_BOOL(!m_is_timeslot_running);
    m_is_timeslot_running = true;

    m_slot_length = time_us * 1000;    // 配置申请的时间

    // 打开会话
    err_code = sd_radio_session_open(radio_callback);
    APP_ERROR_CHECK(err_code);

    // 请求时序
    err_code = request_next_event_earliest();
    APP_ERROR_CHECK(err_code);

    // 堵塞等待时序请求成功
    while(!m_is_timeslot_working) {
        NRF_LOG_PROCESS();
    }

    // 进入临界点
	NVIC_DisableIRQ(RADIO_IRQn);
	NVIC_DisableIRQ(TIMER0_IRQn);
	NVIC_DisableIRQ(TIMER2_IRQn);
	//NVIC_DisableIRQ(GPIOTE_IRQn);
	NVIC_DisableIRQ(MWU_IRQn);
	NVIC_DisableIRQ(RTC1_IRQn);
    //NRF_LOG_INFO("timeslot start.");
}

/**
 * 停止高精度操作
 */
void timeslot_stop(void) {
    ret_code_t err_code;

    // 确保已经有一个timeslot运行
    APP_ERROR_CHECK_BOOL(m_is_timeslot_running);
    m_is_timeslot_running = false;

    // 退出临界点
	//NVIC_EnableIRQ(GPIOTE_IRQn);
	NVIC_EnableIRQ(RTC1_IRQn);
	NVIC_EnableIRQ(MWU_IRQn);

    // 关闭会话并且等待关闭完成
    err_code = sd_radio_session_close();
    APP_ERROR_CHECK(err_code);
    while(m_is_timeslot_working) {
        __NOP();
    }

    // 任务处理完成，进行收尾工作
    m_is_timeslot_running = false;
    //NRF_LOG_INFO("timeslot stop.");
}
