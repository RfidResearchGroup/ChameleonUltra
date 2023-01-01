#include <string.h>
#include <stdbool.h>
#include <nrf_gpio.h>

#include "nrf_drv_spi.h"
#include "nrf_gpio.h"
#include "app_error.h"

#include "rfid_main.h"
#include "rc522.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "app_status.h"
#include "hex_utils.h"
#include "crc_utils.h"

#define NRF_LOG_MODULE_NAME rc522
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define RC522_DOSEL nrf_gpio_pin_clear(HF_SPI_SELECT)
#define RC522_UNSEL nrf_gpio_pin_set(HF_SPI_SELECT)


// CRC 14a计算器，当MCU性能太弱，或者MCU繁忙时，可以使用522计算CRC
static uint8_t m_crc_computer = 0;
// 当前是否初始化了读卡器
static bool m_reader_is_init = false;

// 通信超时
static uint16_t g_com_timeout_ms = DEF_COM_TIMEOUT;
static autotimer* g_timeout_auto_timer;

// RC522使用的SPI
#define SPI_INSTANCE  0 /**< SPI instance index. */
static const nrf_drv_spi_t s_spiHandle = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);	// SPI instance

#define NO_OPT __attribute__((optimize("-O0")))

/**
* @brief  ：读寄存器
* @param  ：Address：寄存器地址
* @retval ：寄存器内的值
*/
uint8_t read_register_single(uint8_t Address)
{
	RC522_DOSEL;
    
    Address = (uint8_t)(((Address << 1) & 0x7E) | 0x80);
    
    NRF_SPI0->TXD = Address;
    while ( NRF_SPI0->EVENTS_READY == 0 );
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;
    
    NRF_SPI0->TXD = Address;
    while ( NRF_SPI0->EVENTS_READY == 0 );
    NRF_SPI0->EVENTS_READY = 0;
    Address = NRF_SPI0->RXD;
	
	RC522_UNSEL;
	
	return Address;
}

void read_register_buffer(uint8_t Address, uint8_t *pInBuffer, uint8_t len)
{ 
	RC522_DOSEL;
	
	Address = (((Address << 1) & 0x7E) | 0x80);
    
    NRF_SPI0->TXD = Address;
    while ( NRF_SPI0->EVENTS_READY == 0 );  // 等待传输结束
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;    // 读取一次，给一个电平
    
    uint8_t i = 0;
    do {
        // 然后开始收数据
        NRF_SPI0->TXD = Address;
        while ( NRF_SPI0->EVENTS_READY == 0 );  // 等待传输结束
        NRF_SPI0->EVENTS_READY = 0;
        pInBuffer[i] = NRF_SPI0->RXD;    // 读取一次，给一个电平
    } while(++i < len);

	RC522_UNSEL;
}

/**
* @brief  ：写寄存器
* @param  ：Address：寄存器地址
*			value: 将要写入的值
*/
void NO_OPT write_register_single(uint8_t Address, uint8_t value)
{
	RC522_DOSEL;

	Address = ((Address << 1) & 0x7E);
    
    // 先传地址
    NRF_SPI0->TXD = Address;
    while ( NRF_SPI0->EVENTS_READY == 0 );
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;
    
    // 再传要写的值
    NRF_SPI0->TXD = value;
    while ( NRF_SPI0->EVENTS_READY == 0 );
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;
    
	RC522_UNSEL;
}

void write_register_buffer(uint8_t Address, uint8_t *values, uint8_t len)
{
	RC522_DOSEL;
	
	Address = ((Address << 1) & 0x7E);
    
    NRF_SPI0->TXD = Address;
    while ( NRF_SPI0->EVENTS_READY == 0 );
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;
    
    uint8_t i = 0;
    do {
        // 然后疯狂发数据
        NRF_SPI0->TXD = values[i];
        while ( NRF_SPI0->EVENTS_READY == 0 );
        NRF_SPI0->EVENTS_READY = 0;
        (void)NRF_SPI0->RXD;
    } while(++i < len);
    
	RC522_UNSEL;
}

/**
* @brief  ：寄存器功能开关
* @param  ：reg：寄存器地址
*			mask: 开关范围
*/
inline void set_register_mask(uint8_t reg, uint8_t mask)
{
	write_register_single(reg, read_register_single(reg) | mask);  // set bit mask
}

/**
* @brief  ：寄存器功能开关
* @param  ：reg：寄存器地址
*			mask: 开关范围
*/
inline void clear_register_mask(uint8_t reg, uint8_t mask)
{
	write_register_single(reg, read_register_single(reg) & ~mask);  // clear bit mask
}

/**
* @brief  初始化读卡器
* @retval 无
*/
void pcd_14a_reader_init(void)
{
	// 确保只初始化一次
	if (!m_reader_is_init) {
		// 标志为已经初始化状态
		m_reader_is_init = true;

		// 初始化NSS脚GPIO
		nrf_gpio_cfg_output(HF_SPI_SELECT);
		
		// 初始化SPI
		ret_code_t errCode;
		
		nrf_drv_spi_config_t spiConfig = NRF_DRV_SPI_DEFAULT_CONFIG;				// 使用SPI默认配置
		// 配置SPI端口，注意CSN不要在这设置，另外用GPIO口控制
		spiConfig.miso_pin = HF_SPI_MISO;
		spiConfig.mosi_pin = HF_SPI_MOSI;
		spiConfig.sck_pin = HF_SPI_SCK;
		spiConfig.mode = NRF_DRV_SPI_MODE_0;
		spiConfig.frequency = NRF_DRV_SPI_FREQ_8M;
		// 配置为堵塞型操作
		errCode = nrf_drv_spi_init(&s_spiHandle, &spiConfig, NULL, NULL);
		APP_ERROR_CHECK(errCode);

		// 初始化定时器
		// 这个定时器初始化后就不释放了，始终需要占用着
		g_timeout_auto_timer = bsp_obtain_timer(0);
	}
}

