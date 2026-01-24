#include <stdlib.h>

#include "nfc_mf1.h"
#include "nfc_14a.h"
#include "hex_utils.h"
#include "fds_util.h"
#include "tag_persistence.h"

#ifdef NFC_MF1_FAST_SIM
#include "mf1_crypto1.h"
#else
#include "crypto1_helper.h"
#endif

#define NRF_LOG_MODULE_NAME tag_mf1
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define MEM_KEY_A_OFFSET            48        /* Bytes */
#define MEM_KEY_B_OFFSET            58        /* Bytes */
#define MEM_KEY_BIGSECTOR_OFFSET    192
#define MEM_KEY_SIZE                6        /* Bytes */
#define MEM_ACC_GPB_SIZE            4        /* Bytes */
#define MEM_SECTOR_ADDR_MASK        0xFC
#define MEM_BIGSECTOR_ADDR_MASK     0xF0
#define MEM_BYTES_PER_BLOCK         16        /* Bytes */
#define MEM_VALUE_SIZE              4       /* Bytes */

/* NXP Originality check */
/* Sector 18/Block 68..71 is used to store signature data for NXP originality check */
#define MEM_EV1_SIGNATURE_BLOCK     68
#define MEM_EV1_SIGNATURE_TRAILER   ((MEM_EV1_SIGNATURE_BLOCK + 3 ) * MEM_BYTES_PER_BLOCK)


#define CMD_AUTH_A                  0x60
#define CMD_AUTH_B                  0x61
#define CMD_AUTH_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_AUTH_RB_FRAME_SIZE      4         /* Bytes */
#define CMD_AUTH_AB_FRAME_SIZE      8         /* Bytes */
#define CMD_AUTH_BA_FRAME_SIZE      4         /* Bytes */
#define CMD_HALT                    0x50
#define CMD_HALT_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_READ                    0x30
#define CMD_READ_FRAME_SIZE         2         /* Bytes without CRCA */
#define CMD_READ_RESPONSE_FRAME_SIZE 16       /* Bytes without CRCA */
#define CMD_WRITE                   0xA0
#define CMD_WRITE_FRAME_SIZE        2         /* Bytes without CRCA */
#define CMD_DECREMENT               0xC0
#define CMD_DECREMENT_FRAME_SIZE    2         /* Bytes without CRCA */
#define CMD_INCREMENT               0xC1
#define CMD_INCREMENT_FRAME_SIZE    2         /* Bytes without CRCA */
#define CMD_RESTORE                 0xC2
#define CMD_RESTORE_FRAME_SIZE      2         /* Bytes without CRCA */
#define CMD_TRANSFER                0xB0
#define CMD_TRANSFER_FRAME_SIZE     2         /* Bytes without CRCA */

#define CMD_CHINESE_UNLOCK          0x40
#define CMD_CHINESE_WIPE            0x41
#define CMD_CHINESE_UNLOCK_RW       0x43

/*
Source: NXP: MF1S50YYX Product data sheet

Access conditions for the sector trailer

Access bits     Access condition for                   Remark
            KEYA         Access bits  KEYB
C1 C2 C3        read  write  read  write  read  write
0  0  0         never key A  key A never  key A key A  Key B may be read[1]
0  1  0         never never  key A never  key A never  Key B may be read[1]
1  0  0         never key B  keyA|B never never key B
1  1  0         never never  keyA|B never never never
0  0  1         never key A  key A  key A key A key A  Key B may be read,
                                                       transport configuration[1]
0  1  1         never key B  keyA|B key B never key B
1  0  1         never never  keyA|B key B never never
1  1  1         never never  keyA|B never never never

[1] For this access condition key B is readable and may be used for data
*/
#define ACC_TRAILER_READ_KEYA   0x01
#define ACC_TRAILER_WRITE_KEYA  0x02
#define ACC_TRAILER_READ_ACC    0x04
#define ACC_TRAILER_WRITE_ACC   0x08
#define ACC_TRAILER_READ_KEYB   0x10
#define ACC_TRAILER_WRITE_KEYB  0x20



/*
Access conditions for data blocks
Access bits Access condition for                 Application
C1 C2 C3     read     write     increment     decrement,
                                                transfer,
                                                restore

0 0 0         key A|B key A|B key A|B     key A|B     transport configuration
0 1 0         key A|B never     never         never         read/write block
1 0 0         key A|B key B     never         never         read/write block
1 1 0         key A|B key B     key B         key A|B     value block
0 0 1         key A|B never     never         key A|B     value block
0 1 1         key B     key B     never         never         read/write block
1 0 1         key B     never     never         never         read/write block
1 1 1         never     never     never         never         read/write block

*/
#define ACC_BLOCK_READ      0x01
#define ACC_BLOCK_WRITE     0x02
#define ACC_BLOCK_INCREMENT 0x04
#define ACC_BLOCK_DECREMENT 0x08

#define KEY_A 0
#define KEY_B 1


/* Decoding table for Access conditions of the sector trailer */
static const uint8_t abTrailerAccessConditions[8][2] = {
    /* 0  0  0 RdKA:never WrKA:key A  RdAcc:key A WrAcc:never  RdKB:key A WrKB:key A      Key B may be read[1] */
    {
        /* Access with Key A */
        ACC_TRAILER_WRITE_KEYA | ACC_TRAILER_READ_ACC | ACC_TRAILER_WRITE_ACC | ACC_TRAILER_READ_KEYB | ACC_TRAILER_WRITE_KEYB,
        /* Access with Key B */
        0
    },
    /* 1  0  0 RdKA:never WrKA:key B  RdAcc:keyA|B WrAcc:never RdKB:never WrKB:key B */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC,
        /* Access with Key B */
        ACC_TRAILER_WRITE_KEYA | ACC_TRAILER_READ_ACC |  ACC_TRAILER_WRITE_KEYB
    },
    /* 0  1  0 RdKA:never WrKA:never  RdAcc:key A WrAcc:never  RdKB:key A WrKB:never  Key B may be read[1] */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC | ACC_TRAILER_READ_KEYB,
        /* Access with Key B */
        0
    },
    /* 1  1  0         never never  keyA|B never never never */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC,
        /* Access with Key B */
        ACC_TRAILER_READ_ACC
    },
    /* 0  0  1         never key A  key A  key A key A key A  Key B may be read,transport configuration[1] */
    {
        /* Access with Key A */
        ACC_TRAILER_WRITE_KEYA | ACC_TRAILER_READ_ACC | ACC_TRAILER_WRITE_ACC | ACC_TRAILER_READ_KEYB | ACC_TRAILER_WRITE_KEYB,
        /* Access with Key B */
        0
    },
    /* 0  1  1         never key B  keyA|B key B never key B */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC,
        /* Access with Key B */
        ACC_TRAILER_WRITE_KEYA | ACC_TRAILER_READ_ACC | ACC_TRAILER_WRITE_ACC | ACC_TRAILER_WRITE_KEYB
    },
    /* 1  0  1         never never  keyA|B key B never never */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC,
        /* Access with Key B */
        ACC_TRAILER_READ_ACC | ACC_TRAILER_WRITE_ACC
    },
    /* 1  1  1         never never  keyA|B never never never */
    {
        /* Access with Key A */
        ACC_TRAILER_READ_ACC,
        /* Access with Key B */
        ACC_TRAILER_READ_ACC
    },
};

