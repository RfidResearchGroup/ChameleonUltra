/**
 * Copyright (c) 2012 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(BLE_DTM)
#include "ble_dtm.h"
#include "ble_dtm_hw.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "nrf.h"
#include "nrf_timer.h"
#include "nrf_radio.h"
#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
#include "nrf21540.h"
#endif

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
#if NRF21540_INTERRUPT_PRIORITY >= DTM_RADIO_IRQ_PRIORITY
#error "nRF21540 interrupt priority must be smaller than radio interrupt priority"
#endif
#endif // defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)

#if NRF_RADIO_ANTENNA_COUNT > DTM_MAX_ANTENNA_CNT
#error "Antena count must be smaller or equal 19" 
#endif

#if defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    #define DIRECTION_FINDING_SUPPORTED 1
#else
    #define DIRECTION_FINDING_SUPPORTED 0
#endif // defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)

#define DTM_HEADER_OFFSET         0                                                  /**< Index where the header of the pdu is located. */
#define DTM_HEADER_SIZE           2                                                  /**< Size of PDU header. */
#define DTM_HEADER_WITH_CTE_SIZE  3                                                  /**< Size of PDU header with CTEInfo field. */
#define DTM_HEADER_CTEINFO_OFFSET 2                                                  /**< CTEInfo field offset in payload. */
#define DTM_PAYLOAD_MAX_SIZE      255                                                /**< Maximum payload size allowed during dtm execution. */
#define DTM_LENGTH_OFFSET         (DTM_HEADER_OFFSET + 1)                            /**< Index where the length of the payload is encoded. */
#define DTM_PDU_MAX_MEMORY_SIZE   (DTM_HEADER_WITH_CTE_SIZE + DTM_PAYLOAD_MAX_SIZE)  /**< Maximum PDU size allowed during dtm execution. */
#define DTM_ON_AIR_OVERHEAD_SIZE  10                                                 /**< Size of the packet on air without the payload (preamble + sync word + type + RFU + length + CRC). */
#define DTM_CTE_REF_SAMPLE_CNT    8                                                  /**< CTE Reference period sample count. */

#define DTM_RESPONSE_EVENT_SHIFT 0x01                                                /**< Response event data shift. */
#define NRF_MAX_PAYLOAD_OCTETS   0x00FF                                              /**< Maximum number of payload octets that the local Controller supports for transmission of a single Link Layer Data Physical Channel PDU. */

#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
#define NRF_MAX_RX_TX_TIME       0x2148                                    /**< Maximum transmit or receive time, in microseconds, that the local Controller supports for transmission of a single Link Layer Data Physical Channel PDU, divided by 2. */
#else
#define NRF_MAX_RX_TX_TIME       0x424
#endif // defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)

#define NRF_CTE_MAX_LENGTH      0x14                                       /**< Maximum length of the Constant Tone Extension that the local Controller supports for transmission in a Link Layer packet, in 8 us units. */
#define NRF_CTE_TIME_IN_US      0x08                                       /**< CTE time unit in us. CTE length is expressed in 8us unit. */

#define RX_MODE          true   /**< Constant defining RX mode for radio during dtm test. */
#define TX_MODE          false  /**< Constant defining TX mode for radio during dtm test. */

#define PHYS_CH_MAX      39     /**< Maximum number of valid channels in BLE. */

// Values that for now are "constants" - they could be configured by a function setting them,
// but most of these are set by the BLE DTM standard, so changing them is not relevant.
#define RFPHY_TEST_0X0F_REF_PATTERN  0x0f  /**<  RF-PHY test packet patterns, for the repeated octet packets. */
#define RFPHY_TEST_0X55_REF_PATTERN  0x55  /**<  RF-PHY test packet patterns, for the repeated octet packets. */
#define RFPHY_TEST_0XFF_REF_PATTERN  0xFF  /**<  RF-PHY test packet patterns, for the repeated octet packets. */

#define PRBS9_CONTENT  {0xFF, 0xC1, 0xFB, 0xE8, 0x4C, 0x90, 0x72, 0x8B,   \
                        0xE7, 0xB3, 0x51, 0x89, 0x63, 0xAB, 0x23, 0x23,   \
                        0x02, 0x84, 0x18, 0x72, 0xAA, 0x61, 0x2F, 0x3B,   \
                        0x51, 0xA8, 0xE5, 0x37, 0x49, 0xFB, 0xC9, 0xCA,   \
                        0x0C, 0x18, 0x53, 0x2C, 0xFD, 0x45, 0xE3, 0x9A,   \
                        0xE6, 0xF1, 0x5D, 0xB0, 0xB6, 0x1B, 0xB4, 0xBE,   \
                        0x2A, 0x50, 0xEA, 0xE9, 0x0E, 0x9C, 0x4B, 0x5E,   \
                        0x57, 0x24, 0xCC, 0xA1, 0xB7, 0x59, 0xB8, 0x87,   \
                        0xFF, 0xE0, 0x7D, 0x74, 0x26, 0x48, 0xB9, 0xC5,   \
                        0xF3, 0xD9, 0xA8, 0xC4, 0xB1, 0xD5, 0x91, 0x11,   \
                        0x01, 0x42, 0x0C, 0x39, 0xD5, 0xB0, 0x97, 0x9D,   \
                        0x28, 0xD4, 0xF2, 0x9B, 0xA4, 0xFD, 0x64, 0x65,   \
                        0x06, 0x8C, 0x29, 0x96, 0xFE, 0xA2, 0x71, 0x4D,   \
                        0xF3, 0xF8, 0x2E, 0x58, 0xDB, 0x0D, 0x5A, 0x5F,   \
                        0x15, 0x28, 0xF5, 0x74, 0x07, 0xCE, 0x25, 0xAF,   \
                        0x2B, 0x12, 0xE6, 0xD0, 0xDB, 0x2C, 0xDC, 0xC3,   \
                        0x7F, 0xF0, 0x3E, 0x3A, 0x13, 0xA4, 0xDC, 0xE2,   \
                        0xF9, 0x6C, 0x54, 0xE2, 0xD8, 0xEA, 0xC8, 0x88,   \
                        0x00, 0x21, 0x86, 0x9C, 0x6A, 0xD8, 0xCB, 0x4E,   \
                        0x14, 0x6A, 0xF9, 0x4D, 0xD2, 0x7E, 0xB2, 0x32,   \
                        0x03, 0xC6, 0x14, 0x4B, 0x7F, 0xD1, 0xB8, 0xA6,   \
                        0x79, 0x7C, 0x17, 0xAC, 0xED, 0x06, 0xAD, 0xAF,   \
                        0x0A, 0x94, 0x7A, 0xBA, 0x03, 0xE7, 0x92, 0xD7,   \
                        0x15, 0x09, 0x73, 0xE8, 0x6D, 0x16, 0xEE, 0xE1,   \
                        0x3F, 0x78, 0x1F, 0x9D, 0x09, 0x52, 0x6E, 0xF1,   \
                        0x7C, 0x36, 0x2A, 0x71, 0x6C, 0x75, 0x64, 0x44,   \
                        0x80, 0x10, 0x43, 0x4E, 0x35, 0xEC, 0x65, 0x27,   \
                        0x0A, 0xB5, 0xFC, 0x26, 0x69, 0x3F, 0x59, 0x99,   \
                        0x01, 0x63, 0x8A, 0xA5, 0xBF, 0x68, 0x5C, 0xD3,   \
                        0x3C, 0xBE, 0x0B, 0xD6, 0x76, 0x83, 0xD6, 0x57,   \
                        0x05, 0x4A, 0x3D, 0xDD, 0x81, 0x73, 0xC9, 0xEB,   \
                        0x8A, 0x84, 0x39, 0xF4, 0x36, 0x0B, 0xF7}           /**< The PRBS9 sequence used as packet payload.
                                                                                 The bytes in the sequence is in the right order, but the bits of each byte in the array is reverse.
                                                                                 of that found by running the PRBS9 algorithm. This is because of the endianess of the nRF5 radio. */

#if defined(NRF52840_XXAA)
#define DTM_SUPPORTED_FEATURE (DTM_LE_DATA_PACKET_LEN_EXTENSION | DTM_LE_2M_PHY | DTM_LE_CODED_PHY)
#elif  defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
#define DTM_SUPPORTED_FEATURE (DTM_LE_DATA_PACKET_LEN_EXTENSION | DTM_LE_2M_PHY | DTM_LE_CODED_PHY | \
                               DTM_LE_CONSTANT_TONE_EXTENSION | DTM_LE_ANTENNA_SWITCH |              \
                               DTM_LE_AOD_1US_TANSMISSION | DTM_LE_AOD_1US_RECEPTION |               \
                               DTM_LE_AOA_1US_RECEPTION)
#else
#define DTM_SUPPORTED_FEATURE (DTM_LE_DATA_PACKET_LEN_EXTENSION | DTM_LE_2M_PHY)
#endif


/**@brief Structure holding the PDU used for transmitting/receiving a PDU.
 */
typedef struct
{
    uint8_t content[DTM_HEADER_WITH_CTE_SIZE + DTM_PAYLOAD_MAX_SIZE];                 /**< PDU packet content. */
} pdu_type_t;

/**@brief States used for the DTM test implementation.
 */
typedef enum
{
    STATE_UNINITIALIZED,                                                     /**< The DTM is uninitialized. */
    STATE_IDLE,                                                              /**< State when system has just initialized, or current test has completed. */
    STATE_TRANSMITTER_TEST,                                                  /**< State used when a DTM Transmission test is running. */
    STATE_CARRIER_TEST,                                                      /**< State used when a DTM Carrier test is running (Vendor specific test). */
    STATE_RECEIVER_TEST                                                      /**< State used when a DTM Receive test is running. */
} state_t;

/**@brief Constant Tone Extension mode.
 */
