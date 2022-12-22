/**
 * Copyright (c) 2020 - 2021, Nordic Semiconductor ASA
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
#ifndef NRF21540_DEFS_H_
#define NRF21540_DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Macros creating instance channels number dependent parameters.
 */
#define NRF21540_TIMER                  CONCAT_2(NRF_TIMER, NRF21540_TIMER_NO)
#define NRF21540_TIMER_IRQ_HANDLER      CONCAT_3(TIMER, NRF21540_TIMER_NO, _IRQHandler)
#define NRF21540_TIMER_IRQn             CONCAT_3(TIMER, NRF21540_TIMER_NO, _IRQn)
#define NRF21540_TIM_INTERRUPT_MASK     CONCAT_3(TIMER_INTENSET_COMPARE, \
                                                 NRF21540_TIMER_CC_PD_PG_CHANNEL_NO, _Msk)

#if NRF21540_TIMER_CC_START_TO_PDN_UP_CHANNEL_NO == NRF21540_TIMER_CC_PD_PG_CHANNEL_NO
#error These CC channels must be different
#endif

#if (NRF21540_PDN_PPI_CHANNEL_NO == NRF21540_USER_PPI_CHANNEL_NO) || \
    (NRF21540_PDN_PPI_CHANNEL_NO == NRF21540_TRX_PPI_CHANNEL_NO)  || \
    (NRF21540_TRX_PPI_CHANNEL_NO == NRF21540_USER_PPI_CHANNEL_NO)
#error These PPI channels must be different
#endif

#define NRF21540_USER_PPI_CHANNEL                        CONCAT_2(NRF_PPI_CHANNEL, NRF21540_USER_PPI_CHANNEL_NO)
#define NRF21540_PDN_PPI_CHANNEL                         CONCAT_2(NRF_PPI_CHANNEL, NRF21540_PDN_PPI_CHANNEL_NO)
#define NRF21540_TRX_PPI_CHANNEL                         CONCAT_2(NRF_PPI_CHANNEL, NRF21540_TRX_PPI_CHANNEL_NO)
#define NRF21540_USER_PPI_CHANNEL                        CONCAT_2(NRF_PPI_CHANNEL, NRF21540_USER_PPI_CHANNEL_NO)
#define NRF21540_PDN_PPI_CHANNEL                         CONCAT_2(NRF_PPI_CHANNEL, NRF21540_PDN_PPI_CHANNEL_NO)
#define NRF21540_TRX_PPI_CHANNEL                         CONCAT_2(NRF_PPI_CHANNEL, NRF21540_TRX_PPI_CHANNEL_NO)

#define NRF21540_TIMER_CC_FINISHED_CHANNEL_STOP_MASK     CONCAT_3(NRF_TIMER_SHORT_COMPARE, NRF21540_TIMER_CC_PD_PG_CHANNEL_NO, _STOP_MASK)
#define NRF21540_TIMER_CC_FINISHED_CHANNEL_CLEAR_MASK    CONCAT_3(NRF_TIMER_SHORT_COMPARE, NRF21540_TIMER_CC_PD_PG_CHANNEL_NO, _CLEAR_MASK)

#define NRF21540_TIMER_CC_START_TO_PDN_UP_CHANNEL        CONCAT_2(NRF_TIMER_CC_CHANNEL, NRF21540_TIMER_CC_START_TO_PDN_UP_CHANNEL_NO)
#define NRF21540_TIMER_CC_PD_PG_CHANNEL                  CONCAT_2(NRF_TIMER_CC_CHANNEL, NRF21540_TIMER_CC_PD_PG_CHANNEL_NO)
#define NRF21540_TIMER_CC_TRX_PG_CHANNEL                 NRF21540_TIMER_CC_PD_PG_CHANNEL

