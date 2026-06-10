/*
 * bl_updater.c — bootloader self-update from the application.
 *
 * Entry points:
 *   bl_updater_run()                          replace BL, reset (CRC check)
 *   bl_updater_run_and_invalidate_app()       replace BL, erase own VT, reset (CRC check)
 *   bl_updater_run_and_invalidate_app_force() same, NO CRC check
 *   bl_updater_stage_and_reset_force()        stage BL for bootloader-side apply, NO CRC check
 *
 * The _stage_ variant exists because the bootloader sets ACL flash
 * protection on its own region before jumping to the app, and that
 * protection persists across soft resets.  Direct flash writes from the
 * app are silently ignored.  The staging approach writes the new BL into
 * the unprotected app region, then resets.  On the next boot the
 * bootloader detects the staged image (before ACL is set), copies a
 * tiny erase+write function to RAM, and applies it there — the only
 * safe way to replace the currently-executing bootloader flash.
 */

#include "bl_updater.h"
#include "embedded_bootloader.h"

#include <stdint.h>
#include <string.h>

#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_soc.h"
#include "nrf_delay.h"

/* In recovery mode the embedded BL is the STOCK bootloader, which must
 * be written to its stock address (0xF3000), and the UICR + MBR params
 * must be rewound to match. In normal mode the embedded BL is our UF2
 * bootloader at 0xEB000. */
#ifdef RECOVERY_MODE
  #define BL_REGION_START    0x000F3000UL
  #define BL_REGION_END      0x000FE000UL
  #define UICR_BL_ADDR_STOCK 0x000F3000UL
#else
  #define BL_REGION_START    0x000EB000UL
  #define BL_REGION_END      0x000FE000UL
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
    NRF_NVMC->ERASEPAGE = page_addr;
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

#ifdef RECOVERY_MODE
    /* Rewind the UICR bootloader start address to the stock location.
     * UICR can only be written after a page erase; the value only takes
     * effect after a reset. */
    if (*(volatile uint32_t *)UICR_BOOTLOADER_ADDR != UICR_BL_ADDR_STOCK) {
        nvmc_page_erase(UICR_PAGE_ADDR);
        nvmc_write_word(UICR_BOOTLOADER_ADDR, UICR_BL_ADDR_STOCK);
    }
#endif

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

bl_updater_status_t bl_updater_run_and_invalidate_app(void)
{
    bl_updater_status_t st = bl_updater_flash_bl(true);
    if (st != BL_UPDATER_OK) return st;
    nvmc_page_erase(APP_REGION_START);
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


/* ---- Staged update ---- */

bl_updater_status_t bl_updater_stage_and_reset_force(void)
{
    if (EMBEDDED_BOOTLOADER_BIN_SIZE == 0u)
        return BL_UPDATER_ERR_EMPTY;
    if (EMBEDDED_BOOTLOADER_BIN_SIZE > BL_REGION_BYTES)
        return BL_UPDATER_ERR_TOO_LARGE;

    if (nrf_sdh_is_enabled()) {
        ret_code_t err = nrf_sdh_disable_request();
        if (err != NRF_SUCCESS) return BL_UPDATER_ERR_SD_DISABLE;
        while (nrf_sdh_is_enabled()) {}
    }

    __disable_irq();

    /* Erase the staging area (12 pages from BL_STAGED_BASE). */
    for (uint32_t i = 0; i < BL_STAGED_PAGES; i++)
        nvmc_page_erase(BL_STAGED_BASE + i * BL_STAGED_PAGE_SZ);

    /* Write header: size first so a torn write of the magic is detectable. */
    const uint32_t bl_size = EMBEDDED_BOOTLOADER_BIN_SIZE;
    nvmc_write_bytes(BL_STAGED_SIZE_ADDR,
                     (const uint8_t *)&bl_size,
                     sizeof(uint32_t));
    nvmc_write_bytes(BL_STAGED_DATA_ADDR,
                     EMBEDDED_BOOTLOADER_BIN,
                     EMBEDDED_BOOTLOADER_BIN_SIZE);
    /* Write magic last — bootloader treats this as the commit point. */
    static const uint32_t magic = BL_STAGED_MAGIC_VAL;
    nvmc_write_bytes(BL_STAGED_MAGIC_ADDR,
                     (const uint8_t *)&magic,
                     sizeof(uint32_t));

    /* Self-destruct: erase own vector table so whichever bootloader
     * runs next (UF2 or stock) finds no valid app. */
    nvmc_page_erase(APP_REGION_START);

    nrf_delay_ms(50);
    NVIC_SystemReset();
    return BL_UPDATER_OK; /* unreached */
}
