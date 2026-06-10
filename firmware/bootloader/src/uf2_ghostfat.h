/*
 * uf2_ghostfat.h — virtual FAT12 disk that captures incoming UF2 blocks.
 *
 * MIT License. Original implementation; design pattern inspired by
 * adafruit/tinyuf2's ghostfat layer.
 */
#ifndef UF2_GHOSTFAT_H__
#define UF2_GHOSTFAT_H__

#include <stdint.h>
#include <stdbool.h>

/* Virtual disk geometry */
#define UF2_SECTOR_SIZE        512u
#define UF2_TOTAL_SECTORS      4026u     /* 2 MB virtual disk — plenty */

/* Flash window this bootloader will accept writes to (app region).
 * Matches application.ld. */
#define UF2_FLASH_APP_START    0x00027000UL
#define UF2_FLASH_APP_END      0x000EB000UL
#define UF2_FLASH_APP_SIZE     (UF2_FLASH_APP_END - UF2_FLASH_APP_START)

/* Stage 1 bootloader accepts writes to the full flash range so the
 * fullimage.uf2 (which includes MBR, SoftDevice, BL, app) can be
 * used to upgrade to stage 2. */
#ifdef STAGE1_BUILD
#undef  UF2_FLASH_APP_START
#undef  UF2_FLASH_APP_END
#define UF2_FLASH_APP_START    0x00000000UL
#define UF2_FLASH_APP_END      0x00100000UL
#endif

/* Called once at transport init. */
void uf2_ghostfat_init(void);

/* MSC read/write hooks. Each reads/writes one 512-byte sector at logical
 * block address `lba`. Return 0 on success, negative on error. */
int  uf2_ghostfat_read_block (uint32_t lba, uint8_t *buf);
int  uf2_ghostfat_write_block(uint32_t lba, const uint8_t *buf);

/* Progress / completion. */
uint32_t uf2_ghostfat_blocks_written(void);
bool     uf2_ghostfat_is_complete(void);
bool     uf2_ghostfat_has_failure(void);

/* Implemented by the integrator (nrf_dfu_uf2.c) — actually write to flash. */
extern bool uf2_flash_write(uint32_t addr, const void *data, uint32_t len);
extern void uf2_flash_read (uint32_t addr,       void *data, uint32_t len);

/* Called by ghostfat exactly once when the last UF2 block has been written.
 * Implemented by nrf_dfu_uf2.c — finalise (mark app valid, reset). */
extern void uf2_dfu_complete(void);

#endif /* UF2_GHOSTFAT_H__ */