#define NRF21540_TIMER_CC_START_TO_PDN_UP_EVENT          CONCAT_2(NRF_TIMER_EVENT_COMPARE, NRF21540_TIMER_CC_START_TO_PDN_UP_CHANNEL_NO)
#define NRF21540_TIMER_CC_PD_PG_EVENT                    CONCAT_2(NRF_TIMER_EVENT_COMPARE, NRF21540_TIMER_CC_PD_PG_CHANNEL_NO)
#define NRF21540_TIMER_CC_TRX_PG_EVENT                   NRF21540_TIMER_CC_PD_PG_EVENT

#if (NRF21540_PDN_GPIOTE_CHANNEL_NO == NRF21540_PA_GPIOTE_CHANNEL_NO) ||   \
    (NRF21540_PDN_GPIOTE_CHANNEL_NO == NRF21540_LNA_GPIOTE_CHANNEL_NO)  || \
    (NRF21540_LNA_GPIOTE_CHANNEL_NO == NRF21540_PA_GPIOTE_CHANNEL_NO)
#error These GPIOTE channels must be different
#endif

#define NRF21540_PDN_GPIOTE_TASK_CLR      CONCAT_2(NRF_GPIOTE_TASKS_CLR_, NRF21540_PDN_GPIOTE_CHANNEL_NO)
#define NRF21540_LNA_GPIOTE_TASK_CLR      CONCAT_2(NRF_GPIOTE_TASKS_CLR_, NRF21540_LNA_GPIOTE_CHANNEL_NO)
#define NRF21540_PA_GPIOTE_TASK_CLR       CONCAT_2(NRF_GPIOTE_TASKS_CLR_, NRF21540_PA_GPIOTE_CHANNEL_NO)

#define NRF21540_PDN_GPIOTE_TASK_SET      CONCAT_2(NRF_GPIOTE_TASKS_SET_, NRF21540_PDN_GPIOTE_CHANNEL_NO)
#define NRF21540_LNA_GPIOTE_TASK_SET      CONCAT_2(NRF_GPIOTE_TASKS_SET_, NRF21540_LNA_GPIOTE_CHANNEL_NO)
#define NRF21540_PA_GPIOTE_TASK_SET       CONCAT_2(NRF_GPIOTE_TASKS_SET_, NRF21540_PA_GPIOTE_CHANNEL_NO)

#define NRF21540_GPIO_TASK_SET(channel)   CONCAT_2(NRF_GPIOTE_TASKS_SET_, channel)
#define NRF21540_GPIO_TASK_CLR(channel)   CONCAT_2(NRF_GPIOTE_TASKS_CLR_, channel)

#if !NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_IRQ_HANDLER      RADIO_IRQHandler
#define NRF21540_RADIO_IRQn             RADIO_IRQn
#define NRF21540_RADIO_READY_Msk        RADIO_INTENSET_READY_Msk
#define NRF21540_RADIO_EVENT_READY      NRF_RADIO_EVENT_READY
#define NRF21540_RADIO_DISABLED_Msk     RADIO_INTENSET_DISABLED_Msk
#define NRF21540_RADIO_EVENT_DISABLED   NRF_RADIO_EVENT_DISABLED
#else
#if (NRF21540_USER_PPI_CHANNEL_NO               == NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL_NO)    || \
    (NRF21540_PDN_PPI_CHANNEL_NO                == NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL_NO)    || \
    (NRF21540_TRX_PPI_CHANNEL_NO                == NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL_NO)    || \
    (NRF21540_USER_PPI_CHANNEL_NO               == NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL_NO) || \
    (NRF21540_PDN_PPI_CHANNEL_NO                == NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL_NO) || \
    (NRF21540_TRX_PPI_CHANNEL_NO                == NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL_NO) || \
    (NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL_NO == NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL_NO)
#error These PPI channels must be different
#endif

#if (NRF21540_RADIO_READY_EGU_CHANNEL_NO == NRF21540_RADIO_DISABLED_EGU_CHANNEL_NO)
#error These EGU channels must be different
#endif