/**
* @brief  ：重置读卡器
* @retval ：状态值HF_TAG_OK，成功
*/
void pcd_14a_reader_reset(void)
{
	// 确保已经初始化再进行通信与软重置
	if (m_reader_is_init) {
		// 软重置 522
		write_register_single(CommandReg, PCD_IDLE);
		write_register_single(CommandReg, PCD_RESET);
		
		// 开关天线
		clear_register_mask(TxControlReg, 0x03);
		set_register_mask(TxControlReg, 0x03);
		
		// 禁用522的定时器，使用MCU的定时器超时
		write_register_single(TModeReg, 0x00);
		
		// 调制发送信号为100%ASK
		write_register_single(TxAutoReg, 0x40);
		// 定义发送和接收常用模式 和Mifare卡通讯，CRC初始值0x6363
		write_register_single(ModeReg, 0x3D);
		
		// 然后默认不使能天线
		// 请不要持续使能高频的天线
		pcd_14a_reader_antenna_off();
	}
}

/**
* @brief  反初始化读卡器
* @retval 无
*/
void pcd_14a_reader_uninit(void)
{
	// 确保已经初始化过设备了，再进行反初始化
	if (m_reader_is_init) {
        m_reader_is_init = false;
		bsp_return_timer(g_timeout_auto_timer);
    	nrf_drv_spi_uninit(&s_spiHandle);
	}
}

/**
* @brief  MF522 通信超时配置
* @param  ：timeout_ms：超时值
*
* @retval 无
*/
void pcd_14a_reader_timeout_set(uint16_t timeout_ms)
{
	g_com_timeout_ms = timeout_ms;
}

/**
* @brief  MF522 通信超时获取
*
* @retval 超时值
*/
uint16_t pcd_14a_reader_timeout_get() {
	return g_com_timeout_ms;
}

