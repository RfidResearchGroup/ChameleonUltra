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
/** @file
 *
 * @defgroup ble_dtm DTM - Direct Test Mode
 * @{
 * @ingroup ble_sdk_lib
 * @brief Module for testing RF/PHY using DTM commands.
 */

#ifndef BLE_DTM_H__
#define BLE_DTM_H__

#include <stdint.h>
#include <stdbool.h>
#include "nrf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DTM_MAX_ANTENNA_CNT 0x13 /**< Maximum supported antenna count. */

/**@brief Configuration parameters. */
#define DTM_BITRATE          UARTE_BAUDRATE_BAUDRATE_Baud19200                    /**< Serial bitrate on the UART */
#define DEFAULT_TX_POWER     RADIO_TXPOWER_TXPOWER_0dBm                           /**< Default Transmission power using in the DTM module. */
#define DTM_TIMER            CONCAT_2(NRF_TIMER, NRF_DTM_TIMER_INSTANCE)          /**< Default timer used for timing. */
#define DTM_TIMER_IRQn       CONCAT_3(TIMER, NRF_DTM_TIMER_INSTANCE, _IRQn )      /**< IRQ used for timer. NOTE: MUST correspond to DTM_TIMER. */
#define DTM_TIMER_IRQHandler CONCAT_3(TIMER, NRF_DTM_TIMER_INSTANCE, _IRQHandler) /**< IRQHandler used for DTM. It is used for sending PDU with valid interval and UART polling interval. */

#define ANOMALY_172_TIMER            NRF_TIMER1                          /**< Timer used for the workaround for errata 172 on affected nRF5 devices. */
#define ANOMALY_172_TIMER_IRQn       TIMER1_IRQn                         /**< IRQ used for timer. NOTE: MUST correspond to ERRATA_172_TIMER. */
#define ANOMALY_172_TIMER_IRQHandler TIMER1_IRQHandler                   /**< IRQHandler used for timer. NOTE: MUST correspond to ERRATA_172_TIMER. */
    
/**@brief BLE DTM command codes. */
typedef uint32_t dtm_cmd_t;                                                 /**< DTM command type. */

#define LE_TEST_SETUP                   0                                   /**< DTM command: Set PHY or modulation, configure upper two bits of length,
                                                                             request matrix of supported features or request max values of parameters. */
#define LE_RECEIVER_TEST                1                                   /**< DTM command: Start receive test. */
#define LE_TRANSMITTER_TEST             2                                   /**< DTM command: Start transmission test. */
#define LE_TEST_END                     3                                   /**< DTM command: End test and send packet report. */

#define LE_TEST_SETUP_RESET              0                                  /**< DTM command control: Stop TX/RX, reset the packet length upper bits and set the PHY to 1Mbit. */
#define LE_TEST_SETUP_SET_UPPER          1                                  /**< DTM command control: Set the upper two bits of the length field. */
#define LE_TEST_SETUP_SET_PHY            2                                  /**< DTM command control: Select the PHY to be used for packets. */
#define LE_TEST_SETUP_SELECT_MODULATION  3                                  /**< DTM command control: Select standard or stable modulation index. Stable modulation index is not supported. */
#define LE_TEST_SETUP_READ_SUPPORTED     4                                  /**< DTM command control: Read the supported test case features. */
#define LE_TEST_SETUP_READ_MAX           5                                  /**< DTM command control: Read the max supported time and length for packets. */
#define LE_TEST_SETUP_CONSTANT_TONE      6                                  /**< DTM command control: Constant Tone Extension info. */
#define LE_TEST_SETUP_CONSTANT_TONE_SLOT 7                                  /**< DTM command control: Constant Tone Extension slot. */
#define LE_TEST_SETUP_ANTENNA_ARRAY      8                                  /**< DTM command control: Antenna number and switch patern. */
#define LE_TEST_SETUP_TRANSMIT_POWER     9                                  /**< DTM command control: Transmit power set. */

#define LE_RESET_MIN_RANGE 0x00                                             /**< DTM command parameter: Reset. Minimum parameter value. */
#define LE_RESET_MAX_RANGE 0x03                                             /**< DTM command parameter: Reset. Maximum parameter value. */

