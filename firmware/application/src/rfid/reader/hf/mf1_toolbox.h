#ifndef MF1_TOOLBOX
#define MF1_TOOLBOX

#include <stdint.h>
#include <stdio.h>
#include <rc522.h>
#include <stdbool.h>
#include <stdlib.h>


#define SETS_NR			2  		// 使用几组随机数探针，至少是2个能确保有两组随机数组合进行交集查询，这个值越大越容易成功
#define DIST_NR			3   	// 越多的距离值越能准确判断当前卡片的通信稳定性

// mifare authentication
#define CRYPT_NONE    	0
#define CRYPT_ALL     	1
#define CRYPT_REQUEST 	2
#define AUTH_FIRST    	0
#define AUTH_NESTED   	2

typedef struct {			// 应答 nested 攻击需要的 距离参数
	uint8_t uid[4];			// 这个距离数据的所属UID的U32部分
	uint8_t distance[4];	// 未经加密的明文随机数
} NestedDist;


typedef struct {			// 应答 nested 攻击需要的 随机数参数
	uint8_t nt1[4];  		// 未经加密的明文随机数
	uint8_t nt2[4];			// 嵌套验证加密的随机数
	uint8_t par;			// 嵌套验证加密的通信过程的奇偶校验位，只用到了 '低3位'，也就是右3位
} NestedCore;

typedef struct {
	uint8_t uid[4];
	uint8_t nt[4];
	uint8_t par_list[8];
	uint8_t ks_list[8];
	uint8_t nr[4];
	uint8_t ar[4];
} DarksideCore;


#ifdef __cplusplus
extern "C" {
#endif


uint8_t Darkside_Recover_Key(
	uint8_t targetBlk,
	uint8_t targetTyp,
	uint8_t firstRecover,
	uint8_t ntSyncMax,
	DarksideCore* dc
);
uint8_t Nested_Distacne_Detect(
	uint8_t block,
	uint8_t type,
	uint8_t *key,
	NestedDist *nd
);
uint8_t Nested_Recover_Key(
    uint64_t keyKnown,
    uint8_t blkKnown,
    uint8_t typKnown,
    uint8_t targetBlock,
    uint8_t targetType,
    NestedCore ncs[SETS_NR]
);
uint8_t Check_Darkside_Support(void);
uint8_t Check_WeakNested_Support(void);
uint8_t Check_STDMifareNT_Support(void);
void Atenna_Switch_Delay(uint32_t delay_ms);
uint8_t auth_key_use_522_hw(uint8_t block, uint8_t type, uint8_t* key);

#ifdef __cplusplus
}
#endif

#endif
