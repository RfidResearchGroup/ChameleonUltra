/*
 * nrf_dfu_uf2.c — Nordic SDK DFU transport for UF2 drag-and-drop updates.
 *
 * Single USB device: MSC only. Owns the entire USBD stack.
 *
 * Memory layout:
 *     0x00000 - 0x01000   MBR
 *     0x01000 - 0x27000   SoftDevice S140
 *     0x27000 - 0xEB000   Application      <— UF2 writes go here
 *     0xEB000 - 0xFE000   Bootloader       <— do not write
 *     0xFE000 - 0xFF000   MBR params
 *     0xFF000 - 0x100000  Bootloader settings
 *
 * MIT License.
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

#define MSC_INTERFACE        0
#define MSC_EPIN             NRF_DRV_USBD_EPIN1
#define MSC_EPOUT            NRF_DRV_USBD_EPOUT1
#define MSC_WORKBUFFER_SIZE  512

static nrf_dfu_observer_t m_observer;

uint32_t uf2_transport_init(nrf_dfu_observer_t observer)         __attribute__((used));
uint32_t uf2_transport_close(nrf_dfu_transport_t const *p_excpt) __attribute__((used));

DFU_TRANSPORT_REGISTER(nrf_dfu_transport_t const uf2_dfu_transport) =
{
    .init_func  = uf2_transport_init,
    .close_func = uf2_transport_close,
};

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
        NRF_LOG_WARNING("UF2 write outside region 0x%08x", addr);
        return false;
    }

    /* Skip MBR and SoftDevice regions in stage1 full-flash mode —
     * we only need to write the bootloader and app, not re-flash SD. */
#ifdef STAGE1_BUILD
    if (addr < 0x00027000UL) {
        /* Silently accept but skip MBR/SD writes — they're already correct. */
        return true;
    }
#endif

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

/* ---- USBD wiring ---- */

static void usbd_event_handler(app_usbd_event_type_t event)
{
    switch (event) {
        case APP_USBD_EVT_DRV_SUSPEND:    app_usbd_suspend_req();  break;
        case APP_USBD_EVT_DRV_RESUME:                              break;
        case APP_USBD_EVT_STARTED:                                 break;
        case APP_USBD_EVT_STOPPED:        app_usbd_disable();      break;
        case APP_USBD_EVT_POWER_DETECTED:
            if (!nrf_drv_usbd_is_enabled()) app_usbd_enable();
            break;
        case APP_USBD_EVT_POWER_REMOVED:  app_usbd_stop();         break;
        case APP_USBD_EVT_POWER_READY:    app_usbd_start();        break;
        default: break;
    }
}

uint32_t uf2_transport_init(nrf_dfu_observer_t observer)
{
    ret_code_t err;

    m_observer = observer;
    uf2_ghostfat_init();

    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_event_handler,
    };

    err = nrf_drv_clock_init();
    if (err != NRF_SUCCESS && err != NRF_ERROR_MODULE_ALREADY_INITIALIZED)
        return err;

    err = nrf_drv_power_init(NULL);
    if (err != NRF_SUCCESS && err != NRF_ERROR_MODULE_ALREADY_INITIALIZED)
        return err;

    app_usbd_serial_num_generate();

    err = app_usbd_init(&usbd_config);
    if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE)
        return err;

    err = app_usbd_class_append(app_usbd_msc_class_inst_get(&m_app_msc));
    if (err != NRF_SUCCESS) {
        NRF_LOG_ERROR("MSC class append failed: 0x%08x", err);
        return err;
    }

    err = app_usbd_power_events_enable();
    if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE)
        return err;

    NRF_LOG_INFO("UF2 transport ready.");
    return NRF_SUCCESS;
}

uint32_t uf2_transport_close(nrf_dfu_transport_t const *p_exception)
{
    (void)p_exception;
    return NRF_SUCCESS;
}