/**
* @brief  ：通过RC522和ISO14443卡通讯
* @param  ：Command：RC522命令字
*          pIn：通过RC522发送到卡片的数据
*          InLenByte：发送数据的字节长度
*          pOut：接收到的卡片返回数据
*          pOutLenBit：返回数据的位长度
* @retval ：状态值MI_OK，成功
*/
uint8_t pcd_14a_reader_bytes_transfer(uint8_t Command, uint8_t* pIn, uint8_t  InLenByte, uint8_t* pOut, uint16_t* pOutLenBit, uint16_t maxOutLenBit) 
{
	uint8_t	status 		= HF_ERRSTAT;
	uint8_t	waitFor 	= 0x00;
	uint8_t	lastBits 	= 0;
	uint8_t	n 			= 0;
	uint8_t	pcd_err_val = 0;
	uint8_t	not_timeout	= 0;
	// 重置接收到的数据的长度
	*pOutLenBit			= 0;
	
	switch(Command) {
		case PCD_AUTHENT: 						//	Mifare认证
			waitFor = 0x10;						//	认证寻卡等待时候 查询空闲中断标志位
			break;
		
		case PCD_TRANSCEIVE:
			waitFor = 0x30;						//	寻卡等待时候 查询接收中断标志位与 空闲中断标志位
			break;
	}

	write_register_single(CommandReg,		PCD_IDLE); 		//	置位FlushBuffer清除内部FIFO的读和写指针以及ErrReg的BufferOvfl标志位被清除
	clear_register_mask(ComIrqReg,  	0x80); 			//	Set1该位清零时，CommIRqReg的屏蔽位清零
	set_register_mask(FIFOLevelReg, 	0x80); 			//	写空闲命令

	write_register_buffer(FIFODataReg, pIn, InLenByte);	// 写数据进FIFOdata
	write_register_single(CommandReg, Command); 			// 写命令

	if (Command == PCD_TRANSCEIVE) {
		set_register_mask(BitFramingReg, 0x80);		// StartSend置位启动数据发送 该位与收发命令使用时才有效
	}
	
	if (pOut == NULL) {
		// 如果开发者不需要接收数据，那么在发送完成后直接返回！
		while((read_register_single(Status2Reg) & 0x07) == 0x03);
		return HF_TAG_OK;
	}

	bsp_set_timer(g_timeout_auto_timer, 0);			// 在启动操作前先归零超时计数器
	
	do {
		n = read_register_single(ComIrqReg);				// 读取通信中断寄存器，判断当前的IO任务是否完成！
		not_timeout = NO_TIMEOUT_1MS(g_timeout_auto_timer, g_com_timeout_ms);
	} while (not_timeout && (!(n & waitFor)));	// 退出条件：超时中断，与写空闲命令中断
	// NRF_LOG_INFO("N = %02x\n", n);

	if (Command == PCD_TRANSCEIVE) {
		clear_register_mask(BitFramingReg, 0x80);		// 清理允许StartSend位与比特长度位
	}
	
	// 是否接收超时
	if (not_timeout) {
		// 先判断是否有错误寄存器的置位
		if (n & 0x02) {
			// 有错误发生
			// 读错误标志寄存器 BufferOfI CollErr ParityErr ProtocolErr
			pcd_err_val = read_register_single(ErrorReg);
			// 检测接收是否有异常
			if (pcd_err_val & 0x01) {				// ProtocolErr 错误仅在下述两种情况下出现：
				if (Command == PCD_AUTHENT) {		// 在MFAuthent命令执行期间，若一个数据流收到的字节数错误则该位置位
					// 因此我们需要处理好，假设是验证过程中出现问题，那么我们需要认为此乃正常情况
					status = MF_ERRAUTH;
				} else {							// 如果SOF出错，则该位置位，接收器启动阶段自动清零，仅在106kBd速率下有效
					NRF_LOG_INFO("Protocol error\n");
					status = HF_ERRSTAT;
				}
			} else if (pcd_err_val & 0x02) {
				// 检测是否有奇偶错误
				NRF_LOG_INFO("Parity error\n");
				status = HF_ERRPARITY;
			} else if (pcd_err_val & 0x04) {		// 检测是否有CRC错误
				NRF_LOG_INFO("CRC error\n");
				status = HF_ERRCRC;
			} else if (pcd_err_val & 0x08) {  		// 检测标签是否有冲突
				NRF_LOG_INFO("Collision tag\n");
				status = HF_COLLISION;
			} else {								// 有其他的未处理的异常
				NRF_LOG_INFO("HF error: 0x%0x2\n", pcd_err_val);
				status = HF_ERRSTAT;
			}
		} else {
			// 无错误发生
			// NRF_LOG_INFO("COM OK\n");
			if (Command == PCD_TRANSCEIVE) {
				n = read_register_single(FIFOLevelReg); 							// 读FIFO中保存的字节数
				if (n == 0) { n = 1; }
                
				lastBits = read_register_single(Control522Reg) & 0x07; 			// 最后接收到得字节的有效位数
                
				if (lastBits) { *pOutLenBit = (n - 1) * 8 + lastBits; } // N个字节数减去1（最后一个字节）+ 最后一位的位数 读取到的数据总位数
				else { *pOutLenBit = n * 8; } 							// 最后接收到的字节整个字节有效
				
				if (*pOutLenBit <= maxOutLenBit) {
					// 将FIFO中的所有数据读取出来
					read_register_buffer(FIFODataReg, pOut, n);
					// 传输指令，读到正常的数据才能认为成功！
					status = HF_TAG_OK;
				} else {
					NRF_LOG_INFO("pcd_14a_reader_bytes_transfer receive response overflow: %d, max = %d\n", *pOutLenBit, maxOutLenBit);
					// 我们不能把有问题的数据传出去，这暂时没意义
					*pOutLenBit = 0;
					// 既然数据有问题，那就直接通知上层，告知一下
					status = HF_ERRSTAT;
				}
			} else {
				// 非传输指令，执行完成无错误就认为成功！
				status = HF_TAG_OK;
			}
		}
	} else {
		status = HF_TAG_NO;
		// NRF_LOG_INFO("Tag lost(timeout).\n");
	}
	
	if (status != HF_TAG_OK) {
		// 如果有某些操作异常的话，
		// 我们可能需要清除 MFCrypto1On 这个寄存器标志，
		// 因为可能是因为验证过导致的错误加密的通信
		clear_register_mask(Status2Reg, 0x08);
	}
	
	// NRF_LOG_INFO("Com status: %d\n", status);
	return status;
}

