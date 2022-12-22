#include "lf_reader_data.h"
#include "nrf_drv_timer.h"


RIO_CALLBACK_S RIO_callback;		 		// 创建实例
uint8_t RIO_callback_state;		 			// 记录状态


void register_rio_callback(RIO_CALLBACK_S P) // 注册回调函数
{
	RIO_callback = P;
	RIO_callback_state = 1;
}

void blank_function(void) {
	// 这就是一个空的函数，
	// 啥都不用做
}

void unregister_rio_callback(void) {
	RIO_callback_state = 0;
	RIO_callback = blank_function;
}

// GPIO中断，就是RIO引脚
void GPIO_INT0_IRQHandler(void)
{
	if(RIO_callback_state == 1){
		RIO_callback();
	}
}


extern nrfx_timer_t m_timer_lf_reader;

// 获得计数器的值
uint32_t get_lf_counter_value(void) {
    return nrfx_timer_capture(&m_timer_lf_reader, NRF_TIMER_CC_CHANNEL1);
}

// 清除计数器的值
void clear_lf_counter_value(void) {
    nrfx_timer_clear(&m_timer_lf_reader);
}