typedef enum
{
    CTE_MODE_OFF  = 0x00,                                           /**< Do not use the Constant Tone Extension. */
    CTE_MODE_AOD  = 0x02,                                           /**< Constant Tone Extension: Use Angle-of-Departure. */
    CTE_MODE_AOA  = 0x03                                            /**< Constant Tone Extension: Use Angle-of-Arrival. */
} cte_mode_t;

/** Constatnt Tone Extension slot.
 */
typedef enum
{
    CTE_SLOT_2US = 0x01,                                            /**< Constant Tone Extension: Sample with 1 us slot. */
    CTE_SLOT_1US = 0x02,                                            /**< Constant Tone Extension: Sample with 2 us slot. */
} cte_slot_t;

/** Constatnt Tone Extension antenna switch pattern.
 */
typedef enum
{
    ANTENNA_PATTERN_123N123N = 0x00,                               /**< Constant Tone Extension: Antenna switch pattern 1, 2, 3 ...N. */
    ANTENNA_PATTERN_123N2123 = 0x01                                /**< Constant Tone Extension: Antenna switch pattern 1, 2, 3 ...N, N - 1, N - 2, ..., 1, ... */
} antenna_pattern_t; 


// Internal variables set as side effects of commands or events.
static state_t            m_state = STATE_UNINITIALIZED;                      /**< Current machine state. */
static uint16_t           m_rx_pkt_count;                                     /**< Number of valid packets received. */
static pdu_type_t         m_pdu[2];                                           /**< Radio PDU buffers to be sent. */
static pdu_type_t       * mp_current_pdu = m_pdu;                             /**< Radio PDU current buffer. */
static uint16_t           m_event;                                            /**< current command status - initially "ok", may be set if error detected, or to packet count. */
static bool               m_new_event;                                        /**< Command has been processed - number of not yet reported event bytes. */
static uint32_t           m_packet_length;                                    /**< Payload length of transmitted PDU, bits 2:7 of 16-bit dtm command. */
static dtm_pkt_type_t     m_packet_type;                                      /**< Bits 0..1 of 16-bit transmit command, or 0xFFFFFFFF. */
static dtm_freq_t         m_phys_ch;                                          /**< 0..39 physical channel number (base 2402 MHz, Interval 2 MHz), bits 8:13 of 16-bit dtm command. */
static uint32_t           m_current_time = 0;                                 /**< Counter for interrupts from timer to ensure that the 2 bytes forming a DTM command are received within the time window. */

#if DIRECTION_FINDING_SUPPORTED
static cte_mode_t         m_cte_mode = CTE_MODE_OFF;                          /**< Constant Tone Extension mode. */
static cte_slot_t         m_cte_slot = CTE_SLOT_2US;                          /**< Constant Tone Extension sample slot */
static uint8_t            m_cte_time = 0;                                     /**< Constant Tone Extension length in 8us unit. */
static antenna_pattern_t  m_antenna_pattern = ANTENNA_PATTERN_123N123N;       /**< Antenna switch pattern. */
static uint8_t            m_antenna_number = 0;                               /**< Number of antenna in the antenna array. */
static uint8_t            m_cte_info = 0;                                     /**< CTEInfo. */
#endif // DIRECTION_FINDING_SUPPORTED

// Nordic specific configuration values (not defined by BLE standard).
// Definition of initial values found in ble_dtm.h
static uint32_t          m_tx_power          = DEFAULT_TX_POWER;             /**< TX power for transmission test, default 0 dBm. */
static NRF_TIMER_Type *  mp_timer            = DTM_TIMER;                    /**< Timer to be used. */
static IRQn_Type         m_timer_irq         = DTM_TIMER_IRQn;               /**< Interrupt used by the DTM Timer. */

static uint8_t const     m_prbs_content[]    = PRBS9_CONTENT;                /**< Pseudo-random bit sequence defined by the BLE standard. */
static uint8_t           m_packetHeaderLFlen = 8;                            /**< Length of length field in packet Header (in bits). */
static uint8_t           m_packetHeaderS0len = 1;                            /**< Length of S0 field in packet Header (in bytes). */
static uint8_t           m_packetHeaderS1len = 0;                            /**< Length of S1 field in packet Header (in bits). */
static uint8_t           m_packetHeaderPlen  = RADIO_PCNF0_PLEN_8bit;        /**< Length of the preamble. */

static uint8_t           m_crcConfSkipAddr   = 1;                            /**< Leave packet address field out of CRC calculation. */
static uint8_t           m_static_length     = 0;                            /**< Number of bytes sent in addition to the var.length payload. */
static uint32_t          m_balen             = 3;                            /**< Base address length in bytes. */
static uint32_t          m_endian            = RADIO_PCNF1_ENDIAN_Little;    /**< On air endianess of packet, this applies to the S0, LENGTH, S1 and the PAYLOAD fields. */
static uint32_t          m_whitening         = RADIO_PCNF1_WHITEEN_Disabled; /**< Whitening disabled. */
static uint8_t           m_crcLength         = RADIO_CRCCNF_LEN_Three;       /**< CRC Length (in bytes). */
static uint32_t          m_address           = 0x71764129;                   /**< Address. */
static uint32_t          m_crc_poly          = 0x0000065B;                   /**< CRC polynomial. */
static uint32_t          m_crc_init          = 0x00555555;                   /**< Initial value for CRC calculation. */
static uint8_t           m_radio_mode        = RADIO_MODE_MODE_Ble_1Mbit;    /**< nRF51 specific radio mode value. */
static uint32_t          m_txIntervaluS      = 2500;                         /**< Time between start of Tx packets (in uS). */

// The variables and defines below are related to the workaround for nRF52840 anomaly 172
static bool              anomaly_172_wa_enabled = false;                         /**< Enable or disable the workaround for Errata 172. */
static uint8_t           m_strict_mode          = 0;                             /**< Enable or disable strict mode to workaround Errata 172. */
#define BLOCKER_FIX_RSSI_THRESHOLD              95                               /**< The RSSI threshold at which to toggle strict mode. */
#define BLOCKER_FIX_WAIT_DEFAULT                1250                             /**< 1250 * 8 = 10000 us = 10 ms. */
#define BLOCKER_FIX_WAIT_END                    63                               /**< 63 * 8 = ~500us. */
#define BLOCKER_FIX_CNTDETECTTHR                15                               /**< Threshold used to determine necessary strict mode status changes. */
#define BLOCKER_FIX_CNTADDRTHR                  2                                /**< Threshold used to determine necessary strict mode status changes. */

const uint32_t nrf_power_value[] = {
                            RADIO_TXPOWER_TXPOWER_Neg40dBm,
                            RADIO_TXPOWER_TXPOWER_Neg30dBm,
                            RADIO_TXPOWER_TXPOWER_Neg20dBm,
                            RADIO_TXPOWER_TXPOWER_Neg16dBm,
                            RADIO_TXPOWER_TXPOWER_Neg12dBm,
                            RADIO_TXPOWER_TXPOWER_Neg8dBm,
                            RADIO_TXPOWER_TXPOWER_Neg4dBm,
                            RADIO_TXPOWER_TXPOWER_0dBm,
#if defined(RADIO_TXPOWER_TXPOWER_Pos2dBm)
                            RADIO_TXPOWER_TXPOWER_Pos2dBm,
#endif // defined(RADIO_TXPOWER_TXPOWER_Pos2dBm)
                            RADIO_TXPOWER_TXPOWER_Pos3dBm,
                            RADIO_TXPOWER_TXPOWER_Pos4dBm,
#if defined(RADIO_TXPOWER_TXPOWER_Pos5dBm)
                            RADIO_TXPOWER_TXPOWER_Pos5dBm,
#endif // defined(RADIO_TXPOWER_TXPOWER_Pos5dBm)
#if defined(RADIO_TXPOWER_TXPOWER_Pos6dBm)
                            RADIO_TXPOWER_TXPOWER_Pos6dBm,
#endif // defined(RADIO_TXPOWER_TXPOWER_Pos6dBm)
#if defined(RADIO_TXPOWER_TXPOWER_Pos7dBm)
                            RADIO_TXPOWER_TXPOWER_Pos7dBm,
#endif // defined(RADIO_TXPOWER_TXPOWER_Pos7dBm)
#if defined(RADIO_TXPOWER_TXPOWER_Pos8dBm)
                            RADIO_TXPOWER_TXPOWER_Pos8dBm
#endif // defined(RADIO_TXPOWER_TXPOWER_Pos8dBm) 
};

#if DIRECTION_FINDING_SUPPORTED

/**@brief Antenna pin array.
 */
static const uint32_t m_antenna_pin[] = {
    NRF_RADIO_ANTENNA_PIN_1,
    NRF_RADIO_ANTENNA_PIN_2,
    NRF_RADIO_ANTENNA_PIN_3,
    NRF_RADIO_ANTENNA_PIN_4,
    NRF_RADIO_ANTENNA_PIN_5,
    NRF_RADIO_ANTENNA_PIN_6,
    NRF_RADIO_ANTENNA_PIN_7,
    NRF_RADIO_ANTENNA_PIN_8
};

/**@brief Received CTE IQ sample data.
*/
static uint32_t m_cte_data[128];

static void radio_gpio_pattern_clear(void)
{
    NRF_RADIO->CLEARPATTERN = 1;
}

static void antenna_radio_pin_config(void)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(m_antenna_pin); i++)
    {
        NRF_RADIO->PSEL.DFEGPIO[i] = m_antenna_pin[i];
    }
}

