#ifndef RC522_H
#define RC522_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/*
* RC522命令字
*/
#define PCD_IDLE              0x00               //取消当前命令
#define PCD_AUTHENT           0x0E               //验证密钥
#define PCD_RECEIVE           0x08               //接收数据
#define PCD_TRANSMIT          0x04               //发送数据
#define PCD_TRANSCEIVE        0x0C               //发送并接收数据
#define PCD_RESET		      0x0F               //复位
#define PCD_CALCCRC           0x03               //CRC计算

/*
* ISO14443-A命令字
*/
#define PICC_REQIDL           0x26               //寻天线区内未进入休眠状态
#define PICC_REQALL           0x52               //寻天线区内全部卡
#define PICC_ANTICOLL1        0x93               //防冲撞
#define PICC_ANTICOLL2        0x95               //防冲撞
#define PICC_ANTICOLL3        0x97               //防冲撞
#define PICC_RATS          	  0xE0				 //选择应答

/*
* M1卡片命令字
*/
#define PICC_AUTHENT1A        0x60               //验证A密钥
#define PICC_AUTHENT1B        0x61               //验证B密钥
#define PICC_READ             0x30               //读块
#define PICC_WRITE            0xA0               //写块
#define PICC_DECREMENT        0xC0               //扣款
#define PICC_INCREMENT        0xC1               //充值
#define PICC_RESTORE          0xC2               //调块数据到缓冲区
#define PICC_TRANSFER         0xB0               //保存缓冲区中数据
#define PICC_HALT             0x50               //休眠

// GEN1A标签的命令字
#define PICC_MAGICWUPC1 	  0x40				 // 后门指令1
#define PICC_MAGICWUPC2		  0x43				 // 后门指令2
#define PICC_MAGICWIPEC		  0x41				 // 后门清卡指令

/* RC522 FIFO长度定义 */
#define DEF_FIFO_LENGTH       	64               //FIFO size=64byte

// RC522 CRC长度定义
#define DEF_CRC_LENGTH       	2

/*
	RC522 默认定时器超时配置，这个值可以动态调整，通过 PcdSetTimeout 函数
	操作标准M1卡最大等待时间 25ms
	我们可以提高超时以兼容一些反应比较迟钝的卡
	比如某些手环模拟的卡，比如某些其他硬件模拟的卡，例如变色龙
	如果超时值太小，就有可能没办法读到UID(Gen1A)卡！
*/
#define DEF_COM_TIMEOUT        	25

// 数据IO长度定义
#define MAX_MIFARE_FRAME_SIZE   18  							// biggest Mifare frame is answer to a read (one block = 16 Bytes) + 2 Bytes CRC
#define MAX_MIFARE_PARITY_SIZE  3   							// need 18 parity bits for the 18 Byte above. 3 Bytes are enough to store these
#define CARD_MEMORY_SIZE        4096