#define LE_SET_UPPER_BITS_MIN_RANGE 0x00                                     /**< DTM command parameter: Set upper bits. Minimum parameter value. */
#define LE_SET_UPPER_BITS_MAX_RANGE 0x0F                                     /**< DTM command parameter: Set upper bits. Maximum parameter value. */

#define LE_PHY_1M_MIN_RANGE          0x04                                   /**< DTM command parameter: Set PHY for future packets to use 1MBit PHY. Minimum parameter value. */
#define LE_PHY_1M_MAX_RANGE          0x07                                   /**< DTM command parameter: Set PHY for future packets to use 1MBit PHY. Maximum parameter value. */
#define LE_PHY_2M_MIN_RANGE          0x08                                   /**< DTM command parameter: Set PHY for future packets to use 2MBit PHY. Minimum parameter value. */
#define LE_PHY_2M_MAX_RANGE          0x0B                                   /**< DTM command parameter: Set PHY for future packets to use 2MBit PHY. Maximum parameter value. */
#define LE_PHY_LE_CODED_S8_MIN_RANGE 0x0C                                   /**< DTM command parameter: Set PHY for future packets to use coded PHY with S=8. Minimum parameter value. */
#define LE_PHY_LE_CODED_S8_MAX_RANGE 0x0F                                   /**< DTM command parameter: Set PHY for future packets to use coded PHY with S=8. Maximum parameter value. */
#define LE_PHY_LE_CODED_S2_MIN_RANGE 0x10                                   /**< DTM command parameter: Set PHY for future packets to use coded PHY with S=2. Minimum parameter value. */
#define LE_PHY_LE_CODED_S2_MAX_RANGE 0x13                                   /**< DTM command parameter: Set PHY for future packets to use coded PHY with S=2. Maximum parameter value. */

#define LE_MODULATION_INDEX_STANDARD_MIN_RANGE 0x00                         /**< DTM command parameter: Set Modulation index to stadard. Minimum parameter value. */
#define LE_MODULATION_INDEX_STANDARD_MAX_RANGE 0x03                         /**< DTM command parameter: Set Modulation index to stadard. Maximum parameter value. */
#define LE_MODULATION_INDEX_STABLE_MIN_RANGE   0x04                         /**< DTM command parameter: Set Modulation index to stable. Minimum parameter value. */
#define LE_MODULATION_INDEX_STABLE_MAX_RANGE   0x07                         /**< DTM command parameter: Set Modulation index to stable. Maximum parameter value. */

#define LE_TEST_FEATURE_READ_MIN_RANGE 0x00                                 /**< DTM command parameter: Read test case supported feature. Minimum parameter value. */
#define LE_TEST_FEATURE_READ_MAX_RANGE 0x03                                 /**< DTM command parameter: Read test case supported feature. Maximum parameter value. */

#define LE_TEST_SUPPORTED_TX_OCTETS_MIN_RANGE 0x00                          /**< DTM command parameter: Read maximum supported Tx Octets. Minimum parameter value. */
#define LE_TEST_SUPPORTED_TX_OCTETS_MAX_RANGE 0x03                          /**< DTM command parameter: Read maximum supported Tx Octets. Maximum parameter value. */
#define LE_TEST_SUPPORTED_TX_TIME_MIN_RANGE   0x04                          /**< DTM command parameter: Read maximum supported Tx Time. Minimum parameter value. */
#define LE_TEST_SUPPORTED_TX_TIME_MAX_RANGE   0x07                          /**< DTM command parameter: Read maximum supported Tx Time. Maximum parameter value. */
#define LE_TEST_SUPPORTED_RX_OCTETS_MIN_RANGE 0x08                          /**< DTM command parameter: Read maximum supported Rx Octets. Minimum parameter value. */
#define LE_TEST_SUPPORTED_RX_OCTETS_MAX_RANGE 0x0B                          /**< DTM command parameter: Read maximum supported Rx Octets. Maximum parameter value. */
#define LE_TEST_SUPPORTED_RX_TIME_MIN_RANGE   0x0C                          /**< DTM command parameter: Read maximum supported Rx Time. Minimum parameter value. */
#define LE_TEST_SUPPORTED_RX_TIME_MAX_RANGE   0x0F                          /**< DTM command parameter: Read maximum supported Rx Time. Maximum parameter value. */
#define LE_TEST_SUPPORTED_CTE_LENGTH          0x10                          /**< DTM command parameter: Read maximum length of the Constant Tone Extension supported. */