/**
* @brief  ：通过RC522和ISO14443卡通讯
* @param
*          pTx		：通过RC522发送到卡片的数据
*          szTxBits	：发送数据的比特长度
*          pTxPar	： 发送数据的奇偶校验位
*          pRx		：存放解包后的卡片回应的数据的缓冲区
*          pRxPar	：存放卡片回应的奇偶校验数据的缓冲区
* @retval ：成功的时候返回卡片回应的数据的比特长度，
			  失败的时候返回对应的错误码。
*/
uint8_t pcd_14a_reader_bits_transfer(uint8_t* pTx, uint16_t  szTxBits, uint8_t* pTxPar, uint8_t* pRx, uint8_t* pRxPar, uint16_t* pRxLenBit, uint16_t szRxLenBitMax) 
{

	static uint8_t buffer[DEF_FIFO_LENGTH];
	uint8_t status		= 0, 
			modulus 	= 0, 
			i 			= 0, 
			dataLen 	= 0;

	buffer[0] = pTx[0];
	if (szTxBits > 8) {
		// 判断到需要合并奇偶校验到数据流中
		if (pTxPar != NULL) {
			// 几个字节就需要几个bit，因此会
			// 多出对应字节个数的bit的数据
			modulus = dataLen = szTxBits / 8;
			buffer[1] = (pTxPar[0] | (pTx[1] << 1));
			for( i = 2; i < dataLen; i++ ) {
				// add the remaining prev byte and parity
				buffer[i] = ((pTxPar[i - 1] << (i - 1)) | (pTx[ i - 1] >> (9 - i)));
				// add next byte and push i bits
				buffer[i] |= (pTx[i] << i);
			}
			// add remainder of last byte + end parity
			buffer[dataLen] = ((pTxPar[dataLen - 1] << (i - 1)) | (pTx[dataLen-1] >> (9 - i)));
			dataLen += 1;
		} else {
			modulus = szTxBits % 8;
			dataLen = modulus > 0 ? (szTxBits / 8 + 1) : (szTxBits / 8);
			// 不需要合并奇偶校验位，就当做是外部已经做好了此处理
			for( i = 1; i < dataLen; i++ ) {
				buffer[i] = pTx[i];
			}
		}
	} else {
		dataLen = 1;
		modulus = szTxBits;
	}

	set_register_mask(BitFramingReg, modulus);  // 设置最后一个字节传输N位
	set_register_mask(MfRxReg, 0x10);  // 需要关闭奇偶校验位的使能

	status = pcd_14a_reader_bytes_transfer(
		PCD_TRANSCEIVE,
		buffer,
		dataLen,  				// 数据的字节计数
		buffer,  				// 接收缓冲区
		pRxLenBit, 				// 接收到的数据的长度，注意，是比特流的长度
		U8ARR_BIT_LEN(buffer)	// 能收的数据的上限长度
	);

	clear_register_mask(BitFramingReg, modulus);
	clear_register_mask(MfRxReg, 0x10);  // 使能奇偶校验位
	
	// 单纯判断数据传输的长度
	if (status != HF_TAG_OK) {
		// NRF_LOG_INFO("pcd_14a_reader_bytes_transfer error status: %d\n", status);
		return status;
	}
	
	pRx[0] = buffer[0];
	modulus = 0;
	if (*pRxLenBit > 8) {
		// 取余，等下要用来统计字节数
		modulus  = *pRxLenBit % 8;
		// 取字节数，等下要用来解包
		dataLen  = *pRxLenBit / 8 + (modulus > 0);
		// 取比特数，这个是最终的数据的长度
		*pRxLenBit = *pRxLenBit - modulus;
		
		// 进一步判断数据解码之后是否会溢出
		if (*pRxLenBit > szRxLenBitMax) {
			NRF_LOG_INFO("pcd_14a_reader_bits_transfer decode parity data overflow: %d, max = %d\n", *pRxLenBit, szRxLenBitMax);
			// 此处也要在出现溢出有，重置有效接收的数据的长度，避免外部调用者以此误判
			*pRxLenBit = 0;
			return HF_ERRSTAT;
		}
		
		// 最终的奇偶校验与数据的分离解包过程
		for(i = 1; i < dataLen - 1; i++) {
			if (pRxPar != NULL) {
				pRxPar[i - 1] = (buffer[i] & (1 << (i - 1))) >> (i - 1);
			}
			pRx[i] = (buffer[i] >> i) | (buffer[i + 1] << (8 - i));
		}
		if (pRxPar != NULL) {
			pRxPar[i - 1] = (buffer[i] & (1 << (i - 1)) ) >> (i - 1);
		}
	}
	return HF_TAG_OK;
}
	
/**
* @brief  : ISO14443-A 寻找一张卡片，只执行一次！
* @param  ：tag: 存放卡片信息的buffer
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_scan_once(picc_14a_tag_t *tag) {
	// 初始化关键的参数
	if (tag) {
		tag->uid_len = 0;
		memset(tag->uid, 0, 10);
    } else {
		return STATUS_PAR_ERR;  // 寻卡不允许不传入标签信息结构体
	}
	
	// 唤醒
    if (pcd_14a_reader_atqa_request(tag->atqa, NULL, U8ARR_BIT_LEN(tag->atqa)) != HF_TAG_OK) {
		// NRF_LOG_INFO("pcd_14a_reader_atqa_request HF_TAG_NO\r\n");
		return HF_TAG_NO;
	}
	
    uint8_t resp[DEF_FIFO_LENGTH] = {0}; // theoretically. A usual RATS will be much smaller
    // uint8_t resp_par[MAX_PARITY_SIZE] = {0};
	
	uint16_t len;
	uint8_t status;
    uint8_t do_cascade = 1;
    uint8_t cascade_level = 0;
	
	// OK we will select at least at cascade 1, lets see if first byte of UID was 0x88 in
    // which case we need to make a cascade 2 request and select - this is a long UID
    // While the UID is not complete, the 3nd bit (from the right) is set in the SAK.
    for (; do_cascade; cascade_level++) {
        // SELECT_* (L1: 0x93, L2: 0x95, L3: 0x97)
        uint8_t sel_all[]    = { PICC_ANTICOLL1, 0x20 };
        uint8_t sel_uid[]    = { PICC_ANTICOLL1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t uid_resp[5] = {0}; // UID + original BCC
        sel_uid[0] = sel_all[0] = PICC_ANTICOLL1 + cascade_level * 2;
		
		// 发送防冲撞指令
		status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, sel_all, sizeof(sel_all), resp, &len, U8ARR_BIT_LEN(resp));
		
		// 出现标签冲撞，我们需要解决冲撞
		if (status != HF_TAG_OK) {
			// 该冲撞还是得冲撞，解密过程中还是不要有这种情况发生
			// 所以暂时不去解决冲撞，而是直接告知用户，让用户保证场内只有一张卡
			NRF_LOG_INFO("Err at tag collision.\n");
			return status;
		} else {  // no collision, use the response to SELECT_ALL as current uid
			memcpy(uid_resp, resp, 5); // UID + original BCC
		}
		
		uint8_t uid_resp_len = 4;
		
		// 永远使用最后的UID段当做u32类型的UID，
		// 不管是几字节的UID段
		// *u32Uid = bytes_to_num(uid_resp, 4);
		
		// Construct SELECT UID command
        sel_uid[1] = 0x70;  // transmitting a full UID (1 Byte cmd, 1 Byte NVB, 4 Byte UID, 1 Byte BCC, 2 Bytes CRC)
		
		memcpy(sel_uid + 2, uid_resp, 5);	// the UID received during anticollision with original BCC
		uint8_t bcc = sel_uid[2] ^ sel_uid[3] ^ sel_uid[4] ^ sel_uid[5]; // calculate BCC
		if (sel_uid[6] != bcc) {
			NRF_LOG_INFO("BCC%d incorrect, got 0x%02x, expected 0x%02x\n", cascade_level, sel_uid[6], bcc);
			return HF_ERRBCC;
		}
		
		crc_14a_append(sel_uid, 7);	// calculate and add CRC
		
		// 发送 9x 70 去选卡
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, sel_uid, sizeof(sel_uid), resp, &len, U8ARR_BIT_LEN(resp));
		if (status != HF_TAG_OK) {
			NRF_LOG_INFO("Err at sak receive.\n");
			return HF_ERRSTAT;
		}
		
		// 缓冲接收到的SAK
		tag->sak = resp[0];
		
		// 如果UID是以 0X88 的形式开头的，说明UID还不完整
		// 下次循环我们需要进行递增级联，返回结束防冲撞，完成级联
        do_cascade = (((tag->sak & 0x04) /* && uid_resp[0] == 0x88 */) > 0);
        if (do_cascade) {
            // Remove first byte, 0x88 is not an UID byte, it CT, see page 3 of:
            // http://www.nxp.com/documents/application_note/AN10927.pdf
            uid_resp[0] = uid_resp[1];
            uid_resp[1] = uid_resp[2];
            uid_resp[2] = uid_resp[3];
            uid_resp_len = 3;
        }
		
		// 拷贝卡片的UID信息到传入的结构体中
		memcpy(tag->uid + (cascade_level * 3), uid_resp, uid_resp_len);
		tag->uid_len += uid_resp_len;
		// 级联只有 1 2 3 三种，对应 4 7 10 字节的卡号
		// 因此需要在下标为0的基础上 + 1
		tag->cascade = cascade_level + 1;
	}
	return HF_TAG_OK;
}

