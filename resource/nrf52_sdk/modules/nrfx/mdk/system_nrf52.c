/*

Copyright (c) 2009-2021 ARM Limited. All rights reserved.

    SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the License); you may
not use this file except in compliance with the License.
You may obtain a copy of the License at

    www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an AS IS BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

NOTICE: This file has been modified by Nordic Semiconductor ASA.

*/

/* NOTE: Template files (including this one) are application specific and therefore expected to
   be copied into the application project folder prior to its use! */

#include <stdint.h>
#include <stdbool.h>
#include "nrf.h"
#include "nrf_peripherals.h"
#include "nrf_erratas.h"
#include "system_nrf52.h"
#include "system_nrf52_approtect.h"

#define __SYSTEM_CLOCK_64M      (64000000UL)


#if defined ( __CC_ARM )
    uint32_t SystemCoreClock __attribute__((used)) = __SYSTEM_CLOCK_64M;
#elif defined ( __ICCARM__ )
    __root uint32_t SystemCoreClock = __SYSTEM_CLOCK_64M;
#elif defined ( __GNUC__ )
    uint32_t SystemCoreClock __attribute__((used)) = __SYSTEM_CLOCK_64M;
#endif

/* Select correct reset pin */
/* Handle DEVELOP_IN-targets first as they take precedence over the later macros */
#if    defined (DEVELOP_IN_NRF52805) \
    || defined (DEVELOP_IN_NRF52810) \
    || defined (DEVELOP_IN_NRF52811) \
    || defined (DEVELOP_IN_NRF52832)
    #define RESET_PIN 21
#elif  defined (DEVELOP_IN_NRF52820) \
    || defined (DEVELOP_IN_NRF52833) \
    || defined (DEVELOP_IN_NRF52840)
    #define RESET_PIN 18
#elif  defined (NRF52805_XXAA) \
    || defined (NRF52810_XXAA) \
    || defined (NRF52811_XXAA) \
    || defined (NRF52832_XXAA) \
    || defined (NRF52832_XXAB)
    #define RESET_PIN 21
#elif  defined (NRF52820_XXAA) \
    || defined (NRF52833_XXAA) \
    || defined (NRF52840_XXAA)
    #define RESET_PIN 18
#else
    #error "A supported device macro must be defined."
#endif

/* -- NVMC utility functions -- */
/* Waits until NVMC is done with the current pending action */
void nvmc_wait(void)
{
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
}

/*  Configure the NVMC to "mode".
    Mode must be an enumerator of field NVMC_CONFIG_WEN */
void nvmc_config(uint32_t mode)
{
    NRF_NVMC->CONFIG = mode << NVMC_CONFIG_WEN_Pos;
    nvmc_wait();
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = __SYSTEM_CLOCK_64M;
}

