#include "bsp_delay.h"
#include "bsp_time.h"
#include "nrf_delay.h"


//初始化延迟函数
void bsp_delay_init(void)
{
}

//延时nms
//注意nms的范围
void bsp_delay_ms(uint16_t nms)
{
	nrf_delay_us(nms * 1000);
}

//延时nus
//nus为要延时的us数.
void bsp_delay_us(uint32_t nus)
{
	nrf_delay_us(nus);
}