/**
* @brief  : ISO14443-A 寻找一张卡片
* @param  ：tag: 存放卡片信息的buffer
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag) {
	uint8_t status;
	
	// 第一次寻卡
	status = pcd_14a_reader_scan_once(tag);
	if (status == HF_TAG_OK) {
		return HF_TAG_OK;
	}
	
	// 第二次寻卡
	status = pcd_14a_reader_scan_once(tag);
	if (status == HF_TAG_OK) {
		return HF_TAG_OK;
	}
	
	// 超过上限次数
	return status;
}

/**
* @brief  : 获取选择应答
* @param  ：pAts：ATS的保存区域
* @param  ：szAts：卡片响应的ATS的长度
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_ats_request(uint8_t *pAts, uint16_t *szAts, uint16_t szAtsBitMax) {
	uint8_t rats[] = { PICC_RATS, 0x80, 0x31, 0x73 }; // FSD=256, FSDI=8, CID=0
	uint8_t status;
	
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, rats, sizeof(rats), pAts, szAts, szAtsBitMax);
	
	if (status != HF_TAG_OK) {
		NRF_LOG_INFO("Err at ats receive.\n");
		return status;
	}
	
	// NRF_LOG_INFO("Length: %d\n", *szAts);
	
	if (*szAts > 0) { *szAts = *szAts / 8; }
    return HF_TAG_OK;
}

/**
* @brief  : 获取答复请求，类型A
* @param  ：pSnr：卡片序列号，N字节
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_atqa_request(uint8_t *resp, uint8_t *resp_par, uint16_t resp_max_bit) {
	uint16_t len = 0;
    uint8_t retry = 0;
	uint8_t status = HF_TAG_OK;
	uint8_t wupa[] = { PICC_REQALL };  // 0x26 - REQA  0x52 - WAKE-UP

    // we may need several tries if we did send an unknown command or a wrong authentication before...
    do {
        // Broadcast for a card, WUPA (0x52) will force response from all cards in the field and Receive the ATQA
        status = pcd_14a_reader_bits_transfer(wupa, 7, NULL, resp, resp_par, &len, resp_max_bit);
		// NRF_LOG_INFO("pcd_14a_reader_atqa_request len: %d\n", len);
    } while (len != 16 && (retry++ < 10));
	
	// 正常的 ATQA 是2字节，也就是16bit的，
	// 我们需要进行判断，收到的数据是否正确
	if (status == HF_TAG_OK && len == 16) {
		// 可以确认当前场内存在至少一张14A的卡了
		return HF_TAG_OK;
	}
	
	// 不存在卡片
    return HF_TAG_NO;
}

/**
* @brief   : 解锁GEN1A后门卡以进行非标准M1操作步骤
*				注意，解锁之后不要halt卡片，后门指令的生命周期只存在于当次解锁后的
*				数据块读写操作范围内，如果发生场掉电或卡片被halt或者重新防冲撞，
*				将会失去后门权限，需要调用此函数重新启动。
*
* @retval ：状态值 HF_TAG_OK，解锁成功，其他状态值表示解锁失败
*/
uint8_t pcd_14a_reader_gen1a_unlock(void)
{
	// 初始化变量
	uint8_t unlock, status;
	uint16_t rx_length = 0;
	uint8_t recvbuf[1] = { 0x00 };
	
	// 重启通信（非常重要）
	pcd_14a_reader_halt_tag();
	
	// 第一步解锁，发送7bit的 0x40
	unlock = PICC_MAGICWUPC1;
	status = pcd_14a_reader_bits_transfer(&unlock, 7, NULL, recvbuf, NULL, &rx_length, U8ARR_BIT_LEN(recvbuf));
	if (!(status == HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
		NRF_LOG_INFO("UNLOCK(MAGICWUPC1) FAILED! Length: %d, Status: %02x\n", rx_length, status);
		return HF_ERRSTAT;
	}
	
	// 第二步解锁，发送整字节的 0x43
	unlock = PICC_MAGICWUPC2;
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, &unlock, 1, recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
	if (!(status == HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
		NRF_LOG_INFO("UNLOCK(MAGICWUPC2) FAILED! Length: %d, Status: %02x\n", rx_length, status);
		return HF_ERRSTAT;
	}
	
	// 两次解锁都没问题，我们默认这次解锁操作成功！
	return HF_TAG_OK;
}

/**
* @brief  : 对UFUID卡进行上锁操作，封锁后门指令，使其成为普通卡，不响应后门指令
*				这个操作成功的前提是：
*				1、已经调用 pcd_14a_reader_gen1a_unlock() 函数解锁卡片成功
*				2、卡片具有 UFUID 卡的封后门指令功能
*
* @retval ：状态值 HF_TAG_OK，封卡或者存在封卡后门，
			其他状态值表示封卡失败或者没有封卡后门
*/
uint8_t pcd_14a_reader_gen1a_uplock(void)
{
	uint8_t status;
	uint16_t rx_length = 0;
	
	// 我们已知的双层封卡指令
	uint8_t uplock_1[] = { 0xE1,  0x00,  0xE1,  0xEE };
	uint8_t uplock_2[] = { 0x85,  0x00,  0x00,  0x00,
						   0x00,  0x00,  0x00,  0x00,
						   0x00,  0x00,  0x00,  0x00,
						   0x00,  0x00,  0x00,  0x08,
						   0x18,  0x47 
						 };
	
	uint8_t recvbuf[1] = { 0x00 };
	
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, uplock_1, sizeof(uplock_1), recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
	if (!(status == HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
		NRF_LOG_INFO("UPLOCK1(UFUID) FAILED!\n");
		return HF_ERRSTAT;
	}
	
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, uplock_2, sizeof(uplock_2), recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
	if (!(status == HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
		NRF_LOG_INFO("UPLOCK2(UFUID) FAILED!\n");
		return HF_ERRSTAT;
	}

	// 封卡成功
	return HF_TAG_OK;
}

/**
* @brief   :校验M1卡片密码
* @param  ：type：密码验证模式
*                     = 0x60，验证A密钥
*                     = 0x61，验证B密钥
*           ucAddr：块地址
*           pKey：密码
*           pSnr：卡片序列号，4字节
* @retval ：状态值HF_TAG_OK成功，TAG_ERRAUTH失败，其他的返回值表示一些通信错误相关的异常！
*/
uint8_t pcd_14a_reader_mf1_auth(picc_14a_tag_t *tag, uint8_t type, uint8_t addr, uint8_t* pKey)
{
	uint8_t dat_buff[12] = { type, addr };
	uint16_t data_len = 0;
	
	memcpy(&dat_buff[2], pKey, 6);
	get_4byte_tag_uid(tag, &dat_buff[8]);

	pcd_14a_reader_bytes_transfer(PCD_AUTHENT, dat_buff, 12, dat_buff, &data_len, U8ARR_BIT_LEN(dat_buff));
	
	// 为了提高兼容性，我们此处直接判断执行完成 PCD_AUTHENT
	// 指令之后，Status2Reg中的通信加密位是否被置位就行了。
	if (read_register_single(Status2Reg) & 0x08) {
		return HF_TAG_OK;
	}
	
	// 其他的情况都认为失败！
	return MF_ERRAUTH;
}

/**
* @brief   :取消已经校验的密钥的状态
*/
void pcd_14a_reader_mf1_unauth(void) {
	clear_register_mask(Status2Reg, 0x08);
}

/**
* @brief   :读取M1卡的指定块地址的数据
* @param  ：cmd : 读块指令
*			addr：块地址
*           p	：读出的数据，16字节
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_mf1_read_by_cmd(uint8_t cmd, uint8_t addr, uint8_t* p) {
	uint8_t status;
	uint16_t len;
	uint8_t dat_buff[MAX_MIFARE_FRAME_SIZE] = { cmd, addr };
	uint8_t crc_buff[DEF_CRC_LENGTH]		= { 0x00 };

	// 短数据直接让 MCU 计算
	crc_14a_append(dat_buff, 2);
	// 然后发起通信
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 4, dat_buff, &len, U8ARR_BIT_LEN(dat_buff));
	if (status == HF_TAG_OK)
	{
		if (len == 0x90 /* 0x90 = 144bits */) {
			// 16字节长度的CRC数据，为了不浪费CPU性能，
			// 我们可以让 522 去计算
			crc_14a_calculate(dat_buff, 16, crc_buff);
			// 校验一下CRC，避免数据出错
			if ((crc_buff[0] != dat_buff[16]) || (crc_buff[1] != dat_buff[17])) { 
				status = HF_ERRCRC; 
			}
			// 虽然 CRC 校验有毛病，但是我们还是可以回传
			// 读取到的卡片数据，因为开发者有可能有特殊用法
			memcpy(p, dat_buff, 16);
		} else {
			// 传回来的数据有毛病，可能是环境因素或者卡片不遵守规范！
			// 又或者是控制位影响到了读取！
			status = HF_ERRSTAT;
		}
	}
	return status;
}

/**
* @brief   :读取M1卡的指定块地址的数据
* @param  ：addr：块地址
*           p	：读出的数据，16字节
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_mf1_read(uint8_t addr, uint8_t* p)
{
	// 标准的M1规范内的读卡
	return pcd_14a_reader_mf1_read_by_cmd(PICC_READ, addr, p);
}

/**
* @brief   :在M1卡的指定块地址写入指定数据
* @param  ：cmd : 写块指令
*			addr：块地址
*           p	：写入的数据，16字节
*			
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_mf1_write_by_cmd(uint8_t cmd, uint8_t addr, uint8_t* p) {
	uint8_t status;
	uint16_t dat_len;
	
	// 准备写卡数据，用于发起写卡
	uint8_t dat_buff[18] = { cmd, addr };
	crc_14a_append(dat_buff, 2);
	
	// NRF_LOG_INFO("0 pcd_14a_reader_mf1_write addr = %d\r\n", addr);

	// 请求写卡，此时，卡片应当回复ACK
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 4, dat_buff, &dat_len, U8ARR_BIT_LEN(dat_buff));
	// 通信失败，直接返回原因
	if (status != HF_TAG_OK) {
		return status;
	}
	// 通信成功，但是操作被卡片拒绝！
	if ((dat_len != 4) || ((dat_buff[0] & 0x0F) != 0x0A)) {
		// NRF_LOG_INFO("1 status = %d, datalen = %d, data = %02x\n", status, dat_len, dat_buff[0]);
		status = HF_ERRSTAT;
	}
	// 通信成功，卡片接受了写卡操作
	if (status == HF_TAG_OK) {
		// 1、拷贝数据并且计算 CRC
		memcpy(dat_buff, p, 16);
		crc_14a_calculate(dat_buff, 16, &dat_buff[16]);
		
		// NRF_LOG_INFO_hex("Will send: ", (uint8_t *)p, 16);
		// NRF_LOG_INFO("\n");
		
		// 2、传输最终的写卡数据完成写卡
		status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 18, dat_buff, &dat_len, U8ARR_BIT_LEN(dat_buff));
		// 通信失败，直接返回原因
		if (status != HF_TAG_OK) {
			return status;
		}
		// 通信成功，我们需要判断卡片接收到数据后是否成功处理
		// 并且回复ACK
		if ((dat_len != 4) || ((dat_buff[0] & 0x0F) != 0x0A)) {
			// NRF_LOG_INFO("2 status = %d, datalen = %d, data = %02x\n", status, dat_len, dat_buff[0]);
			status = HF_ERRSTAT;
		}
	}
	return status;
}

/**
* @brief   :在M1卡的指定块地址写入指定数据
* @param  ：addr：块地址
*           p：写入的数据，16字节
* @retval ：状态值HF_TAG_OK，成功
*/
uint8_t pcd_14a_reader_mf1_write(uint8_t addr, uint8_t* p)
{
	// 标准的M1规范内的写卡
	return pcd_14a_reader_mf1_write_by_cmd(PICC_WRITE, addr, p);
}

/**
* @brief   :让卡片进入休眠模式
* @param  ：无
* @retval ：状态值 TAG_NOTAG，成功
*/
uint8_t pcd_14a_reader_halt_tag(void)
{
	uint8_t status;
	uint16_t unLen;
	// 直接准备好成型的数据了，还计算个鬼的CRC
	uint8_t data[] = { PICC_HALT, 0x00, 0x57, 0xCD };
	status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, data, 4, data, &unLen, U8ARR_BIT_LEN(data));
	return status == HF_TAG_NO && unLen == 0;
}