void SystemInit(void)
{
    /* Enable SWO trace functionality. If ENABLE_SWO is not defined, SWO pin will be used as GPIO (see Product
       Specification to see which one). */
    #if defined (ENABLE_SWO) && defined(CLOCK_TRACECONFIG_TRACEMUX_Pos)
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        NRF_CLOCK->TRACECONFIG |= CLOCK_TRACECONFIG_TRACEMUX_Serial << CLOCK_TRACECONFIG_TRACEMUX_Pos;
        NRF_P0->PIN_CNF[18] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
    #endif

    /* Enable Trace functionality. If ENABLE_TRACE is not defined, TRACE pins will be used as GPIOs (see Product
       Specification to see which ones). */
    #if defined (ENABLE_TRACE) && defined(CLOCK_TRACECONFIG_TRACEMUX_Pos)
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        NRF_CLOCK->TRACECONFIG |= CLOCK_TRACECONFIG_TRACEMUX_Parallel << CLOCK_TRACECONFIG_TRACEMUX_Pos;
        NRF_P0->PIN_CNF[14] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        NRF_P0->PIN_CNF[15] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        NRF_P0->PIN_CNF[16] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        NRF_P0->PIN_CNF[18] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        NRF_P0->PIN_CNF[20] = (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
    #endif

    #if NRF52_ERRATA_12_ENABLE_WORKAROUND
        /* Workaround for Errata 12 "COMP: Reference ladder not correctly calibrated" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp */
        if (nrf52_errata_12()){
            *(volatile uint32_t *)0x40013540 = (*(uint32_t *)0x10000324 & 0x00001F00) >> 8;
        }
    #endif

    #if NRF52_ERRATA_16_ENABLE_WORKAROUND
        /* Workaround for Errata 16 "System: RAM may be corrupt on wakeup from CPU IDLE" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp */
        if (nrf52_errata_16()){
            *(volatile uint32_t *)0x4007C074 = 3131961357ul;
        }
    #endif

    #if NRF52_ERRATA_31_ENABLE_WORKAROUND
        /* Workaround for Errata 31 "CLOCK: Calibration values are not correctly loaded from FICR at reset" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp */
        if (nrf52_errata_31()){
            *(volatile uint32_t *)0x4000053C = ((*(volatile uint32_t *)0x10000244) & 0x0000E000) >> 13;
        }
    #endif

    #if NRF52_ERRATA_32_ENABLE_WORKAROUND
        /* Workaround for Errata 32 "DIF: Debug session automatically enables TracePort pins" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp */
        if (nrf52_errata_32()){
            CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
        }
    #endif

    #if NRF52_ERRATA_36_ENABLE_WORKAROUND
        /* Workaround for Errata 36 "CLOCK: Some registers are not reset when expected" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_36()){
            NRF_CLOCK->EVENTS_DONE = 0;
            NRF_CLOCK->EVENTS_CTTO = 0;
            NRF_CLOCK->CTIV = 0;
        }
    #endif

    #if NRF52_ERRATA_37_ENABLE_WORKAROUND
        /* Workaround for Errata 37 "RADIO: Encryption engine is slow by default" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_37()){
            *(volatile uint32_t *)0x400005A0 = 0x3;
        }
    #endif

    #if NRF52_ERRATA_57_ENABLE_WORKAROUND
        /* Workaround for Errata 57 "NFCT: NFC Modulation amplitude" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_57()){
            *(volatile uint32_t *)0x40005610 = 0x00000005;
            *(volatile uint32_t *)0x40005688 = 0x00000001;
            *(volatile uint32_t *)0x40005618 = 0x00000000;
            *(volatile uint32_t *)0x40005614 = 0x0000003F;
        }
    #endif

    #if NRF52_ERRATA_66_ENABLE_WORKAROUND
        /* Workaround for Errata 66 "TEMP: Linearity specification not met with default settings" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_66()){
            NRF_TEMP->A0 = NRF_FICR->TEMP.A0;
            NRF_TEMP->A1 = NRF_FICR->TEMP.A1;
            NRF_TEMP->A2 = NRF_FICR->TEMP.A2;
            NRF_TEMP->A3 = NRF_FICR->TEMP.A3;
            NRF_TEMP->A4 = NRF_FICR->TEMP.A4;
            NRF_TEMP->A5 = NRF_FICR->TEMP.A5;
            NRF_TEMP->B0 = NRF_FICR->TEMP.B0;
            NRF_TEMP->B1 = NRF_FICR->TEMP.B1;
            NRF_TEMP->B2 = NRF_FICR->TEMP.B2;
            NRF_TEMP->B3 = NRF_FICR->TEMP.B3;
            NRF_TEMP->B4 = NRF_FICR->TEMP.B4;
            NRF_TEMP->B5 = NRF_FICR->TEMP.B5;
            NRF_TEMP->T0 = NRF_FICR->TEMP.T0;
            NRF_TEMP->T1 = NRF_FICR->TEMP.T1;
            NRF_TEMP->T2 = NRF_FICR->TEMP.T2;
            NRF_TEMP->T3 = NRF_FICR->TEMP.T3;
            NRF_TEMP->T4 = NRF_FICR->TEMP.T4;
        }
    #endif

    #if NRF52_ERRATA_98_ENABLE_WORKAROUND
        /* Workaround for Errata 98 "NFCT: Not able to communicate with the peer" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_98()){
            *(volatile uint32_t *)0x4000568Cul = 0x00038148ul;
        }
    #endif

    #if NRF52_ERRATA_103_ENABLE_WORKAROUND && defined(CCM_MAXPACKETSIZE_MAXPACKETSIZE_Pos)
        /* Workaround for Errata 103 "CCM: Wrong reset value of CCM MAXPACKETSIZE" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_103()){
            NRF_CCM->MAXPACKETSIZE = 0xFBul;
        }
    #endif

    #if NRF52_ERRATA_108_ENABLE_WORKAROUND
        /* Workaround for Errata 108 "RAM: RAM content cannot be trusted upon waking up from System ON Idle or System OFF mode" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_108()){
            *(volatile uint32_t *)0x40000EE4ul = *(volatile uint32_t *)0x10000258ul & 0x0000004Ful;
        }
    #endif

    #if NRF52_ERRATA_115_ENABLE_WORKAROUND
        /* Workaround for Errata 115 "RAM: RAM content cannot be trusted upon waking up from System ON Idle or System OFF mode" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_115()){
            *(volatile uint32_t *)0x40000EE4 = (*(volatile uint32_t *)0x40000EE4 & 0xFFFFFFF0) | (*(uint32_t *)0x10000258 & 0x0000000F);
        }
    #endif

    #if NRF52_ERRATA_120_ENABLE_WORKAROUND
        /* Workaround for Errata 120 "QSPI: Data read or written is corrupted" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_120()){
            *(volatile uint32_t *)0x40029640ul = 0x200ul;
        }
    #endif

    #if NRF52_ERRATA_136_ENABLE_WORKAROUND
        /* Workaround for Errata 136 "System: Bits in RESETREAS are set when they should not be" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_136()){
            if (NRF_POWER->RESETREAS & POWER_RESETREAS_RESETPIN_Msk){
                NRF_POWER->RESETREAS =  ~POWER_RESETREAS_RESETPIN_Msk;
            }
        }
    #endif

    #if NRF52_ERRATA_182_ENABLE_WORKAROUND
        /* Workaround for Errata 182 "RADIO: Fixes for anomalies #102, #106, and #107 do not take effect" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_182()){
            *(volatile uint32_t *) 0x4000173C |= (0x1 << 10);
        }
    #endif

    #if NRF52_ERRATA_217_ENABLE_WORKAROUND
        /* Workaround for Errata 217 "RAM: RAM content cannot be trusted upon waking up from System ON Idle or System OFF mode" found at the Errata document
           for your device located at https://infocenter.nordicsemi.com/index.jsp  */
        if (nrf52_errata_217()){
            *(volatile uint32_t *)0x40000EE4ul |= 0x0000000Ful;
        }
    #endif

    /* Enable the FPU if the compiler used floating point unit instructions. __FPU_USED is a MACRO defined by the
     * compiler. Since the FPU consumes energy, remember to disable FPU use in the compiler if floating point unit
     * operations are not used in your code. */
    #if (__FPU_USED == 1)
        SCB->CPACR |= (3UL << 20) | (3UL << 22);
        __DSB();
        __ISB();
    #endif

    nrf52_handle_approtect();

    /* Configure NFCT pins as GPIOs if NFCT is not to be used in your code. If CONFIG_NFCT_PINS_AS_GPIOS is not defined,
       two GPIOs (see Product Specification to see which ones) will be reserved for NFC and will not be available as
       normal GPIOs. */
    #if defined (CONFIG_NFCT_PINS_AS_GPIOS) && defined(NFCT_PRESENT)
        if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)){
            nvmc_config(NVMC_CONFIG_WEN_Wen);
            NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
            nvmc_wait();
            nvmc_config(NVMC_CONFIG_WEN_Ren);
            NVIC_SystemReset();
        }
    #endif

    /* Configure GPIO pads as pPin Reset pin if Pin Reset capabilities desired. If CONFIG_GPIO_AS_PINRESET is not
      defined, pin reset will not be available. One GPIO (see Product Specification to see which one) will then be
      reserved for PinReset and not available as normal GPIO. */
    #if defined (CONFIG_GPIO_AS_PINRESET)
        if (((NRF_UICR->PSELRESET[0] & UICR_PSELRESET_CONNECT_Msk) != (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos)) ||
            ((NRF_UICR->PSELRESET[1] & UICR_PSELRESET_CONNECT_Msk) != (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos))){
            nvmc_config(NVMC_CONFIG_WEN_Wen);
            NRF_UICR->PSELRESET[0] = RESET_PIN;
            nvmc_wait();
            NRF_UICR->PSELRESET[1] = RESET_PIN;
            nvmc_wait();
            nvmc_config(NVMC_CONFIG_WEN_Ren);
            NVIC_SystemReset();
        }
    #endif

    /* When developing for nRF52810 on an nRF52832, or nRF52811 on an nRF52840,
        make sure NFC pins are mapped as GPIO. */
    #if    defined (DEVELOP_IN_NRF52832) && defined(NRF52810_XXAA) \
        || defined (DEVELOP_IN_NRF52840) && defined(NRF52811_XXAA)
        if ((*((uint32_t *)0x1000120C) & (1 << 0)) != 0){
            nvmc_config(NVMC_CONFIG_WEN_Wen);
            *((uint32_t *)0x1000120C) = 0;
            nvmc_wait();
            nvmc_config(NVMC_CONFIG_WEN_Ren);
            NVIC_SystemReset();
        }
    #endif

    SystemCoreClockUpdate();
}