// Save the current MF1 standard status
static nfc_tag_mf1_std_state_machine_t m_mf1_state = MF1_STATE_UNAUTHENTICATED;
// Save the current GEN1A status
static nfc_tag_mf1_gen1a_state_machine_t m_gen1a_state = GEN1A_STATE_DISABLE;
// Data structure pointer to the label information
static nfc_tag_mf1_information_t *m_tag_information = NULL;
// Define and use shadow anti -collision resources
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;
//Pind to the tail block in the label sector (control data block)
static nfc_tag_mf1_trailer_info_t *m_tag_trailer_info = NULL;
// Define and use MF1 special communication buffer
static nfc_tag_mf1_tx_buffer_t m_tag_tx_buffer;
//Save the specific type of MF1 currently being emulated
static tag_specific_type_t m_tag_type;

// Fast simulate is enable, we use internal crypto1 instance from 'mf1_crypto1.c'
#ifndef NFC_MF1_FAST_SIM
// mifare classic crypto1
static struct Crypto1State mpcs = {0, 0};
static struct Crypto1State *pcs = &mpcs;
#endif

// Define the buffer of the data that stored the detected data
// Place this data in a dormant RAM to save time and space to write into Flash
#define MF1_AUTH_LOG_MAX_SIZE   1000
static __attribute__((section(".noinit_mf1"))) struct nfc_tag_mf1_auth_log_buffer {
    uint32_t count;
    nfc_tag_mf1_auth_log_t logs[MF1_AUTH_LOG_MAX_SIZE];
} m_auth_log;

static uint8_t CardResponse[4];
static uint8_t ReaderResponse[4];
static uint8_t CurrentAddress;
static uint8_t KeyInUse;
static uint8_t m_data_block_buffer[MEM_BYTES_PER_BLOCK];

// MifareClassic crypto1 setup use fixed uid by cascade level
#define UID_BY_CASCADE_LEVEL (m_shadow_coll_res.uid + (*m_shadow_coll_res.size - NFC_TAG_14A_UID_SINGLE_SIZE))

#define BYTE_SWAP(x) (((uint8_t)(x)>>4)|((uint8_t)(x)<<4))
#define NO_ACCESS 0x07


/* decode Access conditions for a block */
uint8_t GetAccessCondition(uint8_t Block) {
    uint8_t  InvSAcc0;
    uint8_t  InvSAcc1;
    uint8_t  Acc0 = m_tag_trailer_info->acs[0];
    uint8_t  Acc1 = m_tag_trailer_info->acs[1];
    uint8_t  Acc2 = m_tag_trailer_info->acs[2];
    uint8_t  ResultForBlock = 0;

    InvSAcc0 = ~BYTE_SWAP(Acc0);
    InvSAcc1 = ~BYTE_SWAP(Acc1);

    /* Check */
    if (((InvSAcc0 ^ Acc1) & 0xf0) ||    /* C1x */
            ((InvSAcc0 ^ Acc2) & 0x0f) ||   /* C2x */
            ((InvSAcc1 ^ Acc2) & 0xf0)) {   /* C3x */
        return (NO_ACCESS);
    }
    /* Fix for MFClassic 4K cards */
    if (Block < 128)
        Block &= 3;
    else {
        Block &= 15;
        if (Block & 15)
            Block = 3;
        else if (Block <= 4)
            Block = 0;
        else if (Block <= 9)
            Block = 1;
        else
            Block = 2;
    }

    Acc0 = ~Acc0;       /* C1x Bits to bit 0..3 */
    Acc1 =  Acc2;       /* C2x Bits to bit 0..3 */
    Acc2 =  Acc2 >> 4;  /* C3x Bits to bit 0..3 */

    if (Block) {
        Acc0 >>= Block;
        Acc1 >>= Block;
        Acc2 >>= Block;
    }
    /* combine the bits */
    ResultForBlock = ((Acc2 & 1) << 2) |
                     ((Acc1 & 1) << 1) |
                     (Acc0 & 1);
    return (ResultForBlock);
}

bool CheckValueIntegrity(uint8_t *Block) {
    // Value Blocks contain a value stored three times, with the middle portion inverted.
    if ((Block[0] == (uint8_t) ~Block[4]) && (Block[0] == Block[8])
            && (Block[1] == (uint8_t) ~Block[5]) && (Block[1] == Block[9])
            && (Block[2] == (uint8_t) ~Block[6]) && (Block[2] == Block[10])
            && (Block[3] == (uint8_t) ~Block[7]) && (Block[3] == Block[11])
            && (Block[12] == (uint8_t) ~Block[13])
            && (Block[12] == Block[14])
            && (Block[14] == (uint8_t) ~Block[15])) {
        return true;
    } else {
        return false;
    }
}

void ValueFromBlock(uint32_t *Value, uint8_t *Block) {
    *Value = 0;
    *Value |= ((uint32_t) Block[0] << 0);
    *Value |= ((uint32_t) Block[1] << 8);
    *Value |= ((uint32_t) Block[2] << 16);
    *Value |= ((uint32_t) Block[3] << 24);
}

void ValueToBlock(uint8_t *Block, uint32_t Value) {
    Block[0] = (uint8_t)(Value >> 0);
    Block[1] = (uint8_t)(Value >> 8);
    Block[2] = (uint8_t)(Value >> 16);
    Block[3] = (uint8_t)(Value >> 24);
    Block[4] = ~Block[0];
    Block[5] = ~Block[1];
    Block[6] = ~Block[2];
    Block[7] = ~Block[3];
    Block[8] = Block[0];
    Block[9] = Block[1];
    Block[10] = Block[2];
    Block[11] = Block[3];
}

/** @brief MF1 Get a random number
 * @param nonce      Random number buffer
 */
void nfc_tag_mf1_random_nonce(uint8_t nonce[4], bool isNested) {
    // Use RAND to quickly generate random numbers, less performance loss
    // isNested provides more randomness for hardnested attack
    if (isNested) {
        nonce[0] = rand() & 0xff;
        nonce[1] = rand() & 0xff;
        nonce[2] = rand() & 0xff;
        nonce[3] = rand() & 0xff;
    } else {
        // fast for most readers
        num_to_bytes(rand(), 4, nonce);
    }
}

/**
 * @brief MF1 additional verification log, step 1, store basic information
 * @param isKeyB: Are you verifying the secret B
 * @param isNested:Whether it is undercover verification
 * @param block: The block currently verified
 * @param nonce: Brightly random number
 */