/**
* @brief   :快速让卡片进入休眠模式
* @param  ：无
* @retval ：无
*/
void pcd_14a_reader_fast_halt_tag(void)
{
	uint8_t data[] = { PICC_HALT, 0x00, 0x57, 0xCD };
	pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, data, 4, NULL, NULL, U8ARR_BIT_LEN(data));
}

/**
* @brief  ：用RC522计算CRC16（循环冗余校验）
* @param  ：pIn：计算CRC16的数组
*           len：计算CRC16的数组字节长度
*           pOut：存放计算结果存放的首地址
* @retval ：状态值HF_TAG_OK，成功
*/
void pcd_14a_reader_calc_crc(uint8_t* pbtData, size_t szLen, uint8_t* pbtCrc)
{
	uint8_t i, n;
	
	// 重置状态机
	clear_register_mask(Status1Reg, 0x20);
	write_register_single(CommandReg, PCD_IDLE);
	set_register_mask(FIFOLevelReg, 0x80);
	
	// 向FIFO写入要计算CRC的数据
	write_register_buffer(FIFODataReg, pbtData, szLen);
	write_register_single(CommandReg, PCD_CALCCRC);
	
	// 等待计算完成
	i = szLen * 2;
	do
	{
		n = read_register_single(Status1Reg);
		i--;
	} while ((i != 0) && !(n & 0x20));
	
	// 获得最终计算出来的CRC数据
	pbtCrc[0] = read_register_single(CRCResultRegL);
	pbtCrc[1] = read_register_single(CRCResultRegM);
}

