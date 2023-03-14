/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
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
#include "nrf_bootloader_info.h"
#include "nrf_dfu_types.h"
#include "nrf_nvmc.h"

#define UICR_BOOTLOADER_ADDR 0x10001014

/** @brief  This variable ensures that the linker script will write the bootloader start address
 *          to the UICR register. This value will be written in the HEX file and thus written to
 *          UICR when the bootloader is flashed into the chip.
 */
#if defined (__CC_ARM )
    #pragma push
    #pragma diag_suppress 1296
    uint32_t  m_uicr_bootloader_start_address __attribute__((at(UICR_BOOTLOADER_ADDR)))
                                                    = BOOTLOADER_START_ADDR;
    #pragma pop
#elif defined ( __GNUC__ ) || defined ( __SES_ARM )
    volatile uint32_t m_uicr_bootloader_start_address  __attribute__ ((section(".uicr_bootloader_start_address")))
                                            = BOOTLOADER_START_ADDR;
#elif defined ( __ICCARM__ )
    __root    const uint32_t m_uicr_bootloader_start_address @ UICR_BOOTLOADER_ADDR
                                            = BOOTLOADER_START_ADDR;
#endif

void nrf_bootloader_mbr_addrs_populate(void)
{
    if (*(const uint32_t *)MBR_BOOTLOADER_ADDR == 0xFFFFFFFF)
    {
        nrf_nvmc_write_word(MBR_BOOTLOADER_ADDR, BOOTLOADER_START_ADDR);
    }
    if (*(const uint32_t *)MBR_PARAM_PAGE_ADDR == 0xFFFFFFFF)
    {
        nrf_nvmc_write_word(MBR_PARAM_PAGE_ADDR, NRF_MBR_PARAMS_PAGE_ADDRESS);
    }
}


void nrf_bootloader_debug_port_disable(void)
{
    if (NRF_UICR->APPROTECT != 0x0)
    {
        nrf_nvmc_write_word((uint32_t)&NRF_UICR->APPROTECT, 0x0);
        NVIC_SystemReset();
    }
#if (!defined (NRF52810_XXAA) && !defined (NRF52811_XXAA) && !defined (NRF52832_XXAA) && !defined (NRF52832_XXAB))
    if (NRF_UICR->DEBUGCTRL != 0x0)
    {
        nrf_nvmc_write_word((uint32_t)&NRF_UICR->DEBUGCTRL, 0x0);
        NVIC_SystemReset();
    }
#endif
}
