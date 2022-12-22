#ifndef STATUS_H
#define STATUS_H


/////////////////////////////////////////////////////////////////////
// 14a status
/////////////////////////////////////////////////////////////////////
#define 	HF_TAG_OK                 				(0x00)  // IC卡操作成功
#define 	HF_TAG_NO              					(0x01)  // 没有发现IC卡
#define 	HF_ERRSTAT                				(0x02)  // IC卡通信异常
#define 	HF_ERRCRC             					(0x03)  // IC卡通信校验异常
#define 	HF_COLLISION          					(0x04)  // IC卡冲突
#define 	HF_ERRBCC             					(0x05)  // IC卡BCC错误
#define 	MF_ERRAUTH            					(0x06)  // MF卡验证失败
#define 	HF_ERRPARITY            				(0x07)  // IC卡奇偶校验错误


/////////////////////////////////////////////////////////////////////
// MIFARE status
/////////////////////////////////////////////////////////////////////
#define 	DARKSIDE_CANT_FIXED_NT          		(0x20)	// Darkside，无法固定随机数，这个情况可能出现在UID卡上
#define 	DARKSIDE_LUCK_AUTH_OK           		(0x21)	// Darkside，直接验证成功了，可能刚好密钥是空的
#define 	DARKSIDE_NACK_NO_SNED           		(0x22)	// Darkside，卡片不响应nack，可能是一张修复了nack逻辑漏洞的卡片
#define 	DARKSIDE_TAG_CHANGED            		(0x23)	// Darkside，在运行darkside的过程中出现了卡片切换，可能信号问题，或者真的是两张卡迅速切换了
#define 	NESTED_TAG_IS_STATIC            		(0x24)	// Nested，检测到卡片应答的随机数是固定的
#define 	NESTED_TAG_IS_HARD              		(0x25)	// Nested，检测到卡片应答的随机数是不可预测的


/////////////////////////////////////////////////////////////////////
// lf status
/////////////////////////////////////////////////////////////////////
#define 	LF_TAG_OK                 				(0x40)	// 低频卡的一些操作成功！
#define 	EM410X_TAG_NO_FOUND             		(0x41)	// 无法搜索到有效的EM410X标签


/////////////////////////////////////////////////////////////////////
// other status
/////////////////////////////////////////////////////////////////////
#define     STATUS_PAR_ERR                          (0x60)	// BLE指令传递的参数错误，或者是调用某些函数传递的参数错误
#define     STATUS_DEVIEC_MODE_ERROR                (0x66)  // 当前设备所处的模式错误，无法调用对应的API
#define     STATUS_INVALID_CMD                      (0x67)  // 无效的指令
#define     STATUS_DEVICE_SUCCESS                   (0x68)  // 设备相关操作成功执行
#define     STATUS_NOT_IMPLEMENTED                  (0x69)  // 调用了某些未实现的操作，属于开发者遗漏的错误
#endif