/**
* @brief  ：开启天线
*/
inline void pcd_14a_reader_antenna_on(void)
{
	set_register_mask(TxControlReg, 0x03);
}

/**
* @brief  ：关闭天线
*/
inline void pcd_14a_reader_antenna_off(void)
{
	clear_register_mask(TxControlReg, 0x03);
}

/**
* @brief  ：奇偶校验位启用
*/
inline void pcd_14a_reader_parity_on(void) {
	clear_register_mask(MfRxReg, 0x10);
}

/**
* @brief  ：奇偶校验位关闭
*/
inline void pcd_14a_reader_parity_off(void) {
	set_register_mask(MfRxReg, 0x10);
}

/**
* @brief 	: 获得级联命令，输入只允许存在三种情况
*				1 表示第一次级联，命令是 PICC_ANTICOLL1
*				2 表示第二次级联，命令是 PICC_ANTICOLL2
*				3 表示第三次级联，命令是 PICC_ANTICOLL3
* @param	:len  : 存放数值的缓冲区的字节长度
* @param 	:src  : 存放数值的字节缓冲区
* @retval 	: 转换结果
*
*/
uint8_t cascade_to_cmd(uint8_t cascade)
{
	uint8_t ret = PICC_ANTICOLL1;
	switch(cascade) {
		case 1:
			ret = PICC_ANTICOLL1;
			break;
		
		case 2:
			ret = PICC_ANTICOLL2;
			break;
		
		case 3:
			ret = PICC_ANTICOLL3;
			break;
	}
	return ret;
}

