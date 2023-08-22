#include "hex_utils.h"


/**
* @brief 	: 将大数字转换为HEX字节数组
* @param 	:n	  : 将被转换的值
* @param 	:len  : 存放转换后的数值的字节长度
* @param 	:dest : 存放转换结果的缓冲区
* @retval 	: 无
*
*/
void num_to_bytes(uint64_t n, uint8_t len, uint8_t* dest)
{
	while (len--) {
		dest[len] = (uint8_t)n;
		n >>= 8;
	}
}

/**
* @brief 	: 将字节数组转换为大数字
* @param	:len  : 存放数值的缓冲区的字节长度
* @param 	:src  : 存放数值的字节缓冲区
* @retval 	: 转换结果
*
*/
uint64_t bytes_to_num(uint8_t* src, uint8_t len)
{
	uint64_t num = 0;
	while (len--) {
		num = (num << 8) | (*src);
		src++;
	}
	return num;
}