static void switch_pattern_set(void)
{
    // Set antenna for the guard period and for the reference period.
    NRF_RADIO->SWITCHPATTERN = 1;
    NRF_RADIO->SWITCHPATTERN = 1;

    switch (m_antenna_pattern)
    {
        case ANTENNA_PATTERN_123N123N:
            for (uint16_t i = 1; i <= m_antenna_number; i++)
            {
                NRF_RADIO->SWITCHPATTERN = i;
            }

            break;
        
        case ANTENNA_PATTERN_123N2123:
            for (uint16_t i = 1; i <= m_antenna_number; i++)
            {
                NRF_RADIO->SWITCHPATTERN = i;
            }

            for (uint16_t i = m_antenna_number - 1; i > 0; i--)
            {
                NRF_RADIO->SWITCHPATTERN = i;
            }

            break;
    }
}


static void radio_cte_prepare(bool rx)
{
    if ((rx && (m_cte_mode ==  CTE_MODE_AOA)) || ((!rx) && (m_cte_mode == CTE_MODE_AOD)))
    {
        antenna_radio_pin_config();
        switch_pattern_set();

        // Set antenna switch spacing.
        NRF_RADIO->DFECTRL1 &= ~ RADIO_DFECTRL1_TSWITCHSPACING_Msk;
        NRF_RADIO->DFECTRL1 |= (m_cte_slot << RADIO_DFECTRL1_TSWITCHSPACING_Pos);
    }

    NRF_RADIO->DFEMODE = m_cte_mode;
    NRF_RADIO->PCNF0  |= (8 << RADIO_PCNF0_S1LEN_Pos);

    if (rx)
    {
        // Enable parsing CTEInfo from received packet.
        NRF_RADIO->CTEINLINECONF |= RADIO_CTEINLINECONF_CTEINLINECTRLEN_Enabled;
        NRF_RADIO->CTEINLINECONF |= (RADIO_CTEINLINECONF_CTEINFOINS1_InS1 << RADIO_CTEINLINECONF_CTEINFOINS1_Pos);

        // Set S0 Mask and Configuration to check if CP bit is set in received PDU.
        NRF_RADIO->CTEINLINECONF |= (0x20 << RADIO_CTEINLINECONF_S0CONF_Pos) |
                                    (0x20 << RADIO_CTEINLINECONF_S0MASK_Pos);

        NRF_RADIO->DFEPACKET.PTR    = (uint32_t)m_cte_data;
        NRF_RADIO->DFEPACKET.MAXCNT = (uint16_t)sizeof(m_cte_data);
    }
    else
    {
        // Disable parsing CTEInfo from received packet.
        NRF_RADIO->CTEINLINECONF &= ~RADIO_CTEINLINECONF_CTEINLINECTRLEN_Enabled;

        NRF_RADIO->DFECTRL1 &= ~RADIO_DFECTRL1_NUMBEROF8US_Msk;
        NRF_RADIO->DFECTRL1 |= m_cte_time;
    }
}

#endif // DIRECTION_FINDING_SUPPORTED

/**@brief Function for verifying that a received PDU has the expected structure and content.
 * 
 * @param[in] pdu Pointer to radio pdu.
 */
static bool check_pdu(const pdu_type_t *pdu)
{
    uint8_t        k;                // Byte pointer for running through PDU payload
    uint8_t        pattern;          // Repeating octet value in payload
    dtm_pkt_type_t pdu_packet_type;  // Note: PDU packet type is a 4-bit field in HCI, but 2 bits in BLE DTM
    uint32_t       length = 0;
    uint8_t        header_len;

    pdu_packet_type = (dtm_pkt_type_t)(pdu->content[DTM_HEADER_OFFSET] & 0x0F);
    length          = pdu->content[DTM_LENGTH_OFFSET];

#if DIRECTION_FINDING_SUPPORTED
    header_len = (m_cte_mode != CTE_MODE_OFF) ? DTM_HEADER_WITH_CTE_SIZE : DTM_HEADER_SIZE;
#else
    header_len = DTM_HEADER_SIZE;
#endif // DIRECTION_FINDING_SUPPORTED

    // Check that the length is valid.
    if (length > DTM_PAYLOAD_MAX_SIZE)
    {
        return false;
    }

    // If the 1Mbit or 2Mbit radio mode is active, check that one of the three valid uncoded DTM packet types are selected.
    if ((m_radio_mode == RADIO_MODE_MODE_Ble_1Mbit || m_radio_mode == RADIO_MODE_MODE_Ble_2Mbit) && (pdu_packet_type > (dtm_pkt_type_t)DTM_PKT_0X55))
    {
        return false;
    }

#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    // If a long range radio mode is active, check that one of the four valid coded DTM packet types are selected.
    if ((m_radio_mode == RADIO_MODE_MODE_Ble_LR500Kbit || m_radio_mode == RADIO_MODE_MODE_Ble_LR125Kbit) && (pdu_packet_type > (dtm_pkt_type_t)DTM_PKT_0XFF))
    {
        return false;
    }
#endif //defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)

    if (pdu_packet_type == DTM_PKT_PRBS9)
    {
        // Payload does not consist of one repeated octet; must compare ir with entire block into
        return (memcmp(pdu->content + header_len, m_prbs_content, length) == 0);
    }

    if (pdu_packet_type == DTM_PKT_0X0F)
    {
        pattern = RFPHY_TEST_0X0F_REF_PATTERN;
    }
    else if (pdu_packet_type == DTM_PKT_0X55)
    {
        pattern = RFPHY_TEST_0X55_REF_PATTERN;
    }
    else if (pdu_packet_type == DTM_PKT_0XFF)
    {
        pattern = RFPHY_TEST_0XFF_REF_PATTERN;
    }
    else
    {
        // No valid packet type set.
        return false;
    }

    for (k = 0; k < length; k++)
    {
        // Check repeated pattern filling the PDU payload
        if (pdu->content[k + header_len] != pattern)
        {
            return false;
        }
    }

#if DIRECTION_FINDING_SUPPORTED
    // Check CTEInfo and IQ sample cnt
    if (m_cte_mode != CTE_MODE_OFF)
    {
        uint8_t cte_info;
        uint8_t cte_sample_cnt;
        uint8_t expected_sample_cnt;

        cte_info = pdu->content[DTM_HEADER_CTEINFO_OFFSET];

        expected_sample_cnt = DTM_CTE_REF_SAMPLE_CNT + ((m_cte_time * 8)) /
                              ((m_cte_slot == CTE_SLOT_1US) ? 2 : 4);
        cte_sample_cnt = NRF_RADIO->DFEPACKET.AMOUNT;

        memset(m_cte_data, 0, sizeof(m_cte_data));

        if ((cte_info != m_cte_info) || (expected_sample_cnt != cte_sample_cnt))
        {
            return false;
        }
    }
#endif // DIRECTION_FINDING_SUPPORTED
    return true;
}


/**@brief Function for turning off the radio after a test.
 *        Also called after test done, to be ready for next test.
 */
static void radio_reset(void)
{
#if !defined(NRF21540_DRIVER_ENABLE) || (NRF21540_DRIVER_ENABLE == 0)
    NRF_PPI->CHENCLR = PPI_CHENCLR_CH0_Msk | PPI_CHENCLR_CH1_Msk;


    NRF_RADIO->SHORTS          = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE   = 1;

    while (NRF_RADIO->EVENTS_DISABLED == 0)
    {
        // Do nothing
    }

    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_RXEN      = 0;
    NRF_RADIO->TASKS_TXEN      = 0;
#endif

    NVIC_DisableIRQ(RADIO_IRQn);
    nrf_radio_int_disable(NRF_RADIO_INT_READY_MASK |
                          NRF_RADIO_INT_ADDRESS_MASK |
                          NRF_RADIO_INT_END_MASK);

    m_rx_pkt_count = 0;

    NRF_RADIO->PCNF0 &= ~RADIO_PCNF0_S1LEN_Msk;
}


/**@brief Function for initializing the radio for DTM.
 */
