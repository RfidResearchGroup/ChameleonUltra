/*
 * bl_updater.c — bootloader self-update from the application.
 *
 * Entry points:
 *   bl_updater_run()                          replace BL, reset (CRC check)
 *   bl_updater_run_and_invalidate_app_force() replace BL, erase own vector
 *                                             table, reset — NO CRC check
 */

#include "bl_updater.h"
#include "embedded_bootloader.h"

#include <stdint.h>
#include <string.h>

#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_soc.h"
#include "nrf_delay.h"

/* This branch builds the bootloader at 0xF3000 (the stock 44KB region,
 * via bootloader-stage1.ld). Both the recovery build (embedding the STOCK
 * bootloader) and the normal build (embedding our custom bootloader) place
 * it at 0xF3000, so both rewind UICR to 0xF3000 to match. */
#ifdef RECOVERY_MODE
  #define BL_REGION_START    0x000F3000UL
  #define BL_REGION_END      0x000FE000UL
  #define UICR_BL_ADDR_STOCK 0x000F3000UL
#else
  #define BL_REGION_START    0x000F3000UL
  #define BL_REGION_END      0x000FE000UL
  #define UICR_BL_ADDR_STOCK 0x000F3000UL
#endif
#define BL_PAGE_SIZE       0x1000UL
#define BL_REGION_PAGES    ((BL_REGION_END - BL_REGION_START) / BL_PAGE_SIZE)
#define BL_REGION_BYTES    (BL_REGION_END - BL_REGION_START)
#define APP_REGION_START   0x00027000UL

#define UICR_BOOTLOADER_ADDR  0x10001014UL
#define UICR_PAGE_ADDR        0x10001000UL


/* ---- Inline NVMC ---- */