void append_mf1_auth_log_step1(bool isKeyB, bool isNested, uint8_t block, uint8_t *nonce) {
    // Power up for the first time, reset the buffer information
    if (m_auth_log.count == 0xFFFFFFFF) {
        m_auth_log.count = 0;
        NRF_LOG_INFO("Mifare Classic auth log buffer ready");
    }
    // Non -first -time call, see if you record whether the detection log is over the upper limit of the size
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        // Skill this operation directly over the upper limit.
        NRF_LOG_INFO("Mifare Classic auth log buffer overflow");
        return;
    }
    // Determine whether this card slot enables the detection log record
    if (m_tag_information->config.detection_enable) {
        m_auth_log.logs[m_auth_log.count].is_key_b = isKeyB;
        m_auth_log.logs[m_auth_log.count].block = block;
        m_auth_log.logs[m_auth_log.count].is_nested = isNested;
        memcpy(m_auth_log.logs[m_auth_log.count].uid, UID_BY_CASCADE_LEVEL, 4);
//        m_auth_log.logs[m_auth_log.count].nt = U32HTONL(*(uint32_t *)nonce);
        memcpy(m_auth_log.logs[m_auth_log.count].nt, nonce, 4);
    }
}

/** @brief MF1 additional verification log, step 2, store the encryption information of the read -ahead response
 * @param nr: The card reader is generated, the random number of encryption with the secret key
 * @param ar: The random number of the label, the random number of the read -headed head is encrypted
 */
void append_mf1_auth_log_step2(uint8_t *nr, uint8_t *ar) {
    // Determine to the upper limit and skip this operation directly to avoid covering the previous records
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        return;
    }
    if (m_tag_information->config.detection_enable) {
        // Cache encryption information
//        m_auth_log.logs[m_auth_log.count].nr = U32HTONL(*(uint32_t *)nr);
//        m_auth_log.logs[m_auth_log.count].ar = U32HTONL(*(uint32_t *)ar);
        memcpy(m_auth_log.logs[m_auth_log.count].nr, nr, 4);
        memcpy(m_auth_log.logs[m_auth_log.count].ar, ar, 4);
    }
}

/** @brief MF1 additional verification log, step 3, store the last verification or failure log
 * This step has completed the final statistics increase
 * @param is_auth_success: Whether to verify success
 */
void append_mf1_auth_log_step3(bool is_auth_success) {
    // Determine to the upper limit and skip this operation directly to avoid covering the previous records
    if (m_auth_log.count > MF1_AUTH_LOG_MAX_SIZE) {
        return;
    }
    if (m_tag_information->config.detection_enable) {
        // Then you can end this record, the number of statistics increases
        m_auth_log.count += 1;
        // Print the number of logs in the current record
        NRF_LOG_INFO("Auth log count: %d", m_auth_log.count);
    }
}

/** @brief MF1 obtain verification log
 * @param count: The statistics of the verification log
 */
nfc_tag_mf1_auth_log_t *mf1_get_auth_log(uint32_t *count) {
    // First pass the total number of logs verified by verified
    *count = m_auth_log.count;
    // Just return to the head pointer of the log number array
    return m_auth_log.logs;
}

static int get_block_max_by_tag_type(tag_specific_type_t tag_type) {
    int block_max;
    switch (tag_type) {
        case TAG_TYPE_MIFARE_Mini:
            block_max = 20;
            break;
        default:
        case TAG_TYPE_MIFARE_1024:
            block_max = 64;
            break;
        case TAG_TYPE_MIFARE_2048:
            block_max = 128;
            break;
        case TAG_TYPE_MIFARE_4096:
            block_max = 256;
            break;
    }
    return block_max;
}

static bool check_block_max_overflow(uint8_t block) {
    uint8_t block_max = get_block_max_by_tag_type(m_tag_type) - 1;
    return block > block_max;
}

#ifndef NFC_MF1_FAST_SIM
void mf1_prng_by_bytes(uint8_t *nonces, uint32_t n) {
    uint32_t nonces_u32 = bytes_to_num(nonces, 4);
    nonces_u32 = prng_successor(nonces_u32, n);
    num_to_bytes(nonces_u32, 4, nonces);
}
#endif

void mf1_response_4bit_auto_encrypt(uint8_t value) {
#ifdef NFC_MF1_FAST_SIM
    nfc_tag_14a_tx_nbit(value ^ Crypto1Nibble(), 4);
#else
    nfc_tag_14a_tx_nbit(mf_crypto1_encrypt4bit(pcs, value), 4);
#endif
}

/** @brief MF1 status machine
 * @param data      From reading head data
 * @param szBits    length of data
 * @param state     Finite State Machine
 */
