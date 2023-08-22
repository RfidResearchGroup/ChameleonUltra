#include "math.h"
#include "nrf_gpio.h"
#include "hw_connect.h"
#include "bsp_delay.h"
#include "rgb_marquee.h"
#include "bsp_time.h"


#define NRF_LOG_MODULE_NAME rgb
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define PWM_MAX 1000 // PWM 最大值
#define LIGHT_LEVEL_MAX 99 // 亮度级别最大值
static nrf_drv_pwm_t pwm0_ins = NRF_DRV_PWM_INSTANCE(1);
nrf_pwm_values_individual_t pwm_sequ_val;//独立模式下的PWM控制4通道的占空比
nrf_pwm_sequence_t const seq = //配置pwm输出用的结构体
{
	.values.p_individual = &pwm_sequ_val,
	.length          = 4,
	.repeats         = 0,
	.end_delay       = 0
};
nrf_drv_pwm_config_t pwm_config = //PWM配置结构体
{
	.irq_priority = APP_IRQ_PRIORITY_LOWEST,
	.base_clock = NRF_PWM_CLK_1MHz,
	.count_mode = NRF_PWM_MODE_UP,
	.top_value = PWM_MAX,
	.load_mode = NRF_PWM_LOAD_INDIVIDUAL,//4个通道用四个值
	.step_mode = NRF_PWM_STEP_AUTO
};
static autotimer *timer;
static uint8_t ledblink6_step = 0;
static uint8_t ledblink1_step = 0;
extern bool g_usb_led_marquee_enable;


void rgb_marquee_init(void){
    timer = bsp_obtain_timer(0);
}

void rgb_marquee_stop(void){
	nrfx_pwm_stop(&pwm0_ins, true);
	nrfx_pwm_uninit(&pwm0_ins);	//关闭pwm输出
	ledblink6_step = 0;
	ledblink1_step = 0;
}

// reset RGB state machines to force a refresh of the LED color
void rgb_marquee_reset(void){
	ledblink6_step = 0;
	ledblink1_step = 0;
}

// 亮度转PWM值
uint16_t get_pwmduty(uint8_t light_level){
    return PWM_MAX-(PWM_MAX*pow(((double)light_level/LIGHT_LEVEL_MAX),2.2));
}

//4灯同亮度水平移动循环（不返回）
//COLOR 0-R,1-G,2-B
void ledblink1(uint8_t color, uint8_t dir){
	static uint8_t startled = 0;
	static uint8_t setled = 0;
	uint32_t* led_pins_arr;

	if (!g_usb_led_marquee_enable && ledblink1_step != 0) {
		startled = 0;
		setled = 0;
		rgb_marquee_stop();
		return;
	}

	//处理方向
	if(dir == 0){
		led_pins_arr = hw_get_led_array();
	} else{
		led_pins_arr = hw_get_led_reversal_array();
	}

	if (ledblink1_step == 0) {
		//调整颜色
		set_slot_light_color(color);
		pwm_sequ_val.channel_0 = 1;
		pwm_sequ_val.channel_1 = 1;
		pwm_sequ_val.channel_2 = 1;
		pwm_sequ_val.channel_3 = 1;
		bsp_set_timer(timer, 0);
		ledblink1_step = 1;

		// 重置USB开启通信时的灯的状态
		ledblink6_step = 0;
	}

	if (ledblink1_step == 1) {
		setled = startled;
		for(uint8_t i=0;i<4;i++){
			pwm_config.output_pins[i] = led_pins_arr[setled];
			setled++;
			if(setled>7)setled=0;
		}
		startled++;
		if(startled>7)startled = 0;
		nrfx_pwm_uninit(&pwm0_ins);
		nrf_drv_pwm_init(&pwm0_ins, &pwm_config, NULL);
		nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);

		bsp_set_timer(timer, 0);
		ledblink1_step = 2;
	}

	if (ledblink1_step == 2) {
		if (!(NO_TIMEOUT_1MS(timer, 80))) {
			ledblink1_step = 1;
		}
	}
}