/**
* @brief 	: 获得标签的4字节类型的UID，根据传入的基础卡片信息决定
*				1 表示一次级联，UID有  4 字节，直接使用全部的
*				2 表示二次级联，UID有  7 字节，使用后四位
*				3 表示三次级联，UID有 10 字节，使用后四位
* @param	:tag	: 存放卡片信息的结构体
* @param 	:pUid		: 存放结果的字节缓冲区
* @retval 	: 返回实际上我们搜索到的有效UID字节的起始地址，
*             注意：此地址可能是指向栈中的，不保证一定是全局有效的地址，
*                   此地址根据 tag 处于的内存位置决定生命周期。
*
*/
uint8_t* get_4byte_tag_uid(picc_14a_tag_t *tag, uint8_t *pUid)
{
	uint8_t *p_TmpUid = NULL;
	switch(tag->cascade) {
		case 1:
			p_TmpUid = tag->uid;
			break;
		
		case 2:
			p_TmpUid = tag->uid + 3;
			break;
		
		case 3:
			p_TmpUid = tag->uid + 6;
			break;
	}
	if (pUid != NULL) {
		memcpy(pUid, p_TmpUid, 4);
	}
    return p_TmpUid;
}

/**
* @brief 	: 获得标签的U32类型的UID，根据传入的基础卡片信息决定
*				1 表示一次级联，UID有  4 字节
*				2 表示二次级联，UID有  7 字节
*				3 表示三次级联，UID有 10 字节
* @param	:tag  : 存放卡片信息的结构体
* @retval 	: 转换结果
*
*/
uint32_t get_u32_tag_uid(picc_14a_tag_t *tag)
{
	uint8_t uid_buf[4] = { 0x00 };
	// 直接调用封装好的函数拷贝目标值
	get_4byte_tag_uid(tag, uid_buf);
	return bytes_to_num(uid_buf, 4);
}

/**
* @brief 在选中的平台上上计算CRC
 *
 */
inline void crc_14a_calculate(uint8_t* pbtData, size_t szLen, uint8_t* pbtCrc) {
	switch(m_crc_computer) {
		case 0: {
			calc_14a_crc_lut(pbtData, szLen, pbtCrc);
		} break;
		case 1: {
			pcd_14a_reader_calc_crc(pbtData, szLen, pbtCrc);
		} break;
		default: {
			// 
		} break;
	}
}


/**
 * @brief 向数据结尾追加计算后的CRC
 *
 */
inline void crc_14a_append(uint8_t* pbtData, size_t szLen) {
	switch(m_crc_computer) {
		case 0: {
			calc_14a_crc_lut(pbtData, szLen, pbtData + szLen);
		} break;
		case 1: {
			pcd_14a_reader_calc_crc(pbtData, szLen, pbtData + szLen);
		} break;
		default: {

		} break;
	}
}

/**
* @brief 切换CRC的计算源，默认在 MCU 上计算，
*			可以切换到RC522上计算，如果MCU的性能不是很足
*			如果 MCU 的性能很足，建议放在 MCU 上计算，让计算过程更加的顺畅
*			如果 MCU 的性能不足，建议放在 522 上计算，缓解MCU的计算压力
*
 */
inline void pcd_14a_reader_crc_computer(uint8_t use522CalcCRC) {
	m_crc_computer = use522CalcCRC;
}