#define LE_UPPER_BITS_MASK 0x0C                                             /**< DTM command parameter: Upper bits mask. */
#define LE_UPPER_BITS_POS 0x04                                              /**< DTM command parameter: Upper bits position. */

#define LE_TRANSMIT_POWER_LVL_MIN           -127                            /**< DTM command parameter: Minimum supported transmit power level. */
#define LE_TRANSMIT_POWER_LVL_MAX           20                              /**< DTM command parameter: Maximum supported transmit power level. */
#define LE_TRANSMIT_POWER_LVL_SET_MIN       0x7E                            /**< DTM command parameter: Set minimum transmit power level. */
#define LE_TRANSMIT_POWER_LVL_SET_MAX       0x7F                            /**< DTM command parameter: Set maximum  taranmit poer level. */

#define LE_TRANSMIT_POWER_RESPONSE_LVL_POS  (0x01)                         /** Position of power level in the DTM power level set response. */ 
#define LE_TRANSMIT_POWER_RESPONSE_LVL_MASK (0x1FE)                        /** Mask of the power level in the DTM power level set respose. */
#define LE_TRANSMIT_POWER_MAX_LVL_BIT       (1 << 0x0A)                    /** Maximum power level bit in the power level set response. */
#define LE_TRANSMIT_POWER_MIN_LVL_BIT       (1 << 0x09)                    /** Minimum power level bit in the power level set response. */

#define LE_CTE_TYPE_MASK    0x03                                           /** Mask of the CTE type in the CTEInfo. */
#define LE_CTE_TYPE_POS     0x06                                           /** Position of the CTE type in the CTEInfo. */
#define LE_CTE_CTETIME_MASK 0x1F                                           /** Mask of the CTE Time in the CTEInfo. */

#define LE_CTE_TYPE_AOA     0x00                                           /** CTE Type Angle of Arrival. */
#define LE_CTE_TYPE_AOD_1US 0x01                                           /** CTE Type Angle of Departure with 1 us slot. */
#define LE_CTE_TYPE_AOD_2US 0x02                                           /** CTE Type Angle of Departure with 2 us slot.*/

#define LE_CTE_LENGTH_MIN 0x02                                             /**< DTM command parameter: Minimum supported CTE length in 8 us units. */
#define LE_CTE_LENGTH_MAX 0x14                                             /**< DTM command parameter: Maximum supported CTE length in 8 us units. */

#define LE_ANTENNA_NUMBER_MASK        0x3F                                 /**< DTM command parameter: Mask of the Antenna Number. */
#define LE_ANTENA_SWITCH_PATTERN_MASK 0x80                                 /**< DTM command parameter: Mask of the Antenna switch pattern. */

#define LE_TEST_ANTENNA_NUMBER_MIN 0x01                                    /**< Minimum antenna number. */
#define LE_TEST_ANTENNA_NUMBER_MAX 0x4B                                    /**< Maximum antenna number. */


// Configuration options used as parameter 2
// when cmd == LE_TRANSMITTER_TEST and payload == DTM_PKT_VENDORSPECIFIC
// Configuration value, if any, is supplied in parameter 3

#define CARRIER_TEST                    0                                   /**< Length=0 indicates a constant, unmodulated carrier until LE_TEST_END or LE_RESET */
#define CARRIER_TEST_STUDIO             1                                   /**< nRFgo Studio uses value 1 in length field, to indicate a constant, unmodulated carrier until LE_TEST_END or LE_RESET */
#define SET_TX_POWER                    2                                   /**< Set transmission power, value -40..+4 dBm in steps of 4 */
#define SET_NRF21540_TX_POWER           4                                   /**< Set nRF21540 transmission power level. Choose between two predefinied option +20 dBm or +10 dBm. */