static uint32_t radio_init(void)
{
    if (dtm_radio_validate(m_tx_power, m_radio_mode) != DTM_SUCCESS)
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    // Turn off radio before configuring it
    radio_reset();

    NRF_RADIO->TXPOWER = m_tx_power;
    NRF_RADIO->MODE    = m_radio_mode << RADIO_MODE_MODE_Pos;

    // Set the access address, address0/prefix0 used for both Rx and Tx address
    NRF_RADIO->PREFIX0    &= ~RADIO_PREFIX0_AP0_Msk;
    NRF_RADIO->PREFIX0    |= (m_address >> 24) & RADIO_PREFIX0_AP0_Msk;
    NRF_RADIO->BASE0       = m_address << 8;
    NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos;
    NRF_RADIO->TXADDRESS   = (0x00 << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;

    // Configure CRC calculation
    NRF_RADIO->CRCCNF = (m_crcConfSkipAddr << RADIO_CRCCNF_SKIP_ADDR_Pos) |
                        (m_crcLength << RADIO_CRCCNF_LEN_Pos);

    if (m_radio_mode == RADIO_MODE_MODE_Ble_1Mbit || m_radio_mode == RADIO_MODE_MODE_Ble_2Mbit)
    {
        // Non-coded PHY
        NRF_RADIO->PCNF0 = (m_packetHeaderS1len << RADIO_PCNF0_S1LEN_Pos) |
                           (m_packetHeaderS0len << RADIO_PCNF0_S0LEN_Pos) |
                           (m_packetHeaderLFlen << RADIO_PCNF0_LFLEN_Pos) |
                           (m_packetHeaderPlen << RADIO_PCNF0_PLEN_Pos);
    }
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    else
    {
        // Coded PHY (Long range)
        NRF_RADIO->PCNF0 = (m_packetHeaderS1len << RADIO_PCNF0_S1LEN_Pos) |
                       (m_packetHeaderS0len << RADIO_PCNF0_S0LEN_Pos) |
                       (m_packetHeaderLFlen << RADIO_PCNF0_LFLEN_Pos) |
                       (3 << RADIO_PCNF0_TERMLEN_Pos) |
                       (2 << RADIO_PCNF0_CILEN_Pos) |
                       (m_packetHeaderPlen << RADIO_PCNF0_PLEN_Pos);
    }
#endif //defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)

    NRF_RADIO->PCNF1 = (m_whitening          << RADIO_PCNF1_WHITEEN_Pos) |
                       (m_endian             << RADIO_PCNF1_ENDIAN_Pos)  |
                       (m_balen              << RADIO_PCNF1_BALEN_Pos)   |
                       (m_static_length      << RADIO_PCNF1_STATLEN_Pos) |
                       (DTM_PAYLOAD_MAX_SIZE << RADIO_PCNF1_MAXLEN_Pos);

    return DTM_SUCCESS;
}


// Strict mode setting will be used only by devices affected by nRF52840 anomaly 172
void set_strict_mode (bool enable)
{
   uint8_t dbcCorrTh;
   uint8_t dsssMinPeakCount;
   if (enable == true)
   {
      dbcCorrTh = 0x7d;
      dsssMinPeakCount = 6;
      *(volatile uint32_t *) 0x4000173c = ((*((volatile uint32_t *) 0x4000173c)) & 0x7FFFFF00) | 0x80000000 | (((uint32_t)(dbcCorrTh)) << 0);
      *(volatile uint32_t *) 0x4000177c = ((*((volatile uint32_t *) 0x4000177c)) & 0x7FFFFF8F) | 0x80000000 | ((((uint32_t)dsssMinPeakCount) & 0x00000007) << 4);
   }
   else
   {
      *(volatile uint32_t *) 0x4000173c = 0x40003034;
      *(volatile uint32_t *) 0x4000177c = ((*((volatile uint32_t *) 0x4000177c)) & 0x7FFFFFFF); // Unset override of dsssMinPeakCount
   }

   m_strict_mode = enable;
}


// Radio configuration used as a workaround for nRF52840 anomaly 172
void anomaly_172_radio_operation(void)
{
    *(volatile uint32_t *) 0x40001040 = 1;
    *(volatile uint32_t *) 0x40001038 = 1;
}


// Function to gather RSSI data and set strict mode accordingly. Used as part of the workaround for nRF52840 anomaly 172
uint8_t anomaly_172_rssi_check(void)
{
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->TASKS_RSSISTART = 1;
    while (NRF_RADIO->EVENTS_RSSIEND == 0);
    uint8_t rssi = NRF_RADIO->RSSISAMPLE;
    return rssi;
}

/**@brief Function for swapping the pdu buffer for radio rx operation.
 * 
 * @retval Pointer to received data in the last rx operation.
 */
static pdu_type_t *radio_buffer_swap(void)
{
    pdu_type_t *received_pdu = mp_current_pdu;
    uint32_t packet_index = (mp_current_pdu == m_pdu);

    mp_current_pdu = &m_pdu[packet_index];

    NRF_RADIO->PACKETPTR = (uint32_t)mp_current_pdu;

    return received_pdu;
}

/**@brief Function for preparing the radio. At start of each test: Turn off RF, clear interrupt flags of RF, initialize the radio
 *        at given RF channel.
 *
 *@param[in] rx     boolean indicating if radio should be prepared in rx mode (true) or tx mode.
 */
static void radio_prepare(bool rx)
{
    dtm_turn_off_test();

#if DIRECTION_FINDING_SUPPORTED
    if (m_cte_mode != CTE_MODE_OFF)
    {
        radio_cte_prepare(rx);
    }
#endif // DIRECTION_FINDING_SUPPORTED

    NRF_RADIO->CRCPOLY      = m_crc_poly;
    NRF_RADIO->CRCINIT      = m_crc_init;
    NRF_RADIO->FREQUENCY    = (m_phys_ch << 1) + 2;                  // Actual frequency (MHz): 2400 + register value
    NRF_RADIO->PACKETPTR    = (uint32_t)mp_current_pdu;              // Setting packet pointer will start the radio
    NRF_RADIO->EVENTS_READY = 0;

#if !defined(NRF21540_DRIVER_ENABLE) || (NRF21540_DRIVER_ENABLE == 0)
    NRF_RADIO->SHORTS       = (1 << RADIO_SHORTS_READY_START_Pos);   // Shortcut between READY event and START task
#if DIRECTION_FINDING_SUPPORTED
    if (m_cte_mode != CTE_MODE_OFF)
    {
        NRF_RADIO->SHORTS |= (1 << RADIO_SHORTS_PHYEND_DISABLE_Pos);  // Shortcut between PHY_END event and DISABLE task
    }
    else
    {
        NRF_RADIO->SHORTS |= (1 << RADIO_SHORTS_END_DISABLE_Pos);     // Shortcut between END event and DISABLE task
    }
#else
     NRF_RADIO->SHORTS |= (1 << RADIO_SHORTS_END_DISABLE_Pos);     // Shortcut between END event and DISABLE task
#endif // DIRECTION_FINDING_SUPPORTED
#endif // !defined(NRF21540_DRIVER_ENABLE) || (NRF21540_DRIVER_ENABLE == 0)

    // Enable radio interrupts
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);

    nrf_radio_int_enable(NRF_RADIO_INT_READY_MASK |
                         NRF_RADIO_INT_ADDRESS_MASK |
                         NRF_RADIO_INT_END_MASK);

    if (rx)
    {
        // Enable strict mode if running on a device affected by nRF52840 anomaly 172

        if (anomaly_172_wa_enabled)
        {
            set_strict_mode(1);
        }

        NRF_RADIO->EVENTS_END = 0;
#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        (void)nrf21540_rx_set(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_NON_BLOCKING);
#else
        NRF_RADIO->TASKS_RXEN = 1;  // shorts will start radio in RX mode when it is ready
#endif
    }
    else // tx
    {
        NRF_RADIO->TXPOWER = m_tx_power & RADIO_TXPOWER_TXPOWER_Msk;

        // Stop the timer used by nRF52840 anomaly 172 if running on an affected device.
        if (anomaly_172_wa_enabled)
        {
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_CLEAR);
            nrf_timer_event_clear(ANOMALY_172_TIMER,
                                  nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
            nrf_timer_event_clear(ANOMALY_172_TIMER,
                                  nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));
        }
    }
}


/**@brief Function for terminating the ongoing test (if any) and closing down the radio.
 */
static void dtm_test_done(void)
{
    dtm_turn_off_test();
#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
    (void)nrf21540_power_down(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_BLOCKING);
#else
    NRF_PPI->CHENCLR = 0x01;
    NRF_PPI->CH[0].EEP = 0;     // Break connection from timer to radio to stop transmit loop
    NRF_PPI->CH[0].TEP = 0;
#endif
    nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);

    radio_reset();
    m_state = STATE_IDLE;
}


/**@brief Function for configuring the timer for 625us cycle time.
 */
static uint32_t timer_init(void)
{
    // Use 16MHz from external crystal
    // This could be customized for RC/Xtal, or even to use a 32 kHz crystal
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART    = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0)
    {
        // Do nothing while waiting for the clock to start
    }

    nrf_timer_task_trigger(mp_timer, NRF_TIMER_TASK_STOP);            // Stop timer, if it was running
    nrf_timer_task_trigger(mp_timer, NRF_TIMER_TASK_CLEAR);
    nrf_timer_mode_set(mp_timer, NRF_TIMER_MODE_TIMER);               // Timer mode (not counter)

    nrf_timer_event_clear(mp_timer,                                   // clean up possible old events
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
    nrf_timer_event_clear(mp_timer,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));
    nrf_timer_event_clear(mp_timer,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL2));
    nrf_timer_event_clear(mp_timer,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL3));
    
    nrf_timer_frequency_set(mp_timer, NRF_TIMER_FREQ_1MHz);                 // Input clock is 16MHz, timer clock = 2 ^ prescale -> interval 1us

    nrf_timer_shorts_enable(mp_timer, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK); // Clear the count every time timer reaches the CCREG0 count
    nrf_timer_int_enable(mp_timer, NRF_TIMER_INT_COMPARE0_MASK);

    nrf_timer_cc_write(mp_timer, NRF_TIMER_CC_CHANNEL0, m_txIntervaluS);    // 625uS with 1MHz clock to the timer
    nrf_timer_cc_write(mp_timer, NRF_TIMER_CC_CHANNEL1, UART_POLL_CYCLE);   // Depends on the baud rate of the UART. Default baud rate of 19200 will result in a 260uS time with 1MHz clock to the timer

    NVIC_ClearPendingIRQ(m_timer_irq);
    NVIC_SetPriority(m_timer_irq, DTM_TIMER_IRQ_PRIORITY);
    NVIC_EnableIRQ(m_timer_irq);

    nrf_timer_task_trigger(mp_timer, NRF_TIMER_TASK_START);                 // Start the timer - it will be running continuously                                 

    m_current_time = 0;


    // Enable the timer used by nRF52840 anomaly 172 if running on an affected device.
    nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);            // Stop timer, if it was running
    nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_CLEAR);
    nrf_timer_mode_set(ANOMALY_172_TIMER, NRF_TIMER_MODE_TIMER);               // Timer mode (not counter)
    nrf_timer_event_clear(ANOMALY_172_TIMER,                                   // clean up possible old events
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
    nrf_timer_event_clear(ANOMALY_172_TIMER,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));
    nrf_timer_event_clear(ANOMALY_172_TIMER,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL2));
    nrf_timer_event_clear(ANOMALY_172_TIMER,
                          nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL3));

    nrf_timer_cc_write(ANOMALY_172_TIMER, NRF_TIMER_CC_CHANNEL0, BLOCKER_FIX_WAIT_DEFAULT);
    nrf_timer_cc_write(ANOMALY_172_TIMER, NRF_TIMER_CC_CHANNEL1, 0);


    nrf_timer_frequency_set(ANOMALY_172_TIMER, NRF_TIMER_FREQ_125kHz); // Input clock is 16MHz, timer clock = 2 ^ prescale -> interval 1us

    NVIC_ClearPendingIRQ(ANOMALY_172_TIMER_IRQn);
    NVIC_SetPriority(ANOMALY_172_TIMER_IRQn, DTM_ANOMALY_172_TIMER_IRQ_PRIORITY);
    NVIC_EnableIRQ(ANOMALY_172_TIMER_IRQn);

    nrf_timer_int_enable(ANOMALY_172_TIMER,
                         NRF_TIMER_INT_COMPARE0_MASK);

    return DTM_SUCCESS;
}


