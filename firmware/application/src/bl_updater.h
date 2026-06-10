/*
 * bl_updater.h — bootloader self-update from the application.
 */
#ifndef BL_UPDATER_H
#define BL_UPDATER_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Staging area layout (shared with bootloader/src/main.c) -----------
 *
 * bl_updater_stage_and_reset_force() writes the embedded BL binary here.
 * On next reset, the bootloader checks for BL_STAGED_MAGIC *before* it
 * sets ACL flash protection, copies a tiny update function to RAM, and
 * applies the staged image — bypassing the ACL problem entirely.
 *
 * The staging area sits in the high end of the app flash region, well
 * above the largest application binary (~0x6C000).
 *
 * Layout:
 *   0xDF000  BL_STAGED_MAGIC  (4 bytes)  = 0xBEEFCAFE
 *   0xDF004  BL size          (4 bytes)
 *   0xDF008  BL binary data   (up to 80KB, ends ~0xEAFFF)
 *
 * 12 pages (0xDF000–0xEAFFF) are reserved.  Keep in sync with the
 * matching constants in bootloader/src/main.c.
 * ----------------------------------------------------------------------- */
#define BL_STAGED_BASE       0x000DF000UL
#define BL_STAGED_MAGIC_ADDR (BL_STAGED_BASE + 0u)
#define BL_STAGED_SIZE_ADDR  (BL_STAGED_BASE + 4u)
#define BL_STAGED_DATA_ADDR  (BL_STAGED_BASE + 8u)
#define BL_STAGED_MAGIC_VAL  0xBEEFCAFEUL
#define BL_STAGED_PAGES      12u          /* 12 × 4 KB = 48 KB               */
#define BL_STAGED_PAGE_SZ    0x1000UL

typedef enum {
    BL_UPDATER_OK             = 0,
    BL_UPDATER_ERR_EMPTY      = 1,
    BL_UPDATER_ERR_TOO_LARGE  = 2,
    BL_UPDATER_ERR_CRC        = 3,
    BL_UPDATER_ERR_SD_DISABLE = 4,
    BL_UPDATER_ERR_VERIFY     = 5,
} bl_updater_status_t;

/* Validate the embedded BL data (size + CRC32) without touching flash. */
bl_updater_status_t bl_updater_validate(void);

/* Write the embedded BL into the BL region, then reset.
 * Validates CRC first. */
bl_updater_status_t bl_updater_run(void);

/* Write the embedded BL into the BL region, erase own vector table, reset.
 * Validates CRC first. */
bl_updater_status_t bl_updater_run_and_invalidate_app(void);

/* Same as above but SKIPS CRC check. */
bl_updater_status_t bl_updater_run_and_invalidate_app_force(void);

/* Stage the embedded BL in the app region so the bootloader can apply it
 * on the next reset — BEFORE ACL protection is set.
 * SKIPS CRC check (use when BL has been verified at build time).
 * Erases own vector table then resets. Does NOT return on success. */
bl_updater_status_t bl_updater_stage_and_reset_force(void);

#endif /* BL_UPDATER_H */