#define LE_PACKET_REPORTING_EVENT       0x8000                              /**< DTM Packet reporting event, returned by the device to the tester. */
#define LE_TEST_STATUS_EVENT_SUCCESS    0x0000                              /**< DTM Status event, indicating success. */
#define LE_TEST_STATUS_EVENT_ERROR      0x0001                              /**< DTM Status event, indicating an error. */

#define DTM_PKT_PRBS9                   0x00                                /**< Bit pattern PRBS9. */
#define DTM_PKT_0X0F                    0x01                                /**< Bit pattern 11110000 (LSB is the leftmost bit). */
#define DTM_PKT_0X55                    0x02                                /**< Bit pattern 10101010 (LSB is the leftmost bit). */
#define DTM_PKT_0XFF                    0x03                                /**< Bit pattern 11111111 (Used only for coded PHY). */
#define DTM_PKT_VENDORSPECIFIC          0x03                                /**< Vendor specific PKT field value. Nordic: Continuous carrier test, or configuration. */
#define DTM_PKT_TYPE_VENDORSPECIFIC     0xFF                                /**< Vendor specific packet type for internal use. */

#define DTM_PKT_CP_BIT                 0x20                                /**< CTEInfo Preset bit. Indicates whether the CTEInfo field is present in the packet. */

// The pdu payload type for each bit pattern. Identical to the PKT value except pattern 0xFF which is 0x04.
#define DTM_PDU_TYPE_PRBS9              0x00                                /**< PDU payload type for bit pattern PRBS9. */
#define DTM_PDU_TYPE_0X0F               0x01                                /**< PDU payload type for bit pattern 11110000 (LSB is the leftmost bit). */
#define DTM_PDU_TYPE_0X55               0x02                                /**< PDU payload type for bit pattern 10101010 (LSB is the leftmost bit). */
#define DTM_PDU_TYPE_0XFF               0x04                                /**< PDU payload type for bit pattern 11111111 (Used only for coded PHY). */

/**@brief Return codes from dtm_cmd(). */
#define DTM_SUCCESS                     0x00                                /**< Indicate that the DTM function completed with success. */
#define DTM_ERROR_ILLEGAL_CHANNEL       0x01                                /**< Physical channel number must be in the range 0..39. */
#define DTM_ERROR_INVALID_STATE         0x02                                /**< Sequencing error: Command is not valid now. */
#define DTM_ERROR_ILLEGAL_LENGTH        0x03                                /**< Payload size must be in the range 0..37. */
#define DTM_ERROR_ILLEGAL_CONFIGURATION 0x04                                /**< Parameter out of range (legal range is function dependent). */
#define DTM_ERROR_UNINITIALIZED         0x05                                /**< DTM module has not been initialized by the application. */


#define DTM_LE_DATA_PACKET_LEN_EXTENSION 0x02                               /**< DTM Status Response: LE Data Packet Length Extension feature supported. */
#define DTM_LE_2M_PHY                    0x04                               /**< DTM Status Response: LE 2M PHY supported. */
#define DTM_LE_STABLE_MODULATION_INDEX   0x08                               /**< DTM Status Response: Transmitter has a Stable Modulation Index. */
#define DTM_LE_CODED_PHY                 0x10                               /**< DTM Status Response: LE Coded PHY supported. */
#define DTM_LE_CONSTANT_TONE_EXTENSION   0x20                               /**< DTM Status Response: Constant Tone Extension Supported. */
#define DTM_LE_ANTENNA_SWITCH            0x40                               /**< DTM Status Response: Antenna switching supported. */
#define DTM_LE_AOD_1US_TANSMISSION       0x80                               /**< DTM Status Response: 1us switching supported for AoD tansmission. */
#define DTM_LE_AOD_1US_RECEPTION         0x100                              /**< DTM Status Response: 1us sampling supported for AoA reception. */
#define DTM_LE_AOA_1US_RECEPTION         0x200                              /**< DTM Status Response: 1us switching and sampling supported for AoA reception. */