/**@brief Function for handling vendor specific commands.
 *        Used when packet type is set to Vendor specific.
 *        The length field is used for encoding vendor specific command.
 *        The frequency field is used for encoding vendor specific options to the command.
 *
 * @param[in]   vendor_cmd      Vendor specific command to be executed.
 * @param[in]   vendor_option   Vendor specific option to the vendor command.
 *
 * @return      DTM_SUCCESS or one of the DTM_ERROR_ values
 */
static uint32_t dtm_vendor_specific_pkt(uint32_t vendor_cmd, dtm_freq_t vendor_option)
{
    switch (vendor_cmd)
    {
        // nRFgo Studio uses CARRIER_TEST_STUDIO to indicate a continuous carrier without
        // a modulated signal.
        case CARRIER_TEST:
        case CARRIER_TEST_STUDIO:
            // Not a packet type, but used to indicate that a continuous carrier signal
            // should be transmitted by the radio.
            radio_prepare(TX_MODE);

            dtm_constant_carrier();

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
            (void)nrf21540_tx_set(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_NON_BLOCKING);
#else
            // Shortcut between READY event and START task
            NRF_RADIO->SHORTS = 1 << RADIO_SHORTS_READY_START_Pos;

            // Shortcut will start radio in Tx mode when it is ready
            NRF_RADIO->TASKS_TXEN = 1;
#endif // defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)

            m_state = STATE_CARRIER_TEST;

            break;

        case SET_TX_POWER:
            if (!dtm_set_txpower(vendor_option))
            {
                m_event = LE_TEST_STATUS_EVENT_ERROR;
                return DTM_ERROR_ILLEGAL_CONFIGURATION;
            }
            break;

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        case SET_NRF21540_TX_POWER:
            if(!dtm_set_nrf21450_power_mode((dtm_nrf21540_power_mode_t)vendor_option))
            {
                m_event = LE_TEST_STATUS_EVENT_ERROR;
                return DTM_ERROR_ILLEGAL_CONFIGURATION;
            }
            break;
#endif // defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
    }

    // Event code is unchanged, successful
    return DTM_SUCCESS;
}


static uint32_t dtm_packet_interval_calculate(uint32_t test_payload_length, uint32_t mode)
{
    uint32_t test_packet_length = 0; // [us] NOTE: bits are us at 1Mbit
    uint32_t packet_interval    = 0; // us
    uint32_t overhead_bits      = 0; // bits

    /* packet overhead
     * see BLE [Vol 6, Part F] page 213
     * 4.1 LE TEST PACKET FORMAT */
    if (mode == RADIO_MODE_MODE_Ble_2Mbit)
    {
        // 16 preamble
        // 32 sync word
        //  8 PDU header, actually packetHeaderS0len * 8
        //  8 PDU length, actually packetHeaderLFlen
        // 24 CRC
        overhead_bits = 88; // 11 bytes
    }
    else if (mode == RADIO_MODE_MODE_Ble_1Mbit)
    {
        //  8 preamble
        // 32 sync word
        //  8 PDU header, actually packetHeaderS0len * 8
        //  8 PDU length, actually packetHeaderLFlen
        // 24 CRC
        overhead_bits = 80; // 10 bytes
    }
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    else if (mode == RADIO_MODE_MODE_Ble_LR125Kbit)
    {
        // 80     preamble
        // 32 * 8 sync word coding=8
        //  2 * 8 Coding indicator, coding=8
        //  3 * 8 TERM1 coding=8
        //  8 * 8 PDU header, actually packetHeaderS0len * 8 coding=8
        //  8 * 8 PDU length, actually packetHeaderLFlen coding=8
        // 24 * 8 CRC coding=8
        //  3 * 8 TERM2 coding=8
        overhead_bits = 720; // 90 bytes
    }
    else if (mode == RADIO_MODE_MODE_Ble_LR500Kbit)
    {
        // 80     preamble
        // 32 * 8 sync word coding=8
        //  2 * 8 Coding indicator, coding=8
        //  3 * 8 TERM 1 coding=8
        //  8 * 2 PDU header, actually packetHeaderS0len * 8 coding=2
        //  8 * 2 PDU length, actually packetHeaderLFlen coding=2
        // 24 * 2 CRC coding=2
        //  3 * 2 TERM2 coding=2
        // NOTE: this makes us clock out 46 bits for CI + TERM1 + TERM2
        //       assumption the radio will handle this
        overhead_bits = 462; // 57.75 bytes
    }
#endif //defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    /* add PDU payload test_payload length */
    test_packet_length = (test_payload_length * 8); // in bits
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    // account for the encoding of PDU
    if (mode == RADIO_MODE_MODE_Ble_LR125Kbit)
    {
        test_packet_length *= 8; // 1 to 8 encoding
    }
    if (mode == RADIO_MODE_MODE_Ble_LR500Kbit)
    {
        test_packet_length *= 2; //  1 to 2 encoding
    }
#endif //defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    // add overhead calculated above
    test_packet_length += overhead_bits;
    // we remember this bits are us in 1Mbit
    if (mode == RADIO_MODE_MODE_Ble_2Mbit)
    {
        test_packet_length /= 2; // double speed
    }

#if DIRECTION_FINDING_SUPPORTED
    if (m_cte_mode != CTE_MODE_OFF)
    {
        // Add 8 - bit S1 field with CTEInfo.
        ((test_packet_length += mode) == RADIO_MODE_MODE_Ble_1Mbit) ? 8 : 4; 

        // Add CTE length in us to test packet length.
        test_packet_length += m_cte_time * NRF_CTE_TIME_IN_US;
    }
#endif // DIRECTION_FINDING_SUPPORTED

    /*
     * packet_interval = ceil((test_packet_length+249)/625)*625
     * NOTE: To avoid floating point an equivalent calculation is used.
     */
    uint32_t i       = 0;
    uint32_t timeout = 0;
    do
    {
        i++;
        timeout = i * 625;
    } while (test_packet_length + 249 > timeout);
    packet_interval = i * 625;

    return packet_interval;
}


static uint32_t phy_set(uint8_t phy)
{
    if ((phy >= LE_PHY_1M_MIN_RANGE) && (phy <= LE_PHY_1M_MAX_RANGE))
    {
        m_radio_mode        = RADIO_MODE_MODE_Ble_1Mbit;
        m_packetHeaderPlen  = RADIO_PCNF0_PLEN_8bit;

#ifdef NRF52840_XXAA
        // Workaround for Errata ID 191
        *(volatile uint32_t *) 0x40001740 = ((*((volatile uint32_t *) 0x40001740)) & 0x7FFFFFFF);
#endif
        // Disable the workaround for nRF52840 anomaly 172.
        set_strict_mode(0);
        nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);
        anomaly_172_wa_enabled = false;

        return radio_init();
    }
    else if ((phy >= LE_PHY_2M_MIN_RANGE) && (phy <= LE_PHY_2M_MAX_RANGE))
    {
        m_radio_mode        = RADIO_MODE_MODE_Ble_2Mbit;
        m_packetHeaderPlen  = RADIO_PCNF0_PLEN_16bit;

#ifdef NRF52840_XXAA
        // Workaround for Errata ID 191
        *(volatile uint32_t *) 0x40001740 = ((*((volatile uint32_t *) 0x40001740)) & 0x7FFFFFFF);
#endif

        // Disable the workaround for nRF52840 anomaly 172.
        set_strict_mode(0);
        nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);
        anomaly_172_wa_enabled = false;

        return radio_init();
    }
    else if ((phy >= LE_PHY_LE_CODED_S8_MIN_RANGE) && (phy <= LE_PHY_LE_CODED_S8_MAX_RANGE))
    {
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
        m_radio_mode        = RADIO_MODE_MODE_Ble_LR125Kbit;
        m_packetHeaderPlen  = RADIO_PCNF0_PLEN_LongRange;
#ifdef NRF52840_XXAA
        //  Workaround for Errata ID 191
        *(volatile uint32_t *) 0x40001740 = ((*((volatile uint32_t *) 0x40001740)) & 0x7FFF00FF) | 0x80000000 | (((uint32_t)(196)) << 8);

        // Enable the workaround for nRF52840 anomaly 172 on affected devices.
        if ((*(volatile uint32_t *)0x40001788) == 0)
        {
            anomaly_172_wa_enabled = true;
        }
#endif //NRF52840_XXAA
        return radio_init();
#else
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
#endif //defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
    }
    else if ((phy >= LE_PHY_LE_CODED_S2_MIN_RANGE) && (phy <= LE_PHY_LE_CODED_S2_MAX_RANGE))
    {
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA) || defined(NRF52811_XXAA) || defined(NRF52820_XXAA)
        m_radio_mode        = RADIO_MODE_MODE_Ble_LR500Kbit;
        m_packetHeaderPlen  = RADIO_PCNF0_PLEN_LongRange;

#ifdef NRF52840_XXAA
        //  Workaround for Errata ID 191
        *(volatile uint32_t *) 0x40001740 = ((*((volatile uint32_t *) 0x40001740)) & 0x7FFF00FF) | 0x80000000 | (((uint32_t)(196)) << 8);

        // Enable the workaround for nRF52840 anomaly 172 on affected devices.
        if ((*(volatile uint32_t *)0x40001788) == 0)
        {
            anomaly_172_wa_enabled = true;
        }
#endif //NRF52840_XXAA

        return radio_init();