#define NRF21540_EGU                     CONCAT_2(NRF_EGU, NRF21540_EGU_NO)
#define SWIx_EGU                         CONCAT_3(SWI, NRF21540_EGU_NO, _EGU)
#define NRF21540_RADIO_IRQ_HANDLER       CONCAT_3(SWIx_EGU, NRF21540_EGU_NO, _IRQHandler)
#define NRF21540_RADIO_IRQn              CONCAT_3(SWIx_EGU, NRF21540_EGU_NO, _IRQn)
#define NRF21540_RADIO_READY_Msk         (1 << NRF21540_RADIO_READY_EGU_CHANNEL_NO)
#define NRF21540_RADIO_DISABLED_Msk      (1 << NRF21540_RADIO_DISABLED_EGU_CHANNEL_NO)
#define NRF21540_RADIO_EVENT_READY       CONCAT_2(NRF_EGU_EVENT_TRIGGERED, NRF21540_RADIO_READY_EGU_CHANNEL_NO)
#define NRF21540_RADIO_EVENT_DISABLED    CONCAT_2(NRF_EGU_EVENT_TRIGGERED, NRF21540_RADIO_DISABLED_EGU_CHANNEL_NO)
#define NRF21540_RADIO_READY_EGU_TASK    CONCAT_2(NRF_EGU_TASK_TRIGGER, NRF21540_RADIO_READY_EGU_CHANNEL_NO)
#define NRF21540_RADIO_DISABLED_EGU_TASK CONCAT_2(NRF_EGU_TASK_TRIGGER, NRF21540_RADIO_DISABLED_EGU_CHANNEL_NO)

#define NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL     CONCAT_2(NRF_PPI_CHANNEL, NRF21540_RADIO_READY_TO_EGU_PPI_CHANNEL_NO)
#define NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL  CONCAT_2(NRF_PPI_CHANNEL, NRF21540_RADIO_DISABLED_TO_EGU_PPI_CHANNEL_NO)
#endif //!NRF21540_DO_NOT_USE_NATIVE_RADIO_IRQ_HANDLER
#define NRF21540_RADIO_INTERRUPT_MASK   (NRF21540_RADIO_READY_Msk | NRF21540_RADIO_DISABLED_Msk)

/**@brief Time in microseconds when PA GPIO is activated before the radio is ready for
 *        transmission.
 */
#define NRF21540_PA_PG_TRX_TIME_US      13

/**@brief Time in microseconds when LNA GPIO is activated before the radio is ready for
 *        reception.
 */
#define NRF21540_LNA_PG_TRX_TIME_US     13

/**@brief The time between activating the PDN and asserting the RX_EN/TX_EN.
*/
#define NRF21540_PD_PG_TIME_US          18

/**@brief The time between deasserting the RX_EN/TX_EN and deactivating PDN.
*/
#define NRF21540_TRX_PG_TIME_US            5

/**@brief Timing definitions for radio peripheral on nRF uc.
 */
#define TX_FAST_RAMP_UP_TIME              40  ///< Radio fast ramp up time in us for tx
#define RX_FAST_RAMP_UP_TIME              40  ///< Radio fast ramp up time in us for rx
#define TX_RAMP_UP_TIME                   130 ///< Radio normal ramp up time in us for tx
#define RX_RAMP_UP_TIME                   130 ///< Radio normal ramp up time in us for rx

#if (TX_RAMP_UP_TIME == RX_RAMP_UP_TIME && TX_FAST_RAMP_UP_TIME == RX_FAST_RAMP_UP_TIME)
#define FAST_RAMP_UP_TIME                 TX_FAST_RAMP_UP_TIME
#define RAMP_UP_TIME                      TX_RAMP_UP_TIME
#else
#error ramp up times for rx and tx direction are different. Driver needs rework
#endif

#if (FAST_RAMP_UP_TIME < (NRF21540_PA_PG_TRX_TIME_US + NRF21540_PD_PG_TIME_US))
#error fast ramp up time must be greater or equal than (TPD->PG + TPG->TRX)
#endif

#if (FAST_RAMP_UP_TIME > RAMP_UP_TIME)
#error fast ramp up time connot be greater than ramp up time
#endif

#ifdef __cplusplus
}
#endif

#endif  // NRF21540_DEFS_H_
