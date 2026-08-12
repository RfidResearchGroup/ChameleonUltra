/*
 * nrf_dfu_uf2.c — UF2 (MSC) DFU transport, composite with CDC serial DFU.
 *
 * Single bootloader at 0xF3000 (44KB). The CDC serial DFU transport
 * (nrf_dfu_serial_usb.c) owns the USBD stack and calls the weak
 * usb_dfu_transport_class_register() hook, where we append MSC. Result:
 * a composite CDC(DFU) interfaces 0+1 + MSC(UF2) interface 2 device.
 *
 * No debug CDC, no logging — keeps the bootloader within 44KB.
 *
 * Memory layout:
 *     0x00000 - 0x01000   MBR
 *     0x01000 - 0x27000   SoftDevice S140
 *     0x27000 - 0xF3000   Application      <- UF2 writes go here
 *     0xF3000 - 0xFE000   Bootloader (44KB)
 *     0xFE000 - 0xFF000   MBR params
 *     0xFF000 - 0x100000  Bootloader settings
 */

#include <string.h>

#include "nrf_dfu_transport.h"
#include "nrf_dfu_settings.h"
#include "nrf_dfu_types.h"
#include "nrf_bootloader_info.h"
#include "nrf_nvmc.h"
#include "nrf_delay.h"
#include "app_usbd.h"
#include "app_usbd_core.h"
#include "app_usbd_msc.h"
#include "app_usbd_string_desc.h"
#include "app_usbd_serial_num.h"
#include "app_scheduler.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"

#define NRF_LOG_MODULE_NAME nrf_dfu_uf2
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#include "crc32.h"
#include "uf2_ghostfat.h"
#include "uf2_blockdev.h"

/* Composite: CDC DFU owns interfaces 0+1 / EP1-2. MSC is interface 2 / EP3. */
#define MSC_INTERFACE        2
#define MSC_EPIN             NRF_DRV_USBD_EPIN3
#define MSC_EPOUT            NRF_DRV_USBD_EPOUT3
#define MSC_WORKBUFFER_SIZE  512

static nrf_dfu_observer_t m_observer;

static void msc_user_ev_handler(app_usbd_class_inst_t const *p_inst,
                                app_usbd_msc_user_event_t event)
{
    (void)p_inst; (void)event;
}

APP_USBD_MSC_GLOBAL_DEF(m_app_msc,
                        MSC_INTERFACE,
                        msc_user_ev_handler,
                        (MSC_EPIN, MSC_EPOUT),
                        (&uf2_blockdev),
                        MSC_WORKBUFFER_SIZE);

/* ---- ghostfat -> flash glue ---- */

void uf2_flash_read(uint32_t addr, void *buf, uint32_t len)
{
    if (addr >= UF2_FLASH_APP_START && addr + len <= UF2_FLASH_APP_END) {
        memcpy(buf, (const void *)addr, len);
    } else {
        memset(buf, 0xFF, len);
    }
}

/* Called by ghostfat on block rejection to reset the inactivity timer. */
void uf2_ping_observer(void)
{
    if (m_observer) m_observer(NRF_DFU_EVT_OBJECT_RECEIVED);
}

bool uf2_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    if (addr < UF2_FLASH_APP_START || addr + len > UF2_FLASH_APP_END) {
        NRF_LOG_WARNING("UF2 write outside app region 0x%08x", addr);
        return false;
    }

    const uint32_t page = NRF_FICR->CODEPAGESIZE;
    if ((addr & (page - 1)) == 0) {
        nrf_nvmc_page_erase(addr);
        const uint32_t *p = (const uint32_t *)addr;
        for (uint32_t i = 0; i < page / 4; i++) {
            if (p[i] != 0xFFFFFFFF) return false;
        }
    }
    nrf_nvmc_write_bytes(addr, (const uint8_t *)data, len);

    if (m_observer) m_observer(NRF_DFU_EVT_OBJECT_RECEIVED);
    return true;
}

/* ---- DFU completion ---- */

void uf2_dfu_complete(void)
{
    NRF_LOG_INFO("UF2 transfer complete (%u blocks)",
                 uf2_ghostfat_blocks_written());

    s_dfu_settings.bank_0.bank_code  = NRF_DFU_BANK_VALID_APP;
    s_dfu_settings.bank_0.image_size = uf2_ghostfat_blocks_written() * 256u;
    s_dfu_settings.bank_0.image_crc  = 0;

    /* UF2 images are unsigned and carry no app CRC, so mark the app as needing
     * no boot-time validation. Otherwise a stale VALIDATE_CRC left in the
     * settings by an earlier serial-DFU flash makes app_is_valid() recompute
     * the app CRC and mismatch on any CRC-required boot (e.g. an NFC/LPCOMP
     * field wake, or the LF-field-at-sleep soft reset), dropping the device
     * into DFU instead of the app. Refresh boot_validation_crc too, so the
     * backup settings page stays consistent (as nrf_dfu_settings_write does). */
    s_dfu_settings.boot_validation_app.type = NO_VALIDATION;
    s_dfu_settings.boot_validation_crc = crc32_compute(
        (const uint8_t *)&s_dfu_settings.boot_validation_softdevice,
        3u * sizeof(boot_validation_t),
        NULL);

    s_dfu_settings.crc = crc32_compute(
        (uint8_t const *)&s_dfu_settings + 4,
        offsetof(nrf_dfu_settings_t, init_command) - 4,
        NULL);

    nrf_nvmc_page_erase(BOOTLOADER_SETTINGS_ADDRESS);
    nrf_nvmc_write_bytes(BOOTLOADER_SETTINGS_ADDRESS,
                         (const uint8_t *)&s_dfu_settings,
                         sizeof(nrf_dfu_settings_t));

    nrf_nvmc_page_erase(BOOTLOADER_SETTINGS_BACKUP_ADDRESS);
    nrf_nvmc_write_bytes(BOOTLOADER_SETTINGS_BACKUP_ADDRESS,
                         (const uint8_t *)&s_dfu_settings,
                         sizeof(nrf_dfu_settings_t));

    if (m_observer) m_observer(NRF_DFU_EVT_DFU_COMPLETED);

    nrf_delay_ms(100);
    NVIC_SystemReset();
}

/* ---- Composite hook ----
 *
 * The CDC serial DFU transport owns the USBD stack. It calls this weak
 * hook after app_usbd_init() but before app_usbd_power_events_enable() —
 * the only safe window to append MSC. GhostFAT is initialised here and
 * the observer is shared via uf2_set_observer(). */

void usb_dfu_transport_class_register(void)
{
    ret_code_t err;

    uf2_ghostfat_init();

    err = app_usbd_class_append(app_usbd_msc_class_inst_get(&m_app_msc));
    if (err != NRF_SUCCESS) {
        NRF_LOG_ERROR("MSC class append failed: 0x%08x", err);
    } else {
        NRF_LOG_INFO("MSC registered as interface 2.");
    }
}

void uf2_set_observer(nrf_dfu_observer_t observer)
{
    m_observer = observer;
}