/////////////////////////////////////////////////////////////////////
// MF522 寄存器定义
/////////////////////////////////////////////////////////////////////
// PAGE 0
#define     RFU00                 0x00    //保留
#define     CommandReg            0x01    //启动和停止命令的执行
#define     ComIEnReg             0x02    //中断请求传递的使能（Enable/Disable）
#define     DivlEnReg             0x03    //中断请求传递的使能
#define     ComIrqReg             0x04    //包含中断请求标志
#define     DivIrqReg             0x05    //包含中断请求标志
#define     ErrorReg              0x06    //错误标志，指示执行的上个命令的错误状态
#define     Status1Reg            0x07    //包含通信的状态标识
#define     Status2Reg            0x08    //包含接收器和发送器的状态标志
#define     FIFODataReg           0x09    //64字节FIFO缓冲区的输入和输出
#define     FIFOLevelReg          0x0A    //指示FIFO中存储的字节数
#define     WaterLevelReg         0x0B    //定义FIFO下溢和上溢报警的FIFO深度
#define     Control522Reg         0x0C    //不同的控制寄存器
#define     BitFramingReg         0x0D    //面向位的帧的调节
#define     CollReg               0x0E    //RF接口上检测到的第一个位冲突的位的位置
#define     RFU0F                 0x0F    //保留
// PAGE 1     
#define     RFU10                 0x10    //保留
#define     ModeReg               0x11    //定义发送和接收的常用模式
#define     TxModeReg             0x12    //定义发送过程的数据传输速率
#define     RxModeReg             0x13    //定义接收过程中的数据传输速率
#define     TxControlReg          0x14    //控制天线驱动器管教TX1和TX2的逻辑特性
#define     TxAutoReg             0x15    //控制天线驱动器的设置
#define     TxSelReg              0x16    //选择天线驱动器的内部源
#define     RxSelReg              0x17    //选择内部的接收器设置
#define     RxThresholdReg        0x18    //选择位译码器的阈值
#define     DemodReg              0x19    //定义解调器的设置
#define     RFU1A                 0x1A    //保留
#define     RFU1B                 0x1B    //保留
#define     MfTxReg               0x1C    //控制ISO 14443/ MIFARE模式中106kbit/s的通信 (比如奇偶校验位的计算)
#define     MfRxReg               0x1D    //控制ISO 14443/ MIFARE模式中106kbit/s的通信 (比如奇偶校验位的计算)
#define     RFU1E                 0x1E    //保留
#define     SerialSpeedReg        0x1F    //选择串行UART接口的速率
// PAGE 2    
#define     RFU20                 0x20    //保留
#define     CRCResultRegM         0x21    //显示CRC计算的实际MSB值
#define     CRCResultRegL         0x22    //显示CRC计算的实际LSB值
#define     RFU23                 0x23    //保留
#define     ModWidthReg           0x24    //控制ModWidth的设置
#define     RFU25                 0x25    //保留
#define     RFCfgReg              0x26    //配置接收器增益
#define     GsNReg                0x27    //选择天线驱动器管脚（TX1和TX2）的调制电导
#define     CWGsCfgReg            0x28    //选择天线驱动器管脚的调制电导
#define     ModGsCfgReg           0x29    //选择天线驱动器管脚的调制电导
#define     TModeReg              0x2A    //定义内部定时器的设置
#define     TPrescalerReg         0x2B    //定义内部定时器的设置
#define     TReloadRegH           0x2C    //描述16位长的定时器重装值
#define     TReloadRegL           0x2D    //描述16位长的定时器重装值
#define     TCounterValueRegH     0x2E   
#define     TCounterValueRegL     0x2F    //显示16位长的实际定时器值
// PAGE 3      
#define     RFU30                 0x30    //保留
#define     TestSel1Reg           0x31    //常用测试信号配置
#define     TestSel2Reg           0x32    //常用测试信号配置和PRBS控制 
#define     TestPinEnReg          0x33    //D1-D7输出驱动器的使能管脚（仅用于串行接口）
#define     TestPinValueReg       0x34    //定义D1-D7用作I/O总线时的值
#define     TestBusReg            0x35    //显示内部测试总线的状态
#define     AutoTestReg           0x36    //控制数字自测试
#define     VersionReg            0x37    //显示版本
#define     AnalogTestReg         0x38    //控制管脚AUX1和AUX2
#define     TestDAC1Reg           0x39    //定义TestDAC1的测试值
#define     TestDAC2Reg           0x3A    //定义TestDAC2的测试值
#define     TestADCReg            0x3B    //显示ADCI和Q通道的实际值
#define     RFU3C                 0x3C    //保留
#define     RFU3D                 0x3D    //保留
#define     RFU3E                 0x3E    //保留
#define     RFU3F		  		  0x3F    //保留


/////////////////////////////////////////////////////////////////////
// 函数和类型定义
/////////////////////////////////////////////////////////////////////


// 高效率转换4字节数据为U32类型的数值
#define BYTES4_TO_U32(src) (__rev(*((uint32_t*)src)))

// 获得静态字节数组的比特长度
#define U8ARR_BIT_LEN(src) ((sizeof(src)) * (8))