/**@details The UART poll cycle in micro seconds.
 *          A baud rate of e.g. 19200 bits / second, and 8 data bits, 1 start/stop bit, no flow control,
 *          give the time to transmit a byte: 10 bits * 1/19200 = approx: 520 us.
 *          To ensure no loss of bytes, the UART should be polled every 260 us.
 */
#if DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud9600
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/9600/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud14400
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/14400/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud19200
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/19200/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud28800
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/28800/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud38400
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/38400/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud57600
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/57600/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud76800
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/768000/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud115200
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/115200/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud230400
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/230400/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud250000
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/250000/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud460800
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/460800/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud921600
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/921600/2))
#elif DTM_BITRATE == UARTE_BAUDRATE_BAUDRATE_Baud1M
#define UART_POLL_CYCLE ((uint32_t)(10*1e6/1e6/2))
#else
// It is possible to find values that work for other baud rates, but the formula above is not
// guaranteed to work for all values. Suitable values may have to be found by trial and error.
#error "Unsupported baud rate set."
#endif

// Note: DTM_PKT_VENDORSPECIFIC, is not a packet type
#define PACKET_TYPE_MAX                 DTM_PKT_0XFF                    /**< Highest value allowed as DTM Packet type. */

/** @brief BLE DTM event type. */
typedef uint32_t dtm_event_t;                                           /**< Type for handling DTM event. */

/** @brief BLE DTM frequency type. */
typedef uint32_t dtm_freq_t;                                            /**< Physical channel, valid range: 0..39. */

/**@brief BLE DTM packet types. */
typedef uint32_t dtm_pkt_type_t;                                        /**< Type for holding the requested DTM payload type.*/

/**@brief BLE DTM nRF21540 power mode. */
typedef enum
{
    NRF21540_POWER_MODE_A = 0x01,                                      /**< Set nRF21540 transmission power level to the A(20 dBm) predefinied value. */
    NRF21540_POWER_MODE_B = 0x02                                       /**< Set nRF21540 transmission power level to the B(10 dBm) predefinied value. */
} dtm_nrf21540_power_mode_t;


/**@brief Function for initializing or re-initializing DTM module
 *
 * @return DTM_SUCCESS on successful initialization of the DTM module.
*/
uint32_t dtm_init(void);

/**@brief Function for waiting the UART poll time. The wait time depends on a current UART baudrate.
 *        For default baudrate of 19200 will return to caller at 260us.
 *
 * @return      Time counter, incremented every UART poll time.
 */
uint32_t dtm_wait(void);


/**@brief Function for calling when a complete command has been prepared by the Tester.
 *
 * @param[in] cmd received 16-bit complete command from the Tester.
 *
 * @return    DTM_SUCCESS or one of the DTM_ERROR_ values
 */
uint32_t dtm_cmd(uint16_t cmd);


/**@brief Function for reading the result of a DTM command
 *
 * @param[out]  p_dtm_event   Pointer to buffer for 16 bit event code according to DTM standard.
 *
 * @return      true: new event, false: no event since last call, this event has been read earlier
 */
bool dtm_event_get(dtm_event_t * p_dtm_event);


/**@brief Function for configuring the timer to use.
 *
 * @note        Must be called when no DTM test is running.
 *
 * @param[in]   new_timer   Index (0..2) of timer to be used by the DTM library
 *
 * @return      true: success, new timer was selected, false: parameter error
 */
bool dtm_set_timer(uint32_t new_timer);


/**@brief Function for configuring the transmit power.
 *
 * @note        Must be called when no DTM test is running.
 *
 * @param[in]   new_tx_power   New output level, +4..-40, in steps of 4.
 *
 * @return      true: tx power setting changed, false: parameter error
 */
bool dtm_set_txpower(uint32_t new_tx_power);


/**@brief Function for choosing nRF21540 power level.
 *
 * @note        Must be called when no DTM test is running and nRF21540 is used.
 *
 * @param[in]   power_mode nRF21540 power mode.
 *
 * @return      true: tx power mode setting changed, false: parameter error
 */
bool dtm_set_nrf21450_power_mode(dtm_nrf21540_power_mode_t power_mode);


#ifdef __cplusplus
}
#endif

#endif // BLE_DTM_H__

/** @} */