void nfc_tag_mf1_state_handler(uint8_t *p_data, uint16_t szDataBits) {
    //Special instructions, such as compatible with MiFare Gen1a label
    if (szDataBits <= 8) {
        // Only when the Gen1A mode is enabled, the response of the back door instruction is allowed
        if (m_tag_information->config.mode_gen1a_magic) {
            if (szDataBits == 7 && p_data[0] == CMD_CHINESE_UNLOCK) {
                // The first step back door card verification
                // NRF_LOG_INFO("MIFARE_MAGICWUPC1 received.\n");
                m_gen1a_state = GEN1A_STATE_UNLOCKING;
                nfc_tag_14a_tx_nbit(ACK_VALUE, 4);
            } else if (szDataBits == 8 && p_data[0] == CMD_CHINESE_UNLOCK_RW) {
                // The second back door card verification
                if (m_gen1a_state == GEN1A_STATE_UNLOCKING) {
                    // NRF_LOG_INFO("MIFARE_MAGICWUPC2 received.\n");
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_ACTIVE);    //Update the status machine of the external 14A
                    m_gen1a_state = GEN1A_STATE_UNLOCKED_RW_WAIT;       // Update the Gen1A status machine
                    m_mf1_state = MF1_STATE_UNAUTHENTICATED;                     // Update MF1 status machine
                    nfc_tag_14a_tx_nbit(ACK_VALUE, 4);     //Reply to the card reader Gen1a label unlock the back door success
#ifndef NFC_MF1_FAST_SIM
                    crypto1_deinit(pcs);                                // Reset crypto1 handler
#endif
                } else {
                    m_gen1a_state = GEN1A_STATE_DISABLE;                // If you find that you have not taken the first step, directly reset the Gen1a status machine
                }
            }
        }
        // Remember, no matter what the byte frame is processed here, it will end directly after processing
        // Do not transfer non -byte frames to the logic below
        return;
    }

    // Processing MiFare's status machine
    switch (m_mf1_state) {
        case MF1_STATE_UNAUTHENTICATED: {    // Unparalleled state, communication is open
            if (szDataBits == 32) {    // 32 -bit, may be instructions
                if (nfc_tag_14a_checks_crc(p_data, 4)) {
                    switch (p_data[0]) {
                        case CMD_AUTH_A:
                        case CMD_AUTH_B: {
                            uint8_t BlockAuth = p_data[1];
                            uint8_t CardNonce[4];
                            uint8_t BlockStart;
                            uint8_t BlockEnd;

                            // Get the starting block of the corresponding sector that is visited, keep in mind: 4K cards have large sectors, and 16 blocks are used as one sector unit
                            // Calculate ideas: x = (y / n) * n, x = starting block of the sector, y = verified block, n = y's number of blocks where the sector is located
                            // Thinking analysis: First do divisions to get the current sector, and then multiply to obtain the number of blocks in the sector
                            if (BlockAuth >= 128) {
                                BlockStart = (BlockAuth / 16) * 16;
                                BlockEnd = BlockStart + 16 - 1;
                            } else {
                                // Non -4K card, step by step with a small sector
                                BlockStart = (BlockAuth / 4) * 4;
                                BlockEnd = BlockStart + 4 - 1;
                            }

                            // The type of current emulation card is not enough to support the access of the card reader
                            if (check_block_max_overflow(BlockAuth)) {
                                break;
                            }

                            // Set KeyInUse as global use to retain information about identity verification
                            KeyInUse = p_data[0] & 1;

                            // Obtain the specified sector access control bytes. Here we directly take the coincidence, convert the memory into a structure, and let the compiler help us maintain the pointing of the pointer
                            m_tag_trailer_info = (nfc_tag_mf1_trailer_info_t *)m_tag_information->memory[BlockEnd];

                            // Generate random number
                            nfc_tag_mf1_random_nonce(CardNonce, false);

                            // Calculate the card reader in advance according to the card random number
                            for (uint8_t i = 0; i < sizeof(ReaderResponse); i++) {
                                ReaderResponse[i] = CardNonce[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(ReaderResponse, 64);
#else
                            mf1_prng_by_bytes(ReaderResponse, 64);
#endif

                            // Calculate our response based on the response from the card reader
                            for (uint8_t i = 0; i < sizeof(CardResponse); i++) {
                                CardResponse[i] = ReaderResponse[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(CardResponse, 32);
#else
                            mf1_prng_by_bytes(CardResponse, 32);
#endif

                            // Record verification log
                            append_mf1_auth_log_step1(KeyInUse, false, BlockAuth, CardNonce);

                            // Use random card random numbers to respond, and hopes to obtain further authentication from the reader in the next frame.
                            m_mf1_state = MF1_STATE_AUTHENTICATING;

                            // The first verification, responding to a clear random number, without CRC
                            m_tag_tx_buffer.tx_raw_buffer[0] = CardNonce[0];
                            m_tag_tx_buffer.tx_raw_buffer[1] = CardNonce[1];
                            m_tag_tx_buffer.tx_raw_buffer[2] = CardNonce[2];
                            m_tag_tx_buffer.tx_raw_buffer[3] = CardNonce[3];

#ifdef NFC_MF1_FAST_SIM
                            Crypto1Setup(
                                // Select A or B secrets based on the current instruction type
                                KeyInUse ? m_tag_trailer_info->key_b : m_tag_trailer_info->key_a,
                                // Passing the current anti -collision UID
                                UID_BY_CASCADE_LEVEL,
                                // Passing into a clear random number, this random number will be used to decrypt subsequent communication
                                CardNonce
                            );
#else
                            // Set the Crypto1 key flow and discard the previous encryption state
                            crypto1_deinit(pcs);
                            // Load key flow
                            crypto1_init(pcs,
                                         // Select A or B secrets based on the current instruction type
                                         bytes_to_num(KeyInUse ? m_tag_trailer_info->key_b : m_tag_trailer_info->key_a, 6)
                                        );
                            // Set key flow
                            crypto1_word(pcs, bytes_to_num(UID_BY_CASCADE_LEVEL, 4) ^ bytes_to_num(CardNonce, 4), 0);
#endif
                            // Responsible for clear -scale random number to read the card reader
                            nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_raw_buffer, 4, false);
                            break;
                        }
                        case CMD_READ: {
                            // I received a block -related reading instruction without verification.
                            if (m_gen1a_state == GEN1A_STATE_UNLOCKED_RW_WAIT) {
                                CurrentAddress = p_data[1];
                                memcpy(m_tag_tx_buffer.tx_raw_buffer, m_tag_information->memory[CurrentAddress], NFC_TAG_MF1_DATA_SIZE);
                                nfc_tag_14a_tx_bytes(m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_DATA_SIZE, true);
                            } else {
                                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
                            }
                            break;
                        }
                        case CMD_WRITE: {
                            // Explanation
                            if (m_gen1a_state == GEN1A_STATE_UNLOCKED_RW_WAIT) {
                                //Save the block and update status machine to be written
                                CurrentAddress = p_data[1];
                                m_gen1a_state = GEN1A_STATE_WRITING;
                                // Responsive ACK, let the read head continue the next step data to come over
                                nfc_tag_14a_tx_nbit(ACK_VALUE, 4);
                            } else {
                                nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
                            }
                            break;
                        }
                        default: {
                            // When the state is not verified, read and write cards directly when the back door mode is turned on
                            // In addition to initiating verification instructions, the others can do nothing
                            nfc_tag_14a_tx_nbit(NAK_INVALID_OPERATION_TBIV, 4);
                            break;
                        }
                    }
                } else {
                    // CRC verification abnormal
                    nfc_tag_14a_tx_nbit(NAK_CRC_PARITY_ERROR_TBIV, 4);
                    return;
                }
            } else {
                if (szDataBits == 144 && m_gen1a_state == GEN1A_STATE_WRITING) {
                    // Determine that we are written into the block operation under the Gen1a mode
                    if (nfc_tag_14a_checks_crc(p_data, NFC_TAG_MF1_FRAME_SIZE)) {
                        // The data verification passes, we need to put the data sent in RAM
                        memcpy(m_tag_information->memory[CurrentAddress], p_data, NFC_TAG_MF1_DATA_SIZE);
                        // Restore the Gen1A special state machine for waiting operation status
                        m_gen1a_state = GEN1A_STATE_UNLOCKED_RW_WAIT;
                        // Reply to read head ACK, complete the writing operation
                        nfc_tag_14a_tx_nbit(ACK_VALUE, 4);
                    } else {
                        // The transmitted CRC verification is abnormal, and you cannot continue writing
                        nfc_tag_14a_tx_nbit(NAK_CRC_PARITY_ERROR_TBIV, 4);
                    }
                } else {
                    // If you wait for the instruction status to the non -4BYTE instruction, it is considered abnormal
                    // At this time, you need to reset the state machine
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                }
            }
            break;
        }

        case MF1_STATE_AUTHENTICATING: {
            if (szDataBits == 64) {
                //NR + AR responded to the card reader
                append_mf1_auth_log_step2(p_data, &p_data[4]);
#ifdef NFC_MF1_FAST_SIM
                // Reader delivers an encrypted nonce. We use it to setup the crypto1 LFSR in nonlinear feedback mode. Furthermore it delivers an encrypted answer. Decrypt and check it
                Crypto1Auth(&p_data[0]);
                Crypto1ByteArray(&p_data[4], 4);
#else
                // NR, a random number generated by a card reader
                uint32_t nr = bytes_to_num(p_data, 4);
                // AR, is the encrypted data that we responded in the first step of the card encryption
                uint32_t ar = bytes_to_num(&p_data[4], 4);
                // --- crypto
                crypto1_word(pcs, nr, 1);
                num_to_bytes(ar ^ crypto1_word(pcs, 0, 0), 4, &p_data[4]);
#endif
                // Was the random number of the return of the card reader was sent by us
                if ((p_data[4] == ReaderResponse[0]) && (p_data[5] == ReaderResponse[1]) && (p_data[6] == ReaderResponse[2]) && (p_data[7] == ReaderResponse[3])) {
                    // The reader has passed the authentication.The estimated calculation card response data and generating the puppet test position.
                    m_tag_tx_buffer.tx_raw_buffer[0] = CardResponse[0];
                    m_tag_tx_buffer.tx_raw_buffer[1] = CardResponse[1];
                    m_tag_tx_buffer.tx_raw_buffer[2] = CardResponse[2];
                    m_tag_tx_buffer.tx_raw_buffer[3] = CardResponse[3];
                    //Encryption and calculation of the puppet school inspection
#ifdef NFC_MF1_FAST_SIM
                    Crypto1ByteArrayWithParity(m_tag_tx_buffer.tx_raw_buffer, m_tag_tx_buffer.tx_bit_parity, 4);
#else
                    mf_crypto1_encrypt(pcs, m_tag_tx_buffer.tx_raw_buffer, 4, m_tag_tx_buffer.tx_bit_parity);
#endif
                    // The verification is successful, and you need to enter the state that has been successfully verified
                    m_mf1_state = MF1_STATE_AUTHENTICATED;
                    // Package, stitch the Qiqi school inspection, return
                    m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 32, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                    nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                } else {
                    // Temporary only stored verification failed logs
                    append_mf1_auth_log_step3(false);
                    // Verification failure, reset the status machine
                    nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                }
            } else {
                // The length of the data sent by the reading head during the verification process is wrong, it must be a problem
                // We can only reset the status machine and wait for the operation instructions to re -initiate
                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
            }
            break;
        }

        case MF1_STATE_AUTHENTICATED: {
            if (szDataBits == 32) {
                // In this state, all communication is encrypted.Therefore, we must first decrypt the data sent by the read head.
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, 4);
#else
                mf_crypto1_decryptEx(pcs, p_data, 4, p_data);
#endif
                // After the decryption is completed, check whether the CRC is correct, and we must ensure that the data coming over is correct!
                if (nfc_tag_14a_checks_crc(p_data, 4)) {
                    switch (p_data[0]) {
                        case CMD_READ: {
                            // Save the block address of the current operation
                            CurrentAddress = p_data[1];
                            // Generate access control, for data access control below
                            uint8_t Acc = abTrailerAccessConditions[ GetAccessCondition(CurrentAddress) ][ KeyInUse ];
                            // Read the command.Read data from memory and add CRCA.Note: Reading operations are limited by the control bit, but at present we only restrict the reading of the control bit
                            if ((CurrentAddress < 128 && (CurrentAddress & 3) == 3) || ((CurrentAddress & 15) == 15)) {
                                // Clear the buffer to avoid the cache data that affect the follow -up operation
                                memset(m_tag_tx_buffer.tx_raw_buffer, 0x00, sizeof(m_tag_tx_buffer.tx_raw_buffer));
                                // Make this data area into the type of tail blocks we need
                                nfc_tag_mf1_trailer_info_t *respTrailerInfo = (nfc_tag_mf1_trailer_info_t *)m_tag_tx_buffer.tx_raw_buffer;
                                // The reading of the tail block has the following conditions:
                                // 1. Always copy GPB (Global Public Byte), which is the last byte of the control bit
                                // 2. Secret A can never be read!
                                // 3. Make the restrictions of the control position reading according to the access conditions of the read during authentication!
                                respTrailerInfo->acs[3] = m_tag_trailer_info->acs[3];
                                // Determine whether the control position itself allows reading
                                if (Acc & ACC_TRAILER_READ_ACC) {
                                    respTrailerInfo->acs[0] = m_tag_trailer_info->acs[0];
                                    respTrailerInfo->acs[1] = m_tag_trailer_info->acs[1];
                                    respTrailerInfo->acs[2] = m_tag_trailer_info->acs[2];
                                }
                                // In a few cases, the Secret B is readable
                                if (Acc & ACC_TRAILER_READ_KEYB) {
                                    memcpy(respTrailerInfo->key_b, m_tag_trailer_info->key_b, 6);
                                }
                            } else {
                                // For data, just return to the corresponding location sector
                                memcpy(m_tag_tx_buffer.tx_raw_buffer, m_tag_information->memory[CurrentAddress], 16);
                            }
                            // In any case, the data of the reply must be calculated CRC
                            nfc_tag_14a_append_crc(m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_DATA_SIZE);
                            // Reply and calculate the coupling school inspection to reply to the card reader
#ifdef NFC_MF1_FAST_SIM
                            Crypto1ByteArrayWithParity(m_tag_tx_buffer.tx_raw_buffer, m_tag_tx_buffer.tx_bit_parity, NFC_TAG_MF1_FRAME_SIZE);
#else
                            mf_crypto1_encrypt(pcs, m_tag_tx_buffer.tx_raw_buffer, NFC_TAG_MF1_FRAME_SIZE, m_tag_tx_buffer.tx_bit_parity);
#endif
                            // Combined Qiqi School Check Data Frame
                            m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 144, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                            // Start sending
                            nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                            return;
                        }
                        case CMD_WRITE: {
                            //  Normal cards are not allowed to write block0, otherwise it will be recognized by CUID firewall
                            if (p_data[1] == 0x00 && !m_tag_information->config.mode_gen2_magic) {
                                // Reset the 14A state machine directly, let the label sleep
                                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_HALTED);
                                // Tell me to read the head. This operation is not allowed to be allowed
                                mf1_response_4bit_auto_encrypt(NAK_INVALID_OPERATION_TBIV);
                            } else {
                                // Normally write command.Store the address and prepare to receive the upcoming data.
                                CurrentAddress = p_data[1];
                                m_mf1_state = MF1_STATE_WRITE;
                                // Take ACK response, inform the reading head we are ready
                                mf1_response_4bit_auto_encrypt(ACK_VALUE);
                            }
                            return;
                        }
                        // Although I think the following three case code is a bit stupid. Except for the different other ones, the space is the same, but the space is changed (psychological comfort)
                        case CMD_DECREMENT: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_DECREMENT;
                            mf1_response_4bit_auto_encrypt(ACK_VALUE);
                            break;
                        }
                        case CMD_INCREMENT: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_INCREMENT;
                            mf1_response_4bit_auto_encrypt(ACK_VALUE);
                            break;
                        }
                        case CMD_RESTORE: {
                            CurrentAddress = p_data[1];
                            m_mf1_state = MF1_STATE_RESTORE;
                            mf1_response_4bit_auto_encrypt(ACK_VALUE);
                            break;
                        }
                        case CMD_TRANSFER: {
                            uint8_t status;
                            // Do not judge the current writing mode here to control the writing mode
                            if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DENIED) {
                                // Under this mode directly reject operation
                                status = NAK_INVALID_OPERATION_TBIV;
                            } else if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DECEIVE) {
                                // This mode responds to ACK, but it is not written in RAM
                                status = ACK_VALUE;
                            } else {
                                // Write the block address specified by the global buffer back in the instruction parameter
                                memcpy(m_tag_information->memory[p_data[1]], m_data_block_buffer, MEM_BYTES_PER_BLOCK);
                                status = ACK_VALUE;
                            }
                            mf1_response_4bit_auto_encrypt(status);
                            break;
                        }
                        case CMD_AUTH_A:
                        case CMD_AUTH_B: {
                            // The second verification request when it has been encrypted is the process of nested verification
                            uint8_t BlockAuth = p_data[1];
                            uint8_t CardNonce[4];
                            uint8_t BlockStart;
                            uint8_t BlockEnd;

                            // The starting block of the corresponding sector that is visited, keep in mind: 4K cards have large sectors, with 16 blocks as one sector unit
                            // Calculate ideas: x = (y / n) * n, x = starting block of the sector, y = verified block, n = y's number of blocks where the sector is located
                            // Thinking analysis: First do divisions to get the current sector, and then multiply to obtain the number of blocks in the sector
                            if (BlockAuth >= 128) {
                                BlockStart = (BlockAuth / 16) * 16;
                                BlockEnd = BlockStart + 16 - 1;
                            } else {
                                //Non -4K card, step by step with a small sector
                                BlockStart = (BlockAuth / 4) * 4;
                                BlockEnd = BlockStart + 4 - 1;
                            }

                            // The type of current emulation card is not enough to support the access of the card reader
                            if (check_block_max_overflow(BlockAuth)) {
                                break;
                            }

                            // Set KeyInUse as global use to retain information about identity verification
                            KeyInUse = p_data[0] & 1;

                            // Obtain the specified sector access control bytes. Here we directly take the coincidence, convert the memory into a structure, and let the compiler help us maintain the pointing of the pointer
                            m_tag_trailer_info = (nfc_tag_mf1_trailer_info_t *)m_tag_information->memory[BlockEnd];

                            // Generate random number
                            nfc_tag_mf1_random_nonce(CardNonce, true);

                            // Calculate the card reader response based on the card random number
                            for (uint8_t i = 0; i < sizeof(ReaderResponse); i++) {
                                ReaderResponse[i] = CardNonce[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(ReaderResponse, 64);
#else
                            mf1_prng_by_bytes(ReaderResponse, 64);
#endif

                            // Calculate our response based on the response from the card reader
                            for (uint8_t i = 0; i < sizeof(CardResponse); i++) {
                                CardResponse[i] = ReaderResponse[i];
                            }
#ifdef NFC_MF1_FAST_SIM
                            Crypto1PRNG(CardResponse, 32);
#else
                            mf1_prng_by_bytes(CardResponse, 32);
#endif

                            // Record nested verification information
                            append_mf1_auth_log_step1(KeyInUse, true, BlockAuth, CardNonce);

                            //Use random card random numbers to respond, and hopes to obtain further authentication from the reader in the next frame.
                            m_mf1_state = MF1_STATE_AUTHENTICATING;

                            // Copy a random number of a label to the buffer area
                            m_tag_tx_buffer.tx_raw_buffer[0] = CardNonce[0];
                            m_tag_tx_buffer.tx_raw_buffer[1] = CardNonce[1];
                            m_tag_tx_buffer.tx_raw_buffer[2] = CardNonce[2];
                            m_tag_tx_buffer.tx_raw_buffer[3] = CardNonce[3];

#ifdef NFC_MF1_FAST_SIM
                            /* Setup crypto1 cipher. Discard in-place encrypted CardNonce. */
                            Crypto1SetupNested(
                                // Select A or B secrets based on the current instruction type
                                KeyInUse ? m_tag_trailer_info->key_b : m_tag_trailer_info->key_a,
                                // Passing the current anti -collision UID
                                UID_BY_CASCADE_LEVEL,
                                // Passing into a clear random number, this random number will be encrypted and passed through this buffer area
                                m_tag_tx_buffer.tx_raw_buffer,
                                // A buffer that passed into a strange school inspection of random numbers
                                m_tag_tx_buffer.tx_bit_parity,
                                // Explain the user: Decrypt = false for the tag, decrypt = true for the reader
                                // We are currently a label character, so we are introduced into false
                                false
                            );
#else
                            // Set the Crypto1 key flow and discard the previous encryption state
                            crypto1_deinit(pcs);
                            //Load key flow
                            crypto1_init(pcs,
                                         // Select A or B secrets based on the current instruction type
                                         bytes_to_num(KeyInUse ? m_tag_trailer_info->key_b : m_tag_trailer_info->key_a, 6)
                                        );
                            // Random number encryption
                            uint8_t m_auth_nt_keystream[4];
                            num_to_bytes(bytes_to_num(UID_BY_CASCADE_LEVEL, 4) ^ bytes_to_num(CardNonce, 4), 4, m_auth_nt_keystream);
                            mf_crypto1_encryptEx(pcs, CardNonce, m_auth_nt_keystream, m_tag_tx_buffer.tx_raw_buffer, 4, m_tag_tx_buffer.tx_bit_parity);
#endif
                            // In the case of nested verification, after the frame is set up, a encrypted random number is replied, and the puppet school inspection does not bring CRC
                            m_tag_tx_buffer.tx_frame_bit_size = nfc_tag_14a_wrap_frame(m_tag_tx_buffer.tx_raw_buffer, 32, m_tag_tx_buffer.tx_bit_parity, m_tag_tx_buffer.tx_warp_frame);
                            nfc_tag_14a_tx_bits(m_tag_tx_buffer.tx_warp_frame, m_tag_tx_buffer.tx_frame_bit_size);
                            break;
                        }
                        case CMD_HALT: {
                            // Let the label sleep.According to the ISO14443 agreement, the second byte should be 0.
                            if (p_data[1] == 0x00) {
                                // If everything is normal, then we should make the card directly to sleep, and cannot respond to any message to the read head
                                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_HALTED);
                            } else {
                                mf1_response_4bit_auto_encrypt(NAK_INVALID_OPERATION_TBIV);
                            }
                            break;
                        }
                        default: {
                            // If you read your hair, you don't know what ghost instructions, we can't handle it,
                            // Therefore, the task is abnormal, and the status needs to be reset, and the response to the reading head will not support this instruction
                            nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                            mf1_response_4bit_auto_encrypt(NAK_INVALID_OPERATION_TBIV);
                            break;
                        }
                    }
                } else {
                    // CRC is wrong, return the error code notification
                    mf1_response_4bit_auto_encrypt(NAK_INVALID_OPERATION_TBIV);
                    break;
                }
            } else {
                // It has been verified that the secrets are idle but did not receive the normal 4BYTE instructions, we need to reset the status machine
                nfc_tag_14a_set_state(NFC_TAG_STATE_14A_IDLE);
                break;
            }
            break;
        }

        case MF1_STATE_WRITE: {
            uint8_t status;
            //It is currently in a state machine, we need to ensure that the received data is sufficient length
            if (szDataBits == 144) {
                // Decrypted the 16 -byte to be written in data and 2 -byte CRCA
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, NFC_TAG_MF1_FRAME_SIZE);
#else
                mf_crypto1_decryptEx(pcs, p_data, NFC_TAG_MF1_FRAME_SIZE, p_data);
#endif
                //The CRC that checks the data, ensure that the data received again is correct
                if (nfc_tag_14a_checks_crc(p_data, NFC_TAG_MF1_FRAME_SIZE)) {
                    // Do not judge the current writing mode here to control the writing mode
                    if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DENIED) {
                        // Under this mode directly reject operation
                        status = NAK_INVALID_OPERATION_TBIV;
                    } else if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_DECEIVE) {
                        // This mode responds to ACK, but it is not written in RAM
                        status = ACK_VALUE;
                    } else {
                        // Other remaining modes can be updated to the labeled RAM
                        memcpy(m_tag_information->memory[CurrentAddress], p_data, NFC_TAG_MF1_DATA_SIZE);
                        status = ACK_VALUE;
                    }
                } else {
                    status = NAK_CRC_PARITY_ERROR_TBIV;
                }
            } else {
                status = NAK_CRC_PARITY_ERROR_TBIV;
            }
            // In any case, after the operation, the label will be allowed to return to the verification idle state
            m_mf1_state = MF1_STATE_AUTHENTICATED;
            mf1_response_4bit_auto_encrypt(status);
            break;
        }

        case MF1_STATE_DECREMENT:
        case MF1_STATE_INCREMENT:
        case MF1_STATE_RESTORE: {
            uint8_t status;
            if (szDataBits == (MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH) * 8) {
                //When we arrived here, we have issued a decrease, increasing or recovery command, and the reader is now sending data.
                // First, decrypt the data and check the CRC.Read the data in the requested block address into the global block buffer and check the integrity.
                // Then, if necessary, add or decrease according to the command issued, and store the block back to the global block buffer.
#ifdef NFC_MF1_FAST_SIM
                Crypto1ByteArray(p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH);
#else
                mf_crypto1_decryptEx(pcs, p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH, p_data);
#endif
                // After decomposition, CRC must be verified to avoid using error data
                if (nfc_tag_14a_checks_crc(p_data, MEM_VALUE_SIZE + NFC_TAG_14A_CRC_LENGTH)) {
                    // Copy a piece of data first to the global buffer zone
                    memcpy(m_data_block_buffer, m_tag_information->memory[CurrentAddress], MEM_BYTES_PER_BLOCK);
                    // Check whether the value block is valid
                    if (CheckValueIntegrity(m_data_block_buffer)) {
                        //Get the value stored in the current parameter value and block
                        uint32_t value_param, value_block;
                        ValueFromBlock(&value_param, p_data);
                        ValueFromBlock(&value_block, m_data_block_buffer);
                        // Do the corresponding increase or decrease operation
                        if (m_mf1_state == MF1_STATE_DECREMENT) {
                            value_block -= value_param;
                        } else if (m_mf1_state == MF1_STATE_INCREMENT) {
                            value_block += value_param;
                        } else if (m_mf1_state == MF1_STATE_RESTORE) {
                            // Do nothing
                        }
                        // Convert the value to Block data
                        ValueToBlock(m_data_block_buffer, value_block);
                        // No ACK response on value commands part 2
                        m_mf1_state = MF1_STATE_AUTHENTICATED;
                        break;
                    } else {
                        // The answers here may be wrong, or maybe no answer is required at all
                        status = NAK_OTHER_ERROR;
                    }
                } else {
                    // CRC error
                    status = NAK_CRC_PARITY_ERROR_TBIV;
                }
            } else {
                // The length is wrong, but it is counted in the CRC error
                status = NAK_CRC_PARITY_ERROR_TBIV;
            }
            m_mf1_state = MF1_STATE_AUTHENTICATED;
            mf1_response_4bit_auto_encrypt(status);
            break;
        }

        default: {
            // Unknown state?This will never happen, unless the developer has a problem with the brain!
            NRF_LOG_INFO("Unknown MF1 State");
            break;
        }
    }
}