#else
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
#endif
    }
    else
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }
}


static uint32_t modulation_set(uint8_t modulation)
{
    // Only standard modulation is supported.
    if (modulation > LE_MODULATION_INDEX_STANDARD_MAX_RANGE)
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    return DTM_SUCCESS;
}


static uint32_t feature_read(uint8_t cmd)
{
    if (cmd > LE_TEST_FEATURE_READ_MAX_RANGE)
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    m_event = DTM_SUPPORTED_FEATURE;

    return DTM_SUCCESS;
}


static uint32_t maximum_supported_value_read(uint8_t parameter)
{
    // Read supportedMaxTxOctets
    if (parameter <= LE_TEST_SUPPORTED_TX_OCTETS_MAX_RANGE)
    {
        m_event = NRF_MAX_PAYLOAD_OCTETS << DTM_RESPONSE_EVENT_SHIFT;
    }
    // Read supportedMaxTxTime
    else if ((parameter >= LE_TEST_SUPPORTED_TX_TIME_MIN_RANGE) &&
             (parameter <= LE_TEST_SUPPORTED_TX_TIME_MAX_RANGE))
    {
        m_event = NRF_MAX_RX_TX_TIME << DTM_RESPONSE_EVENT_SHIFT;
    }
    // Read supportedMaxRxOctets
    else if ((parameter >= LE_TEST_SUPPORTED_RX_OCTETS_MIN_RANGE) &&
             (parameter <= LE_TEST_SUPPORTED_RX_OCTETS_MAX_RANGE))
    {
        m_event = NRF_MAX_PAYLOAD_OCTETS << DTM_RESPONSE_EVENT_SHIFT;  
    }
    // Read supportedMaxRxTime
    else if ((parameter >= LE_TEST_SUPPORTED_RX_TIME_MIN_RANGE) &&
             (parameter <= LE_TEST_SUPPORTED_RX_TIME_MAX_RANGE))
    {
        m_event = NRF_MAX_RX_TX_TIME << DTM_RESPONSE_EVENT_SHIFT;
    }
#if DIRECTION_FINDING_SUPPORTED
    // Read maximum length of Constant Tone Extension
    else if (parameter == LE_TEST_SUPPORTED_CTE_LENGTH)
    {
        m_event = NRF_CTE_MAX_LENGTH << DTM_RESPONSE_EVENT_SHIFT;
    }
#endif // DIRECTION_FINDING_SUPPORTED
    else
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    return DTM_SUCCESS;
}