// 标签信息的基本结构封装
typedef struct {
	uint8_t uid[10];  // 卡号的字节数组，最长10字节
	uint8_t uid_len;  // 卡号的长度
	uint8_t cascade;  // 防冲撞等级 值为1表示 4byte，2表示7byte，3表示10byte
	uint8_t sak;	  // 选择确认
	uint8_t atqa[2];  // 请求应答
} picc_14a_tag_t;

#ifdef __cplusplus
extern "C" {
#endif
	// Device control
	void pcd_14a_reader_init(void);
    void pcd_14a_reader_uninit(void);
    void pcd_14a_reader_reset(void);
    void pcd_14a_reader_antenna_on(void);
    void pcd_14a_reader_antenna_off(void);

	// Device register
	uint8_t read_register_single(uint8_t Address);
    void write_register_single(uint8_t Address, uint8_t value);
    void clear_register_mask(uint8_t reg, uint8_t mask);
    void set_register_mask(uint8_t reg, uint8_t mask);
	
	// Device comunication control
	uint16_t pcd_14a_reader_timeout_get(void);
	void pcd_14a_reader_timeout_set(uint16_t timeout_ms);
	
	// Device comunication interface
    uint8_t pcd_14a_reader_bytes_transfer(uint8_t Command,
        uint8_t* pIn,
        uint8_t  InLenByte,
        uint8_t* pOut,
        uint16_t* pOutLenBit,
		uint16_t maxOutLenBit);
    uint8_t pcd_14a_reader_bits_transfer(uint8_t* pTx,
        uint16_t  szTxBits,
        uint8_t* pTxPar,
        uint8_t* pRx,
        uint8_t* pRxPar,
		uint16_t* pRxLenBit,
		uint16_t szRxLenBitMax);
	
	// Device auto append and check 14443-A parity enable or disable.
	void pcd_14a_reader_parity_on(void);
    void pcd_14a_reader_parity_off(void);
    
	// 14443-A tag operation
	uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag);
	uint8_t pcd_14a_reader_ats_request(uint8_t *pAts, uint16_t *szAts, uint16_t szAtsBitMax);
	uint8_t pcd_14a_reader_atqa_request(uint8_t *resp, uint8_t *resp_par, uint16_t resp_max_bit);
	
	// M1 tag operation
    uint8_t pcd_14a_reader_mf1_auth(picc_14a_tag_t *tag, uint8_t type, uint8_t addr, uint8_t* pKey);
	void pcd_14a_reader_mf1_unauth(void);
	// 写卡操作
	uint8_t pcd_14a_reader_mf1_write_by_cmd(uint8_t cmd, uint8_t addr, uint8_t* p);
    uint8_t pcd_14a_reader_mf1_write(uint8_t addr, uint8_t* pData);
	// 读卡操作
	uint8_t pcd_14a_reader_mf1_read_by_cmd(uint8_t cmd, uint8_t addr, uint8_t* p);
    uint8_t pcd_14a_reader_mf1_read(uint8_t addr, uint8_t* pData);
    // 休眠卡操作
	uint8_t pcd_14a_reader_halt_tag(void);
	void pcd_14a_reader_fast_halt_tag(void);
		
	// UID & UFUID tag operation
	uint8_t pcd_14a_reader_gen1a_unlock(void);
	uint8_t pcd_14a_reader_gen1a_uplock(void);
	
	// CRC calulate
	void pcd_14a_reader_calc_crc(uint8_t* pbtData, size_t szLen, uint8_t* pbtCrc);
	void crc_14a_calculate(uint8_t* pbtData, size_t szLen, uint8_t* pbtCrc);
    void crc_14a_append(uint8_t* pbtData, size_t szLen);
	void pcd_14a_reader_crc_computer(uint8_t use522CalcCRC);
	
	// other
	uint8_t cascade_to_cmd(uint8_t cascade);
	uint32_t get_u32_tag_uid(picc_14a_tag_t *tag);
	uint8_t* get_4byte_tag_uid(picc_14a_tag_t *tag, uint8_t *out);
#ifdef __cplusplus
}
#endif

#endif // !RC522_H