/**
 * @brief Provide the necessary anti -conflict resources for the MiFare label (only pointer provides pointers)
 */
nfc_tag_14a_coll_res_reference_t *get_mifare_coll_res() {
    //According to the current interoperability configuration, selectively return the configuration data to selectively, assuming that the data interoperability is turned on, then we also need to ensure that the current emulation card is 4BYTE
    if (m_tag_information->config.use_mf1_coll_res && m_tag_information->res_coll.size == NFC_TAG_14A_UID_SINGLE_SIZE) {
        // Manufacturer information obtained by the data area
        nfc_tag_mf1_factory_info_t *block0_factory_info = (nfc_tag_mf1_factory_info_t *)m_tag_information->memory[0];
        m_shadow_coll_res.sak = block0_factory_info->sak;               //Replace SAK
        m_shadow_coll_res.atqa = block0_factory_info->atqa;             //Replace ATQA
        m_shadow_coll_res.uid = block0_factory_info->uid;               // Replace UID
        m_shadow_coll_res.size = &(m_tag_information->res_coll.size);   // Reuse type
        m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);     // Reuse ATS
    } else {
        // Use a separate anti -conflict information instead of using the information in the sector
        m_shadow_coll_res.sak = m_tag_information->res_coll.sak;
        m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
        m_shadow_coll_res.uid = m_tag_information->res_coll.uid;
        m_shadow_coll_res.size = &(m_tag_information->res_coll.size);
        m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);
    }
    // Finally, a shadow data structure pointer with only reference, no physical shadow,
    return &m_shadow_coll_res;
}


