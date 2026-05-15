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
#include "sdk_config.h"
#include "nrf_bootloader_app_start.h"
#include <stdint.h>
#include "nrf.h"
#include "nrf_peripherals.h"
#include "nrf_bootloader_info.h"
#include "nrf_dfu_types.h"
#include "nrf_dfu_utils.h"
#include "nrf_dfu_settings.h"
#include "nrf_assert.h"
#include "nrf_log.h"
#include "sdk_config.h"


#define HANDLER_MODE_EXIT 0xFFFFFFF9 // When this is jumped to, the CPU will exit interrupt context
                                     // (handler mode), and pop values from the stack into registers.
                                     // See ARM's documentation for "Exception entry and return".
#define EXCEPTION_STACK_WORD_COUNT 8 // The number of words popped from the stack when
                                     // HANDLER_MODE_EXIT is branched to.


/**@brief Function that sets the stack pointer and starts executing a particular address.
 *
 * @param[in]  new_msp  The new value to set in the main stack pointer.
 * @param[in]  addr     The address to execute.
 */
void jump_to_addr(uint32_t new_msp, uint32_t addr)
{
    __set_MSP(new_msp);
    ((void (*)(void))addr)();
}


/**@brief Function for booting an app as if the chip was reset.
 *
 * @param[in]  vector_table_addr  The address of the app's vector table.
 */
__STATIC_INLINE void app_start(uint32_t vector_table_addr)
{
    const uint32_t current_isr_num = (__get_IPSR() & IPSR_ISR_Msk);
    const uint32_t new_msp         = *((uint32_t *)(vector_table_addr));                    // The app's Stack Pointer is found as the first word of the vector table.
    const uint32_t reset_handler   = *((uint32_t *)(vector_table_addr + sizeof(uint32_t))); // The app's Reset Handler is found as the second word of the vector table.

    __set_CONTROL(0x00000000);   // Set CONTROL to its reset value 0.
    __set_PRIMASK(0x00000000);   // Set PRIMASK to its reset value 0.
    __set_BASEPRI(0x00000000);   // Set BASEPRI to its reset value 0.
    __set_FAULTMASK(0x00000000); // Set FAULTMASK to its reset value 0.

    ASSERT(current_isr_num == 0); // If this is triggered, the CPU is currently in an interrupt.

    // The CPU is in Thread mode (main context).
    jump_to_addr(new_msp, reset_handler); // Jump directly to the App's Reset Handler.
}


ret_code_t nrf_bootloader_flash_protect(uint32_t address, uint32_t size)
{
    if ((size & (CODE_PAGE_SIZE - 1)) || (address > BOOTLOADER_SETTINGS_ADDRESS))
    {
        return NRF_ERROR_INVALID_PARAM;
    }

#if defined(ACL_PRESENT)

    // Protect using ACL.
    static uint32_t acl_instance = 0;

    uint32_t const mask   = (ACL_ACL_PERM_WRITE_Disable << ACL_ACL_PERM_WRITE_Pos);

    if (acl_instance >= ACL_REGIONS_COUNT)
    {
        return NRF_ERROR_NO_MEM;
    }

    NRF_ACL->ACL[acl_instance].ADDR = address;
    NRF_ACL->ACL[acl_instance].SIZE = size;
    NRF_ACL->ACL[acl_instance].PERM = mask;

    acl_instance++;

#elif defined (BPROT_PRESENT)

    // Protect using BPROT. BPROT does not support read protection.
    uint32_t pagenum_start = address / CODE_PAGE_SIZE;
    uint32_t pagenum_end   = pagenum_start + ((size - 1) / CODE_PAGE_SIZE);

    for (uint32_t i = pagenum_start; i <= pagenum_end; i++)
    {
        uint32_t config_index = i / 32;
        uint32_t mask         = (1 << (i - config_index * 32));

        switch (config_index)
        {
            case 0:
                NRF_BPROT->CONFIG0 = mask;
                break;
            case 1:
                NRF_BPROT->CONFIG1 = mask;
                break;
#if BPROT_REGIONS_NUM > 64
            case 2:
                NRF_BPROT->CONFIG2 = mask;
                break;
            case 3:
                NRF_BPROT->CONFIG3 = mask;
                break;
#endif
        }
    }

#endif

    return NRF_SUCCESS;
}


void nrf_bootloader_app_start_final(uint32_t vector_table_addr)
{
    ret_code_t ret_val;

    // Size of the flash area to protect.
    uint32_t area_size;

    area_size = BOOTLOADER_SIZE + NRF_MBR_PARAMS_PAGE_SIZE;
    if (!NRF_BL_DFU_ALLOW_UPDATE_FROM_APP && !NRF_BL_DFU_ENTER_METHOD_BUTTONLESS && !NRF_DFU_TRANSPORT_BLE)
    {
        area_size += BOOTLOADER_SETTINGS_PAGE_SIZE;
    }

    ret_val = nrf_bootloader_flash_protect(BOOTLOADER_START_ADDR, area_size);

    if (ret_val != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Could not protect bootloader and settings pages, 0x%x.", ret_val);
    }
    APP_ERROR_CHECK(ret_val);

    ret_val = nrf_bootloader_flash_protect(0,
                    nrf_dfu_bank0_start_addr() + ALIGN_TO_PAGE(s_dfu_settings.bank_0.image_size));

    if (ret_val != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Could not protect SoftDevice and application, 0x%x.", ret_val);
    }
    APP_ERROR_CHECK(ret_val);

    // Run application
    app_start(vector_table_addr);
}