//4灯拖尾水平移动循环（不返回），包含尾部消失和头部缓入
//dir 0-从1卡槽到8卡槽，1-从8卡槽到1卡槽 (方向，结束点由end参数决定)
//end 要扫描的灯数量，和方向一起决定最终动画区域
void ledblink2(uint8_t color,uint8_t dir, uint8_t end){
	uint8_t startled = 0;
	uint8_t setled = 0;
	uint8_t leds2turnon = 0;
	uint8_t i = 0;
	uint32_t * led_pins_arr;
	//处理方向
	if(dir == 0){
		led_pins_arr = hw_get_led_array();
	}
	else{
		led_pins_arr = hw_get_led_reversal_array();
	}
	
	//调整颜色
	set_slot_light_color(color);
	pwm_sequ_val.channel_3 = 1; //最亮的
	pwm_sequ_val.channel_2 = 600;
	pwm_sequ_val.channel_1 = 880;
	pwm_sequ_val.channel_0 = 980;//最暗的
	while(1) {
		//关闭所有通道
		pwm_config.output_pins[0] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
		
		setled = startled;
		if(setled<3){//正向期间，0，1，2的时候只能点亮前几个led
			//首先确定能点亮几个灯
			leds2turnon = setled+1;//1，2，3
			//然后设置pwm输出通道
			for(i=0;i<leds2turnon;i++){
				pwm_config.output_pins[3-i] = led_pins_arr[setled-i];
			}	
		}else if (setled<=7){//正向期间，大于4小于8的时候能点亮4个led
			//设置pwm输出通道
			for(i=0;i<4;i++){
				pwm_config.output_pins[3-i] = led_pins_arr[setled];
				setled--;
			}
		}else if (setled>7&&setled<=10){//正向期间，8.9.10的时候只能点亮后几个led
			//首先确定能点亮几个灯
			leds2turnon = 11-setled;
			//然后设置pwm输出通道
			for(i=0;i<leds2turnon;i++){
				pwm_config.output_pins[i] = led_pins_arr[setled-3+i];
			}	
			
		}else{//正向期间，达到11
			//什么都不干
		}
		//处理停止条件
		if(startled>=end){
			//计算需要隐藏几个灯
			leds2turnon = startled-end;
			//把超出去的都隐藏了
			for(i=0;i<leds2turnon;i++){
				pwm_config.output_pins[3-i] = NRF_DRV_PWM_PIN_NOT_USED;
			}
			//重新设置指定位置是最亮的
			if(end <= 7){
				pwm_config.output_pins[3] = led_pins_arr[end];
			}
			
		}
		nrfx_pwm_uninit(&pwm0_ins);
		nrf_drv_pwm_init(&pwm0_ins, &pwm_config, NULL);
		nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
		bsp_delay_ms(40);
		startled++;
		if(startled-end>=4)break;
		if(startled>11)break;
	}
}

//切换卡槽动画
//led_up 要点亮的led
//color_led_up 要点亮的led的颜色 0-R,1-G,2-B
//led_down 要熄灭的led
//color_led_down 要熄灭的led的颜色 0-R,1-G,2-B
volatile bool callback_waiting = 0;
static void ledblink3_pwm_callback(nrfx_pwm_evt_type_t event_type){
	if (event_type == NRF_DRV_PWM_EVT_FINISHED) {
		callback_waiting = 1;
	}
}
void ledblink3(uint8_t led_down,uint8_t color_led_down, uint8_t led_up, uint8_t color_led_up){
	int16_t light_level= 99;//led亮度值
	uint32_t* led_pins = hw_get_led_array();
	if(led_down>=0 &&led_down<=7){
		//先处理要熄灭的
		pwm_config.output_pins[0] = led_pins[led_down];
		pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
		while(light_level >= 0){
			//处理亮度
			pwm_sequ_val.channel_0 = get_pwmduty(light_level);
		
			nrfx_pwm_uninit(&pwm0_ins);	//关闭pwm输出
			
			if(led_up>=0 &&led_up<=7){
				nrf_gpio_pin_clear(led_pins[led_up]);
			}
			
			set_slot_light_color(color_led_down);
			
			nrf_drv_pwm_init(&pwm0_ins, &pwm_config, ledblink3_pwm_callback);
			nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
			
			while(callback_waiting == 0);//等待pwm模块输出完成
			bsp_delay_us(1234);
			callback_waiting = 0;
			light_level --;
		}
	}
	if(led_up>=0 &&led_up<=7){
		//处理要点亮的
		pwm_config.output_pins[0] = led_pins[led_up];
		pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
		light_level  = 0;
		while(light_level < 99){
			//处理亮度
			pwm_sequ_val.channel_0 = get_pwmduty(light_level);
		
			nrfx_pwm_uninit(&pwm0_ins);	//关闭pwm输出
			
			if(led_down>=0 &&led_down<=7){
				nrf_gpio_pin_clear(led_pins[led_down]);
			}
			
			set_slot_light_color(color_led_up);
				
			nrf_drv_pwm_init(&pwm0_ins, &pwm_config, ledblink3_pwm_callback);
			nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
			
			while(callback_waiting == 0);//等待pwm模块输出完成
			bsp_delay_us(1234);
			callback_waiting = 0;
			light_level ++;
		}
	}
}