static uint32_t transmit_power_set(int8_t parameter)
{  
    if (parameter == LE_TRANSMIT_POWER_LVL_SET_MIN)
    {
        m_tx_power = nrf_power_value[0];
        m_event = ((m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
                    LE_TRANSMIT_POWER_RESPONSE_LVL_MASK) |
                  LE_TRANSMIT_POWER_MIN_LVL_BIT;

        return DTM_SUCCESS;
    }

    if (parameter == LE_TRANSMIT_POWER_LVL_SET_MAX)
    {
        m_tx_power = nrf_power_value[ARRAY_SIZE(nrf_power_value) - 1];
        m_event = ((m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
                    LE_TRANSMIT_POWER_RESPONSE_LVL_MASK) |
                  LE_TRANSMIT_POWER_MAX_LVL_BIT;

        return DTM_SUCCESS;
    }

    if (parameter < LE_TRANSMIT_POWER_LVL_MIN || parameter > LE_TRANSMIT_POWER_LVL_MAX)
    {
        m_event = ((m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
                    LE_TRANSMIT_POWER_RESPONSE_LVL_MASK) |
                  LE_TEST_STATUS_EVENT_ERROR;
        
        if (m_tx_power == nrf_power_value[0])
        {
            m_event |= LE_TRANSMIT_POWER_MIN_LVL_BIT;
        }
        else if (m_tx_power == nrf_power_value[ARRAY_SIZE(nrf_power_value) - 1])
        {
             m_event |= LE_TRANSMIT_POWER_MAX_LVL_BIT;
        }
        else
        {
            // Do nothing.
        }

        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    if (parameter <= ((int8_t) nrf_power_value[0]))
    {
        m_tx_power = nrf_power_value[0];
        m_event = ((m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
                    LE_TRANSMIT_POWER_RESPONSE_LVL_MASK) |
                  LE_TRANSMIT_POWER_MIN_LVL_BIT;

        return DTM_SUCCESS;
    }

    if (parameter >= ((int8_t) nrf_power_value[ARRAY_SIZE(nrf_power_value) - 1]))
    {
        m_tx_power = nrf_power_value[ARRAY_SIZE(nrf_power_value) - 1];
        m_event = ((m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
                    LE_TRANSMIT_POWER_RESPONSE_LVL_MASK) |
                  LE_TRANSMIT_POWER_MAX_LVL_BIT;

        return DTM_SUCCESS;
    }

    // Look for the nearest tansmit power level and set it.
    for (uint8_t i = 1; i < ARRAY_SIZE(nrf_power_value); i++)
    {
        if (((int8_t) nrf_power_value[i]) > parameter)
        {
            int8_t diff = abs((int8_t) nrf_power_value[i] - parameter);

            if (diff <  abs((int8_t) nrf_power_value[i - 1] - parameter))
            {
                m_tx_power = nrf_power_value[i];
            }
            else
            {
                m_tx_power = nrf_power_value[i - 1];
            }

            break;
        }
    }

    m_event = (m_tx_power << LE_TRANSMIT_POWER_RESPONSE_LVL_POS) &
               LE_TRANSMIT_POWER_RESPONSE_LVL_MASK;

    return DTM_SUCCESS;
}

#if DIRECTION_FINDING_SUPPORTED
static uint32_t constant_tone_setup(uint8_t cte_info)
{
    uint8_t type = (cte_info >> LE_CTE_TYPE_POS) & LE_CTE_TYPE_MASK;
    m_cte_time   = cte_info & LE_CTE_CTETIME_MASK;
    m_cte_info   = cte_info;


    if ((m_cte_time < LE_CTE_LENGTH_MIN) || (m_cte_time > LE_CTE_LENGTH_MAX))
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    switch (type)
    {
        case LE_CTE_TYPE_AOA:
            m_cte_mode = CTE_MODE_AOA;

            break;

        case LE_CTE_TYPE_AOD_1US:
            m_cte_mode = CTE_MODE_AOD;
            m_cte_slot = CTE_SLOT_1US;

            break;

        case LE_CTE_TYPE_AOD_2US:
            m_cte_mode = CTE_MODE_AOD;
            m_cte_slot = CTE_SLOT_2US;

            break;

        default:
            m_event = LE_TEST_STATUS_EVENT_ERROR;
            return DTM_ERROR_ILLEGAL_CONFIGURATION; 
    }

    return DTM_SUCCESS;
}
#else
static uint32_t constant_tone_setup(uint8_t cte_info)
{
    UNUSED_PARAMETER(cte_info);

    m_event = LE_TEST_STATUS_EVENT_ERROR;
    return DTM_ERROR_ILLEGAL_CONFIGURATION;
}
#endif // DIRECTION_FINDING_SUPPORTED


#if DIRECTION_FINDING_SUPPORTED
static uint32_t constant_tone_slot_set(uint8_t cte_slot)
{
    if (cte_slot == LE_CTE_TYPE_AOD_1US)
    {
        m_cte_slot = CTE_SLOT_1US;

        return DTM_SUCCESS;
    }

    if (cte_slot == LE_CTE_TYPE_AOD_2US)
    {
        m_cte_slot = CTE_SLOT_2US;

        return DTM_SUCCESS;
    }

    m_event = LE_TEST_STATUS_EVENT_ERROR;
    return DTM_ERROR_ILLEGAL_CONFIGURATION;
}
#else
static uint32_t constant_tone_slot_set(uint8_t cte_slot)
{
    UNUSED_PARAMETER(cte_slot);
 
    m_event = LE_TEST_STATUS_EVENT_ERROR;
    return DTM_ERROR_ILLEGAL_CONFIGURATION; 
}
#endif // DIRECTION_FINDING_SUPPORTED


#if DIRECTION_FINDING_SUPPORTED
static uint32_t antenna_set(uint8_t antenna)
{
    m_antenna_number  = antenna & LE_ANTENNA_NUMBER_MASK;
    m_antenna_pattern = (antenna_pattern_t)(antenna & LE_ANTENA_SWITCH_PATTERN_MASK);

    if ((m_antenna_number < LE_TEST_ANTENNA_NUMBER_MIN) ||
        (m_antenna_number > LE_TEST_ANTENNA_NUMBER_MAX) ||
        (m_antenna_number > NRF_RADIO_ANTENNA_COUNT))
    {
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    return DTM_SUCCESS;
}
#else
static uint32_t antenna_set(uint8_t antenna)
{
    UNUSED_PARAMETER(antenna);
    
    m_event = LE_TEST_STATUS_EVENT_ERROR;
    return DTM_ERROR_ILLEGAL_CONFIGURATION; 
}
#endif // DIRECTION_FINDING_SUPPORTED


static uint32_t on_test_setup_cmd(uint8_t control, uint8_t parameter)
{
    // Note that timer will continue running after a reset
    dtm_test_done();

    switch (control)
    {
        case LE_TEST_SETUP_RESET:
            if (parameter > LE_RESET_MAX_RANGE)
            {
                m_event = LE_TEST_STATUS_EVENT_ERROR;
                return DTM_ERROR_ILLEGAL_CONFIGURATION;
            }
            // Reset the packet length upper bits.
            m_packet_length = 0;

            // Reset the selected PHY to 1Mbit
            m_radio_mode        = RADIO_MODE_MODE_Ble_1Mbit;
            m_packetHeaderPlen  = RADIO_PCNF0_PLEN_8bit;

#if DIRECTION_FINDING_SUPPORTED
            m_cte_mode = CTE_MODE_OFF;
            radio_gpio_pattern_clear();
#endif // DIRECTION_FINDING_SUPPORTED

#ifdef NRF52840_XXAA
            // Workaround for Errata ID 191
            *(volatile uint32_t *) 0x40001740 = ((*((volatile uint32_t *) 0x40001740)) & 0x7FFFFFFF);
#endif
            break;

        case LE_TEST_SETUP_SET_UPPER:
            if (parameter > LE_SET_UPPER_BITS_MAX_RANGE)
            {
                m_event = LE_TEST_STATUS_EVENT_ERROR;
                return DTM_ERROR_ILLEGAL_CONFIGURATION;
            }
 
            m_packet_length = (parameter & LE_UPPER_BITS_MASK) << LE_UPPER_BITS_POS;
        
            break;

        case  LE_TEST_SETUP_SET_PHY:
            return phy_set(parameter);

        case LE_TEST_SETUP_SELECT_MODULATION:
            return modulation_set(parameter);

        case LE_TEST_SETUP_READ_SUPPORTED:
            return feature_read(parameter);

        case LE_TEST_SETUP_READ_MAX:
            return maximum_supported_value_read(parameter);

        case LE_TEST_SETUP_TRANSMIT_POWER:
            return transmit_power_set(parameter);

        case LE_TEST_SETUP_CONSTANT_TONE:
            return constant_tone_setup(parameter);

        case LE_TEST_SETUP_CONSTANT_TONE_SLOT:
            return constant_tone_slot_set(parameter);

        case LE_TEST_SETUP_ANTENNA_ARRAY:
            return antenna_set(parameter);
       
        default:
            m_event = LE_TEST_STATUS_EVENT_ERROR;
            return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    return DTM_SUCCESS;
}


static uint32_t on_test_end_cmd(void)
{
    if (m_state == STATE_IDLE)
    {
        // Sequencing error - only rx or tx test may be ended!
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_INVALID_STATE;
    }
 
    m_event = LE_PACKET_REPORTING_EVENT | m_rx_pkt_count;
    dtm_test_done();

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
    (void) nrf21540_power_down(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_BLOCKING);
#endif
    return DTM_SUCCESS;
}


static uint32_t on_test_transmit_cmd(uint32_t length, dtm_freq_t freq)
{
        uint8_t header_len;

        mp_current_pdu = m_pdu;
 
        // Check for illegal values of m_packet_length. Skip the check if the packet is vendor spesific.
        if (m_packet_type != DTM_PKT_TYPE_VENDORSPECIFIC && m_packet_length > DTM_PAYLOAD_MAX_SIZE)
        {
            // Parameter error
            m_event = LE_TEST_STATUS_EVENT_ERROR;
            return DTM_ERROR_ILLEGAL_LENGTH;
        }

#if DIRECTION_FINDING_SUPPORTED
        header_len = (m_cte_mode != CTE_MODE_OFF) ? DTM_HEADER_WITH_CTE_SIZE : DTM_HEADER_SIZE;
#else
        header_len = DTM_HEADER_SIZE;
#endif // DIRECTION_FINDING_SUPPORTED

        mp_current_pdu->content[DTM_LENGTH_OFFSET] = m_packet_length;
        // Note that PDU uses 4 bits even though BLE DTM uses only 2 (the HCI SDU uses all 4)
        switch (m_packet_type)
        {
            case DTM_PKT_PRBS9:
                mp_current_pdu->content[DTM_HEADER_OFFSET] = DTM_PDU_TYPE_PRBS9;
                // Non-repeated, must copy entire pattern to PDU
                memcpy(mp_current_pdu->content + header_len, m_prbs_content, m_packet_length);
                break;

            case DTM_PKT_0X0F:
                mp_current_pdu->content[DTM_HEADER_OFFSET] = DTM_PDU_TYPE_0X0F;
                // Bit pattern 00001111 repeated
                memset(mp_current_pdu->content + header_len, RFPHY_TEST_0X0F_REF_PATTERN, m_packet_length);
                break;

            case DTM_PKT_0X55:
                mp_current_pdu->content[DTM_HEADER_OFFSET] = DTM_PDU_TYPE_0X55;
                // Bit pattern 01010101 repeated
                memset(mp_current_pdu->content + header_len, RFPHY_TEST_0X55_REF_PATTERN, m_packet_length);
                break;

            case DTM_PKT_0XFF:
                mp_current_pdu->content[DTM_HEADER_OFFSET] = DTM_PDU_TYPE_0XFF;
                // Bit pattern 11111111 repeated. Only available in coded PHY (Long range).
                memset(mp_current_pdu->content + header_len, RFPHY_TEST_0XFF_REF_PATTERN, m_packet_length);
                break;

            case DTM_PKT_TYPE_VENDORSPECIFIC:
                // The length field is for indicating the vendor specific command to execute.
                // The frequency field is used for vendor specific options to the command.
                return dtm_vendor_specific_pkt(length, freq);

            default:
                // Parameter error
                m_event = LE_TEST_STATUS_EVENT_ERROR;
                return DTM_ERROR_ILLEGAL_CONFIGURATION;
        }

#if DIRECTION_FINDING_SUPPORTED
        if (m_cte_mode != CTE_MODE_OFF)
        {
            mp_current_pdu->content[DTM_HEADER_OFFSET]         |= DTM_PKT_CP_BIT;
            mp_current_pdu->content[DTM_HEADER_CTEINFO_OFFSET]  = m_cte_info;
        }
#endif // DIRECTION_FINDING_SUPPORTED

        // Initialize CRC value, set channel:
        radio_prepare(TX_MODE);

        // Set the timer to the correct period. The delay between each packet is described in the
        // Bluetooth Core Specification version 4.2 Vol. 6 Part F Section 4.1.6.
        nrf_timer_cc_write(mp_timer,
                           NRF_TIMER_CC_CHANNEL0,
                           dtm_packet_interval_calculate(m_packet_length, m_radio_mode));

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        (void)nrf21540_tx_set((uint32_t) nrf_timer_event_address_get(mp_timer,
                                  nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0)),
                                  NRF21540_EXEC_MODE_NON_BLOCKING);
#else
        // Configure PPI so that timer will activate radio every 625 us

        NRF_PPI->CH[0].EEP = (uint32_t)nrf_timer_event_address_get(mp_timer,
                                            nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
        NRF_PPI->CH[0].TEP = (uint32_t)&NRF_RADIO->TASKS_TXEN;
        NRF_PPI->CHENSET   = 0x01;
#endif
        m_state            = STATE_TRANSMITTER_TEST;

        return DTM_SUCCESS;
}


static uint32_t on_test_receive_cmd(void)
{
        mp_current_pdu = m_pdu;

        // Zero fill all pdu fields to avoid stray data from earlier test run
        memset(&m_pdu[0], 0, DTM_PDU_MAX_MEMORY_SIZE);
        memset(&m_pdu[1], 0, DTM_PDU_MAX_MEMORY_SIZE);
        
        // Reinitialize "everything"; RF interrupts OFF
        radio_prepare(RX_MODE);
        m_state = STATE_RECEIVER_TEST;

        return DTM_SUCCESS;
} 


uint32_t dtm_init(void)
{
    if ((timer_init() != DTM_SUCCESS) || (radio_init() != DTM_SUCCESS))
    {
        return DTM_ERROR_ILLEGAL_CONFIGURATION;
    }

    m_new_event     = false;
    m_state         = STATE_IDLE;
    m_packet_length = 0;

#if defined(NRF_NVMC_ICACHE_PRESENT)
    // Enable cache
    NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Enabled << NVMC_ICACHECNF_CACHEEN_Pos) & NVMC_ICACHECNF_CACHEEN_Msk;
#endif

    // Set Radio interrupt priority
    NVIC_SetPriority(RADIO_IRQn, DTM_RADIO_IRQ_PRIORITY);

    return DTM_SUCCESS;
}


uint32_t dtm_wait(void)
{
    for (;;)
    {
        if (nrf_timer_event_check(mp_timer,
                        nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1)))
        {
            // Reset timeout event flag for next iteration.
            nrf_timer_event_clear(mp_timer,
                              nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));
            return ++m_current_time;
        }

        // Other events: No processing
    }
}


uint32_t dtm_cmd(uint16_t cmd)
{
    uint8_t command = (cmd >> 14) & 0x03;

    // Clean out any non-retrieved event that might linger from an earlier test
    m_new_event     = true;

    // Set default event; any error will set it to LE_TEST_STATUS_EVENT_ERROR
    m_event         = LE_TEST_STATUS_EVENT_SUCCESS;

    if (m_state == STATE_UNINITIALIZED)
    {
        // Application has not explicitly initialized DTM,
        return DTM_ERROR_UNINITIALIZED;
    }

    if (command == LE_TEST_SETUP)
    {
        uint8_t control = (cmd >> 8) & 0x3F;
        uint8_t parameter = cmd;

        return on_test_setup_cmd(control, parameter);
    }

    if (command == LE_TEST_END)
    {
        return on_test_end_cmd();
    }

    if (m_state != STATE_IDLE)
    {
        // Sequencing error - only TEST_END/RESET are legal while test is running
        // Note: State is unchanged; ongoing test not affected
        m_event = LE_TEST_STATUS_EVENT_ERROR;
        return DTM_ERROR_INVALID_STATE;
    }

    uint8_t length  = (cmd >> 2) & 0x3F;
    uint8_t freq    = (cmd >> 8) & 0x3F;
    uint8_t payload = cmd & 0x03;

    // Save specified packet in static variable for tx/rx functions to use.
    // Note that BLE conformance testers always use full length packets.
    m_packet_length = (m_packet_length & 0xC0) | ((uint8_t)length & 0x3F);
    m_packet_type   = payload;
    m_phys_ch       = freq;

    // If 1 Mbit or 2 Mbit radio mode is in use check for Vendor Specific payload.
    if ((m_radio_mode == RADIO_MODE_MODE_Ble_1Mbit || m_radio_mode == RADIO_MODE_MODE_Ble_2Mbit) && payload == DTM_PKT_VENDORSPECIFIC)
    {
        /* Note that in a HCI adaption layer, as well as in the DTM PDU format,
           the value 0x03 is a distinct bit pattern (PRBS15). Even though BLE does not
           support PRBS15, this implementation re-maps 0x03 to DTM_PKT_VENDORSPECIFIC,
           to avoid the risk of confusion, should the code be extended to greater coverage.
        */
        m_packet_type = DTM_PKT_TYPE_VENDORSPECIFIC;
    }


    // Check for illegal values of m_phys_ch. Skip the check if the packet is vendor spesific.
    if (payload != DTM_PKT_VENDORSPECIFIC && m_phys_ch > PHYS_CH_MAX)
    {
        // Parameter error
        // Note: State is unchanged; ongoing test not affected
        m_event = LE_TEST_STATUS_EVENT_ERROR;

        return DTM_ERROR_ILLEGAL_CHANNEL;
    }

    m_rx_pkt_count = 0;

    if (command == LE_RECEIVER_TEST)
    {
        return on_test_receive_cmd();
    }

    if (command == LE_TRANSMITTER_TEST)
    {
        return on_test_transmit_cmd(length, freq);
    }

    return DTM_SUCCESS;
}


bool dtm_event_get(dtm_event_t *p_dtm_event)
{
    bool was_new = m_new_event;
    // mark the current event as retrieved
    m_new_event  = false;
    *p_dtm_event = m_event;
    // return value indicates whether this value was already retrieved.
    return was_new;
}


/**@brief Function for configuring the output power for transmitter test.
          This function may be called directly, or through dtm_cmd() specifying
          DTM_PKT_VENDORSPECIFIC as payload, SET_TX_POWER as length, and the dBm value as frequency.
 */
bool dtm_set_txpower(uint32_t new_tx_power)
{
    // radio->TXPOWER register is 32 bits, low octet a tx power value, upper 24 bits zeroed
    uint8_t new_power8 = (uint8_t)(new_tx_power & 0xFF);

    // The two most significant bits are not sent in the 6 bit field of the DTM command.
    // These two bits are 1's if and only if the tx_power is a negative number.
    // All valid negative values have a non zero bit in among the two most significant
    // of the 6-bit value.
    // By checking these bits, the two most significant bits can be determined.
    new_power8 = (new_power8 & 0x30) != 0 ? (new_power8 | 0xC0) : new_power8;

    if (m_state > STATE_IDLE)
    {
        // radio must be idle to change the tx power
        return false;
    }

    if (dtm_radio_validate(new_power8, m_radio_mode) != DTM_SUCCESS)
    {
        return false;
    }

    m_tx_power = new_power8;

    return true;
}

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
bool dtm_set_nrf21450_power_mode(dtm_nrf21540_power_mode_t power_mode)
{
    nrf21540_pwr_mode_t pwr_mode;

    if (m_state > STATE_IDLE)
    {
        return false;
    }

    switch (power_mode)
    {
        case NRF21540_POWER_MODE_A:
            pwr_mode = NRF21540_PWR_MODE_A;

            break;
        
        case NRF21540_POWER_MODE_B:
            pwr_mode = NRF21540_PWR_MODE_B;

            break;

        default:
          return false;
    }

    if (nrf21540_pwr_mode_set(pwr_mode) == NRF_SUCCESS)
    {
        return true;
    }

    return false;
}
#endif // defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)

static void radio_end_event_process(void)
{
#if !defined(NRF21540_DRIVER_ENABLE) || (NRF21540_DRIVER_ENABLE == 0) || \
    (defined(NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER) && \
    (NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER == 1))
    NVIC_ClearPendingIRQ(RADIO_IRQn);
#endif

    if (m_state == STATE_RECEIVER_TEST)
    {
        pdu_type_t * received_pdu = radio_buffer_swap();

#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        (void) nrf21540_rx_set(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_NON_BLOCKING);
#else
        nrf_radio_task_trigger(NRF_RADIO_TASK_RXEN);
#endif
        if (anomaly_172_wa_enabled)
        {
            nrf_timer_cc_write(ANOMALY_172_TIMER, NRF_TIMER_CC_CHANNEL0, BLOCKER_FIX_WAIT_DEFAULT);
            nrf_timer_cc_write(ANOMALY_172_TIMER, NRF_TIMER_CC_CHANNEL1, BLOCKER_FIX_WAIT_END);
            nrf_timer_event_clear(ANOMALY_172_TIMER,
                                  nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
            nrf_timer_event_clear(ANOMALY_172_TIMER,
                                  nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));

            nrf_timer_int_enable(ANOMALY_172_TIMER,
                         NRF_TIMER_INT_COMPARE1_MASK);

            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_CLEAR);
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_START);
        }

        // Note that failing packets are simply ignored (CRC or contents error).
        if (nrf_radio_crc_status_check() && check_pdu(received_pdu))
        {
            // Count the number of successfully received packets
            m_rx_pkt_count++;
        }

        // Zero fill all pdu fields to avoid stray data
        memset(received_pdu, 0, DTM_PDU_MAX_MEMORY_SIZE);
    }
}

void RADIO_IRQHandler(void)
{
    if (nrf_radio_event_check(NRF_RADIO_EVENT_ADDRESS))
    {
        nrf_radio_event_clear(NRF_RADIO_EVENT_ADDRESS);
        if ((m_state == STATE_RECEIVER_TEST) && anomaly_172_wa_enabled)
        {
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);
        }
    }

    if (nrf_radio_event_check(NRF_RADIO_EVENT_END))
    {
#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        if (m_state != STATE_CARRIER_TEST)
        {
            (void)nrf21540_power_down(NRF21540_EXECUTE_NOW, NRF21540_EXEC_MODE_BLOCKING);
        }
#endif
        nrf_radio_event_clear(NRF_RADIO_EVENT_END);

        radio_end_event_process();
    }
    
    if (nrf_radio_event_check(NRF_RADIO_EVENT_READY))
    {
        nrf_radio_event_clear(NRF_RADIO_EVENT_READY);
        if ((m_state == STATE_RECEIVER_TEST) && anomaly_172_wa_enabled)
        {
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_CLEAR);
            nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_START);
        }
    }
}