nfc_tag_14a_coll_res_reference_t *get_saved_mifare_coll_res() {
    // Always give saved data, not from block 0
    m_shadow_coll_res.sak = m_tag_information->res_coll.sak;
    m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
    m_shadow_coll_res.uid = m_tag_information->res_coll.uid;
    m_shadow_coll_res.size = &(m_tag_information->res_coll.size);
    m_shadow_coll_res.ats = &(m_tag_information->res_coll.ats);
    return &m_shadow_coll_res;
}

/**
 * @brief Reconcile when the parameter label needs to be reset
 */
void nfc_tag_mf1_reset_handler() {
    m_mf1_state = MF1_STATE_UNAUTHENTICATED;
    m_gen1a_state = GEN1A_STATE_DISABLE;

#ifndef NFC_MF1_FAST_SIM
    // Must to reset pcs handler
    crypto1_deinit(pcs);
#endif
}

/** @brief Obtain the length of effective information for the information structure
 * @param type     Refined label type
 * @return Suppose type == tag_type_mifare_1024,
 * The length of the information should be the anti -collision information plus the configuration information plus the length of the sector
 */
static int get_information_size_by_tag_type(tag_specific_type_t type) {
    return sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_mf1_configure_t) + (get_block_max_by_tag_type(type) * NFC_TAG_MF1_DATA_SIZE);
}