//4灯拖尾水平移动循环（不返回），不包含尾部消失但是包含头部缓入（用于弹球类型动画）
//dir 0-从1卡槽到8卡槽，1-从8卡槽到1卡槽 (方向，结束点由end参数决定)
//end 要扫描的灯数量，和方向一起决定最终动画区域
//start_light stop_light 0-99 表示渐变亮度
void ledblink4(uint8_t color,uint8_t dir, uint8_t end,uint8_t start_light,uint8_t stop_light){
	uint8_t startled = 0;
	uint8_t setled = 0;
	uint8_t leds2turnon = 0;
	uint8_t i = 0;
	uint32_t * led_pins_arr;
	volatile double light_cnd;
	//处理方向
	if(dir == 0){
		led_pins_arr = hw_get_led_array();
	}
	else{
		led_pins_arr = hw_get_led_reversal_array();
	}
	
	//调整颜色
	set_slot_light_color(color);
	while(1) {
		//设置亮度
		//当前亮度系数
		//start经过end次数达到stop
		light_cnd = (((double)stop_light-(double)start_light)/end)*startled+start_light;
		pwm_sequ_val.channel_3 = get_pwmduty((uint8_t)(0.99*light_cnd));//1; //最亮的
		pwm_sequ_val.channel_2 = get_pwmduty((uint8_t)(0.60*light_cnd));//600;
		pwm_sequ_val.channel_1 = get_pwmduty((uint8_t)(0.30*light_cnd));//880;
		pwm_sequ_val.channel_0 = get_pwmduty((uint8_t)(0.01*light_cnd));//980;//最暗的
		//关闭所有通道
		pwm_config.output_pins[0] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
		
		setled = startled;
		if(setled<3){//正向期间，0，1，2的时候只能点亮前几个led
			//首先确定能点亮几个灯
			leds2turnon = setled+1;//1，2，3
			//然后设置pwm输出通道
			for(i=0;i<leds2turnon;i++){
				pwm_config.output_pins[3-i] = led_pins_arr[setled-i];
			}
		}else if (setled<=7){//正向期间，大于4小于8的时候能点亮4个led
			//设置pwm输出通道
			for(i=0;i<4;i++){
				pwm_config.output_pins[3-i] = led_pins_arr[setled];
				setled--;
			}
		}else if (setled>7&&setled<=10){//正向期间，8.9.10的时候只能点亮后几个led
			//首先确定能点亮几个灯
			leds2turnon = 11-setled;
			//然后设置pwm输出通道
			for(i=0;i<leds2turnon;i++){
				pwm_config.output_pins[i] = led_pins_arr[setled-3+i];
			}	
			
		}else{//正向期间，达到11
			//什么都不干
		}
		//处理停止条件
		if(startled==end){
			break;
		}
		nrfx_pwm_uninit(&pwm0_ins);
		nrf_drv_pwm_init(&pwm0_ins, &pwm_config, NULL);
		nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
		bsp_delay_ms(50);
		startled++;
		if(startled-end>=4)break;
		if(startled>11)break;
	}
}

//单灯水平移动
//color 要点亮的led的颜色 0-R,1-G,2-B
//start 启动灯位
//stop 停止灯位
void ledblink5(uint8_t color,uint8_t start, uint8_t stop){
	uint8_t setled = start;
	uint32_t * led_pins = hw_get_led_array();
	//设置亮度
	pwm_sequ_val.channel_3 = 0;
	pwm_sequ_val.channel_2 = 0;
	pwm_sequ_val.channel_1 = 0;
	pwm_sequ_val.channel_0 = get_pwmduty(99);
	//调整颜色
	set_slot_light_color(color);
	while(setled < (start < stop ? stop + 1 : stop - 1)) {
		//关闭所有通道
		pwm_config.output_pins[0] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
		pwm_config.output_pins[0] = led_pins[setled];
		nrfx_pwm_uninit(&pwm0_ins);
		nrf_drv_pwm_init(&pwm0_ins, &pwm_config, NULL);
		nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
		bsp_delay_ms(50);
		setled = start < stop ? setled + 1 : setled - 1;
	}
}