void DTM_TIMER_IRQHandler(void)
{
    if (nrf_timer_event_check(mp_timer,
                              nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0)))
    {
        nrf_timer_event_clear(mp_timer,
                              nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
        
#if defined(NRF21540_DRIVER_ENABLE) && (NRF21540_DRIVER_ENABLE == 1)
        if (m_state == STATE_TRANSMITTER_TEST)
        {
            (void) nrf21540_tx_set((uint32_t) nrf_timer_event_address_get(mp_timer,
                                   nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0)),
                                   NRF21540_EXEC_MODE_NON_BLOCKING);
        }
#endif
    }
}

void ANOMALY_172_TIMER_IRQHandler(void)
{
    if (nrf_timer_event_check(ANOMALY_172_TIMER,
                        nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0)) &&
        nrf_timer_int_enable_check(ANOMALY_172_TIMER,
                        nrf_timer_compare_int_get(NRF_TIMER_CC_CHANNEL0)))
    {
        uint8_t rssi = anomaly_172_rssi_check();
        if (m_strict_mode)
        {
            if (rssi > BLOCKER_FIX_RSSI_THRESHOLD)
            {
                set_strict_mode(0);
            }
        }
        else
        {
            bool too_many_detects = false;
            uint32_t packetcnt2 = *(volatile uint32_t *) 0x40001574;
            uint32_t detect_cnt = packetcnt2 & 0xffff;
            uint32_t addr_cnt   = (packetcnt2 >> 16) & 0xffff;

            if ((detect_cnt > BLOCKER_FIX_CNTDETECTTHR) && (addr_cnt < BLOCKER_FIX_CNTADDRTHR))
            {
                too_many_detects = true;
            }

            if ((rssi < BLOCKER_FIX_RSSI_THRESHOLD) || too_many_detects)
            {
                 set_strict_mode(1);
            }
        }

        anomaly_172_radio_operation();

        nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_STOP);
        nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_CLEAR);
        nrf_timer_event_clear(ANOMALY_172_TIMER,
                              nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL0));
        nrf_timer_task_trigger(ANOMALY_172_TIMER, NRF_TIMER_TASK_START);
    }

    if (nrf_timer_event_check(ANOMALY_172_TIMER,
                            nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1)) &&
        nrf_timer_int_enable_check(ANOMALY_172_TIMER,
                            nrf_timer_compare_int_get(NRF_TIMER_CC_CHANNEL1)))
    {
        uint8_t rssi = anomaly_172_rssi_check();
        if (m_strict_mode)
        {
            if (rssi >= BLOCKER_FIX_RSSI_THRESHOLD)
            {
                set_strict_mode(0);
            }
        }
        else
        {
            if (rssi < BLOCKER_FIX_RSSI_THRESHOLD)
            {
                set_strict_mode(1);
            }
        }

        anomaly_172_radio_operation();

        // Disable interrupt from the one-shot timer.
        nrf_timer_int_disable(ANOMALY_172_TIMER,
                              NRF_TIMER_INT_COMPARE1_MASK);
        // Disable this event.
        nrf_timer_event_clear(ANOMALY_172_TIMER,
                              nrf_timer_compare_event_get(NRF_TIMER_CC_CHANNEL1));
        nrf_timer_cc_write(ANOMALY_172_TIMER, NRF_TIMER_CC_CHANNEL1, 0);
    }

    NVIC_ClearPendingIRQ(ANOMALY_172_TIMER_IRQn);
}

/// @}
#endif // NRF_MODULE_ENABLED(BLE_DTM)