/** @brief MF1's callback before saving data
 * @param type      Refined label type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int nfc_tag_mf1_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (m_tag_type != TAG_TYPE_UNDEFINED) {
        if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_SHADOW) {
            NRF_LOG_INFO("The mf1 is shadow write mode.");
            return 0;
        }
        if (m_tag_information->config.mode_block_write == NFC_TAG_MF1_WRITE_SHADOW_REQ) {
            NRF_LOG_INFO("The mf1 will be set to shadow write mode.");
            m_tag_information->config.mode_block_write = NFC_TAG_MF1_WRITE_SHADOW;
        }
        // Save the corresponding size data according to the current label type
        return get_information_size_by_tag_type(type);
    } else {
        return 0;
    }
}

/** @brief MF1 load data
 * @param type     Refined label type
 * @param buffer   Data buffer
 */
int nfc_tag_mf1_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure that external capacity is enough to convert to an information structure
    int info_size = get_information_size_by_tag_type(type);
    if (buffer->length >= info_size) {
        //Convert the data buffer to MF1 structure type
        m_tag_information = (nfc_tag_mf1_information_t *)buffer->buffer;
        // The specific type of MF1 that is emulated by the cache
        m_tag_type = type;
        // Register 14A communication management interface
        nfc_tag_14a_handler_t handler_for_14a = {
            .get_coll_res = get_mifare_coll_res,
            .cb_state = nfc_tag_mf1_state_handler,
            .cb_reset = nfc_tag_mf1_reset_handler,
        };
        nfc_tag_14a_set_handler(&handler_for_14a);
        NRF_LOG_INFO("HF mf1 config 'field_off_do_reset' = %d", m_tag_information->config.field_off_do_reset);
        nfc_tag_14a_set_reset_enable(m_tag_information->config.field_off_do_reset);
        NRF_LOG_INFO("HF mf1 data load finish.");
    } else {
        NRF_LOG_ERROR("nfc_tag_mf1_information_t too big.");
    }
    return info_size;
}