//充电动画
//perc 电池当前百分比 0-4 4代表满电呼吸灯
volatile bool callback_waiting6 = 0;
void ledblink6_pwm_callback(nrfx_pwm_evt_type_t event_type){
	if (event_type == NRF_DRV_PWM_EVT_FINISHED) {
		callback_waiting6 = 1;
	}
}
void ledblink6(void){
	uint32_t* led_array = hw_get_led_array();
	const uint16_t delay_time = 25;
	static int16_t light_level = 99; //led亮度值

	if (!g_usb_led_marquee_enable && ledblink6_step != 0) {
		light_level = 99;
		callback_waiting6 = 0;
		rgb_marquee_stop();
		return;
	}

	if (ledblink6_step == 0) {
		set_slot_light_color(0);
		for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
			nrf_gpio_pin_clear(led_array[i]);
		}
		pwm_config.output_pins[0] = led_array[2];
		pwm_config.output_pins[1] = led_array[3];
		pwm_config.output_pins[2] = led_array[4];
		pwm_config.output_pins[3] = led_array[5];
		ledblink6_step = 1;

		// 重置USB未开启通信时的灯的状态
		ledblink1_step = 0;
	}
	
	if (ledblink6_step == 1) {
		light_level  = 0;
		ledblink6_step = 2;
	}

	if (ledblink6_step == 2 || ledblink6_step == 3 || ledblink6_step == 4) {
		if (light_level <= 99) {
			if (ledblink6_step == 2) {
				//处理亮度
				pwm_sequ_val.channel_0 = get_pwmduty(light_level);
				pwm_sequ_val.channel_1 = pwm_sequ_val.channel_0;
				pwm_sequ_val.channel_2 = pwm_sequ_val.channel_0;
				pwm_sequ_val.channel_3 = pwm_sequ_val.channel_0;
				nrfx_pwm_uninit(&pwm0_ins);	//关闭pwm输出
				set_slot_light_color(1);
				nrf_drv_pwm_init(&pwm0_ins, &pwm_config, ledblink6_pwm_callback);
				nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
				ledblink6_step = 3;
			}
			if (ledblink6_step == 3) {	//等待pwm模块输出完成
				if (callback_waiting6 != 0) {
					ledblink6_step = 4;
					bsp_set_timer(timer, 0);
				}
			}
			if (ledblink6_step == 4) {
				if (!NO_TIMEOUT_1MS(timer, delay_time)) {
					callback_waiting = 0;
					light_level++;
					ledblink6_step = 2;
				}
			}
		} else {
			ledblink6_step = 5;
		}
	}

	if (ledblink6_step == 5) {
		light_level = 99;
		ledblink6_step = 6;
	}

	if (ledblink6_step == 6 || ledblink6_step == 7 || ledblink6_step == 8) {
		if (light_level >=0) {
			if (ledblink6_step == 6) {
				//处理亮度
				pwm_sequ_val.channel_0 = get_pwmduty(light_level);
				pwm_sequ_val.channel_1 = pwm_sequ_val.channel_0;
				pwm_sequ_val.channel_2 = pwm_sequ_val.channel_0;
				pwm_sequ_val.channel_3 = pwm_sequ_val.channel_0;
				nrfx_pwm_uninit(&pwm0_ins);	//关闭pwm输出
				set_slot_light_color(1);
				nrf_drv_pwm_init(&pwm0_ins, &pwm_config, ledblink6_pwm_callback);
				nrf_drv_pwm_simple_playback(&pwm0_ins, &seq, 1,NRF_DRV_PWM_FLAG_LOOP);
				ledblink6_step = 7;
			}
			if (ledblink6_step == 7) {	//等待pwm模块输出完成
				if (callback_waiting6 != 0) {
					ledblink6_step = 8;
					bsp_set_timer(timer, 0);
				}
			}
			if (ledblink6_step == 8) {
				if (!NO_TIMEOUT_1MS(timer, delay_time)) {
					callback_waiting = 0;
					light_level--;
					ledblink6_step = 6;
				}
			}
		} else {
			ledblink6_step = 0;
		}
	}
}

/**
 * @brief 当前的灯效是否使能
 * 
 * @return true 使能状态，处于灯效闪烁状态
 * @return false 禁止状态，处于普通卡槽指示灯状态
 */
bool is_rgb_marquee_enable(void) {
	return g_usb_led_marquee_enable;
}