static inline void nvmc_wait_ready(void)
{
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

static void nvmc_page_erase(uint32_t page_addr)
{
    nvmc_wait_ready();
    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Een << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();
    if (page_addr == UICR_PAGE_ADDR) {
        /* CRITICAL: the UICR is NOT in the code-flash area, so ERASEPAGE does
         * not erase it on nRF52840 — it must be erased with ERASEUICR (which
         * clears the whole UICR page). Using ERASEPAGE here was a silent no-op:
         * the UICR stayed un-erased, so the restore-loop word writes only AND-ed
         * into existing values, corrupting NRFFW[0] (0xEB000 & 0xF3000 = 0xE3000)
         * and bricking the unit (MBR boots a garbage address). */
        NRF_NVMC->ERASEUICR = 1;
    } else {
        NRF_NVMC->ERASEPAGE = page_addr;
    }
    nvmc_wait_ready();
    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();
}

static void nvmc_write_word(uint32_t dst, uint32_t word)
{
    nvmc_wait_ready();
    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();
    *(volatile uint32_t *)dst = word;
    nvmc_wait_ready();
    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();
}

static void nvmc_write_bytes(uint32_t dst, const uint8_t *src, uint32_t len)
{
    nvmc_wait_ready();
    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();

    uint32_t remaining = len;
    while (remaining >= 4) {
        uint32_t word;
        memcpy(&word, src, 4);
        *(volatile uint32_t *)dst = word;
        nvmc_wait_ready();
        dst       += 4;
        src       += 4;
        remaining -= 4;
    }
    if (remaining != 0) {
        uint32_t word = 0xFFFFFFFFu;
        memcpy(&word, src, remaining);
        *(volatile uint32_t *)dst = word;
        nvmc_wait_ready();
    }

    NRF_NVMC->CONFIG = (NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos);
    nvmc_wait_ready();
}


/* CRC32 (zlib polynomial, same as Python zlib.crc32). */
static uint32_t crc32_compute(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}


bl_updater_status_t bl_updater_validate(void)
{
    if (EMBEDDED_BOOTLOADER_BIN_SIZE == 0u)
        return BL_UPDATER_ERR_EMPTY;
    if (EMBEDDED_BOOTLOADER_BIN_SIZE > BL_REGION_BYTES)
        return BL_UPDATER_ERR_TOO_LARGE;
    if (crc32_compute(EMBEDDED_BOOTLOADER_BIN, EMBEDDED_BOOTLOADER_BIN_SIZE)
        != EMBEDDED_BOOTLOADER_BIN_CRC32)
        return BL_UPDATER_ERR_CRC;
    return BL_UPDATER_OK;
}


static bl_updater_status_t bl_updater_flash_bl(bool validate_first)
{
    if (validate_first) {
        bl_updater_status_t st = bl_updater_validate();
        if (st != BL_UPDATER_OK) return st;
    } else {
        if (EMBEDDED_BOOTLOADER_BIN_SIZE == 0u)      return BL_UPDATER_ERR_EMPTY;
        if (EMBEDDED_BOOTLOADER_BIN_SIZE > BL_REGION_BYTES) return BL_UPDATER_ERR_TOO_LARGE;
    }

    if (nrf_sdh_is_enabled()) {
        ret_code_t err = nrf_sdh_disable_request();
        if (err != NRF_SUCCESS) return BL_UPDATER_ERR_SD_DISABLE;
        while (nrf_sdh_is_enabled()) {}
    }

    __disable_irq();

    for (uint32_t i = 0; i < BL_REGION_PAGES; i++)
        nvmc_page_erase(BL_REGION_START + i * BL_PAGE_SIZE);

    nvmc_write_bytes(BL_REGION_START,
                     EMBEDDED_BOOTLOADER_BIN,
                     EMBEDDED_BOOTLOADER_BIN_SIZE);

    if (memcmp((const void *)BL_REGION_START,
               EMBEDDED_BOOTLOADER_BIN,
               EMBEDDED_BOOTLOADER_BIN_SIZE) != 0)
        return BL_UPDATER_ERR_VERIFY;

    /* Ensure the UICR bootloader start address matches where we just wrote
     * the BL (0xF3000). UICR can only be written after a page erase; the
     * value only takes effect after a reset. Corrects any stale value
     * (e.g. 0xEB000) left by earlier experiments.
     *
     * CAUTION: the UICR page also holds REGOUT0 (0x10001304) and other config
     * (PSELRESET, NFCPINS, APPROTECT). On high-voltage-mode boards (battery ->
     * VDDH -> REG0 -> VDD) REGOUT0 sets the core/GPIO voltage; the firmware
     * never re-writes it, so a bare page-erase defaults it to 1.8V and bricks
     * battery-powered units (powers on, but no USB and no LEDs). Dev boards
     * feed VDD directly and are unaffected. So we back up the whole UICR page,
     * change only NRFFW[0], erase, and restore every other programmed word. */
    if (*(volatile uint32_t *)UICR_BOOTLOADER_ADDR != UICR_BL_ADDR_STOCK) {
        static uint32_t uicr_backup[256];   /* covers REGOUT0 @0x304 */
        volatile uint32_t *uicr = (volatile uint32_t *)UICR_PAGE_ADDR;
        for (uint32_t i = 0; i < 256; i++) uicr_backup[i] = uicr[i];
        uicr_backup[(UICR_BOOTLOADER_ADDR - UICR_PAGE_ADDR) / 4] = UICR_BL_ADDR_STOCK;

        nvmc_page_erase(UICR_PAGE_ADDR);
        for (uint32_t i = 0; i < 256; i++) {
            if (uicr_backup[i] != 0xFFFFFFFFUL) {
                nvmc_write_word(UICR_PAGE_ADDR + i * 4, uicr_backup[i]);
            }
        }
    }

    return BL_UPDATER_OK;
}


bl_updater_status_t bl_updater_run(void)
{
    bl_updater_status_t st = bl_updater_flash_bl(true);
    if (st != BL_UPDATER_OK) return st;
    nrf_delay_ms(50);
    NVIC_SystemReset();
    return BL_UPDATER_OK;
}

bl_updater_status_t bl_updater_run_and_invalidate_app_force(void)
{
    bl_updater_status_t st = bl_updater_flash_bl(false);
    if (st != BL_UPDATER_OK) return st;
    nvmc_page_erase(APP_REGION_START);
    nrf_delay_ms(50);
    NVIC_SystemReset();
    return BL_UPDATER_OK;
}