// Factory data for initialization of MF1
bool nfc_tag_mf1_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default mf1 data
    uint8_t default_blk0[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x22, 0x08, 0x04, 0x00, 0x01, 0x77, 0xA2, 0xCC, 0x35, 0xAF, 0xA5, 0x1D };
    uint8_t default_data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t default_trail[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x80, 0x69, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    // default mf1 info
    nfc_tag_mf1_information_t mf1_tmp_information;
    nfc_tag_mf1_information_t *p_mf1_information;
    p_mf1_information = &mf1_tmp_information;
    int block_max = get_block_max_by_tag_type(tag_type);
    for (int block = 0; block < block_max; block++) {
        if (block == 0) {
            memcpy(p_mf1_information->memory[block], default_blk0, sizeof(default_blk0));
        } else if ((block < 128 && (block & 3) == 3) || ((block & 15) == 15)) {
            memcpy(p_mf1_information->memory[block], default_trail, sizeof(default_trail));
        } else {
            memcpy(p_mf1_information->memory[block], default_data, sizeof(default_data));
        }
    }

    // default mf1 auto ant-collision res
    p_mf1_information->res_coll.atqa[0] = 0x04;
    p_mf1_information->res_coll.atqa[1] = 0x00;
    p_mf1_information->res_coll.sak[0] = 0x08;
    p_mf1_information->res_coll.uid[0] = 0xDE;
    p_mf1_information->res_coll.uid[1] = 0xAD;
    p_mf1_information->res_coll.uid[2] = 0xBE;
    p_mf1_information->res_coll.uid[3] = 0xEF;
    p_mf1_information->res_coll.size = NFC_TAG_14A_UID_SINGLE_SIZE;
    p_mf1_information->res_coll.ats.length = 0;

    // default mf1 config
    p_mf1_information->config.mode_gen1a_magic = false;
    p_mf1_information->config.mode_gen2_magic = false;
    p_mf1_information->config.use_mf1_coll_res = false;
    p_mf1_information->config.mode_block_write = NFC_TAG_MF1_WRITE_NORMAL;
    p_mf1_information->config.detection_enable = false;
    p_mf1_information->config.field_off_do_reset = false;

    // zero for reserved byte
    p_mf1_information->config.reserved1 = 0x00;
    p_mf1_information->config.reserved2 = 0x00;
    p_mf1_information->config.reserved3 = 0x00;

    // save data to flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int info_size = get_information_size_by_tag_type(tag_type);
    NRF_LOG_INFO("MF1 info size: %d", info_size);
    bool ret = fds_write_sync(map_info.id, map_info.key, info_size, p_mf1_information);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}

// Settling whether it enables detection
void nfc_tag_mf1_set_detection_enable(bool enable) {
    m_tag_information->config.detection_enable = enable;
}

// Whether it can be detected at present
bool nfc_tag_mf1_is_detection_enable(void) {
    return m_tag_information->config.detection_enable;
}

// Clear detection record
void nfc_tag_mf1_detection_log_clear(void) {
    m_auth_log.count = 0;
}

// The number of statistics of detection records
uint32_t nfc_tag_mf1_detection_log_count(void) {
    return m_auth_log.count;
}

// Set gen1a magic mode
void nfc_tag_mf1_set_gen1a_magic_mode(bool enable) {
    m_tag_information->config.mode_gen1a_magic = enable;
}

// Is in gen1a magic mode?
bool nfc_tag_mf1_is_gen1a_magic_mode(void) {
    return m_tag_information->config.mode_gen1a_magic;
}

// Set gen2 magic mode
void nfc_tag_mf1_set_gen2_magic_mode(bool enable) {
    m_tag_information->config.mode_gen2_magic = enable;
}

// Is in gen2 magic mode?
bool nfc_tag_mf1_is_gen2_magic_mode(void) {
    return m_tag_information->config.mode_gen2_magic;
}

// Set anti collision data from block 0
void nfc_tag_mf1_set_use_mf1_coll_res(bool enable) {
    m_tag_information->config.use_mf1_coll_res = enable;
}

// Get is anti collision data from block 0
bool nfc_tag_mf1_is_use_mf1_coll_res(void) {
    return m_tag_information->config.use_mf1_coll_res;
}

// Set write mode
void nfc_tag_mf1_set_write_mode(nfc_tag_mf1_write_mode_t write_mode) {
    if (write_mode == NFC_TAG_MF1_WRITE_SHADOW) {
        write_mode = NFC_TAG_MF1_WRITE_SHADOW_REQ;
    }
    m_tag_information->config.mode_block_write = write_mode;
}

// Get write mode
nfc_tag_mf1_write_mode_t nfc_tag_mf1_get_write_mode(void) {
    return m_tag_information->config.mode_block_write;
}

