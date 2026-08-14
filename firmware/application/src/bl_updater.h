/*
 * bl_updater.h — bootloader self-update from the application.
 */
#ifndef BL_UPDATER_H
#define BL_UPDATER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BL_UPDATER_OK             = 0,
    BL_UPDATER_ERR_EMPTY      = 1,
    BL_UPDATER_ERR_TOO_LARGE  = 2,
    BL_UPDATER_ERR_CRC        = 3,
    BL_UPDATER_ERR_SD_DISABLE = 4,
    BL_UPDATER_ERR_VERIFY     = 5,
    BL_UPDATER_ERR_ACL_LOCKED = 6,   /* target BL region is ACL write-locked by the old BL */
} bl_updater_status_t;

/* Validate the embedded BL data (size + CRC32) without touching flash. */
bl_updater_status_t bl_updater_validate(void);

/* Read-only pre-flight for THIS unit: BL_UPDATER_OK only if a real run would
 * succeed (embedded BL valid AND target region not ACL-locked). No side effects. */
bl_updater_status_t bl_updater_preflight(void);

/* Current UICR->NRFFW[0] (where the MBR boots the BL from). */
uint32_t bl_updater_current_bl_addr(void);

/* Write the embedded BL into the BL region, then reset.
 * Validates CRC first. */
bl_updater_status_t bl_updater_run(void);

/* Write the embedded BL into the BL region, erase own vector table, reset.
 * SKIPS CRC check (use when the BL has been verified at build time). */
bl_updater_status_t bl_updater_run_and_invalidate_app_force(void);

#endif /* BL_UPDATER_H */
