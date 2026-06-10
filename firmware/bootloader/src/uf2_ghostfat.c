/*
 * uf2_ghostfat.c — virtual FAT12 disk for ChameleonUltra UF2 bootloader.
 *
 * Exposes two status files in the root directory:
 *   - INFO_UF2.TXT: always present, device info + dd recommendations
 *   - FAIL.TXT:     appears after a UF2 block was rejected
 *
 * MIT License.
 */
#include "uf2_ghostfat.h"
#include "uf2.h"
#include "uf2_status.h"
#include <string.h>

#define NRF_LOG_MODULE_NAME uf2_ghostfat
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

/* Resets the DFU inactivity timer — called on rejection so the bootloader
 * doesn't reboot before the host can read FAIL.TXT. */
extern void uf2_ping_observer(void);

/* Triggers a USB reconnect so the host remounts with FAIL.TXT visible. */

#define BPB_BYTES_PER_SECTOR    UF2_SECTOR_SIZE
#define BPB_SECTORS_PER_CLUSTER 1
#define BPB_RESERVED_SECTORS    1
#define BPB_NUM_FATS            2
#define BPB_ROOT_ENTRIES        16
#define BPB_TOTAL_SECTORS       UF2_TOTAL_SECTORS
#define BPB_MEDIA_DESCRIPTOR    0xF8
#define BPB_SECTORS_PER_FAT     12

#define FAT_START_SECTOR        BPB_RESERVED_SECTORS
#define ROOT_DIR_START_SECTOR   (FAT_START_SECTOR + BPB_NUM_FATS * BPB_SECTORS_PER_FAT)
#define ROOT_DIR_SECTORS        ((BPB_ROOT_ENTRIES * 32 + UF2_SECTOR_SIZE - 1) / UF2_SECTOR_SIZE)
#define DATA_START_SECTOR       (ROOT_DIR_START_SECTOR + ROOT_DIR_SECTORS)

#define INFO_FILE_CLUSTER       2
#define FAIL_FILE_CLUSTER       3

#define INFO_FILE_SECTOR        DATA_START_SECTOR
#define FAIL_FILE_SECTOR        (DATA_START_SECTOR + 1)

static uint32_t m_blocks_written;
static uint32_t m_num_blocks_expected;
static bool     m_completion_signalled;

#pragma pack(push, 1)
typedef struct {
    uint8_t  jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} fat_bpb_t;

typedef struct {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t first_cluster_hi;
    uint16_t wtime;
    uint16_t wdate;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} fat_dir_entry_t;
#pragma pack(pop)

_Static_assert(sizeof(fat_dir_entry_t) == 32, "dir entry must be 32B");

static const fat_bpb_t k_bpb = {
    .jump                = { 0xEB, 0x3C, 0x90 },
    .oem_name            = { 'M','S','D','O','S','5','.','0' },
    .bytes_per_sector    = BPB_BYTES_PER_SECTOR,
    .sectors_per_cluster = BPB_SECTORS_PER_CLUSTER,
    .reserved_sectors    = BPB_RESERVED_SECTORS,
    .num_fats            = BPB_NUM_FATS,
    .root_entries        = BPB_ROOT_ENTRIES,
    .total_sectors_16    = BPB_TOTAL_SECTORS,
    .media_descriptor    = BPB_MEDIA_DESCRIPTOR,
    .sectors_per_fat     = BPB_SECTORS_PER_FAT,
    .sectors_per_track   = 1,
    .num_heads           = 1,
    .hidden_sectors      = 0,
    .total_sectors_32    = 0,
    .drive_number        = 0x80,
    .reserved1           = 0,
    .boot_sig            = 0x29,
    .volume_id           = 0x00420042,
    .volume_label        = { 'C','H','A','M','E','L','E','O','N',' ',' ' },
    .fs_type             = { 'F','A','T','1','2',' ',' ',' ' },
};

static const fat_dir_entry_t k_vol_label = {
    .name = { 'C','H','A','M','E','L','E','O','N',' ',' ' },
    .attr = 0x08,
};

static void fat12_put(uint8_t *fat, uint32_t entry, uint16_t value)
{
    uint32_t off = entry + (entry >> 1);
    if (entry & 1) {
        fat[off]     = (fat[off] & 0x0F) | ((value & 0x0F) << 4);
        fat[off + 1] = (value >> 4) & 0xFF;
    } else {
        fat[off]     = value & 0xFF;
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

static void dir_make_file(fat_dir_entry_t *e, const char *name11,
                          uint16_t cluster, uint32_t size)
{
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name11, 11);
    e->attr             = 0x20;
    e->cdate            = 0x5221;
    e->adate            = 0x5221;
    e->wdate            = 0x5221;
    e->first_cluster_lo = cluster;
    e->file_size        = size;
}

void uf2_ghostfat_init(void)
{
    /* Preserve failure state across re-init (host remounts the MSC device
     * after writing, which triggers op_init -> ghostfat_init before the
     * host can read FAIL.TXT. Clearing here would make FAIL.TXT disappear
     * before the host ever sees it). */
    if (!uf2_status_has_failure()) {
        m_blocks_written = 0;
        m_num_blocks_expected = 0;
        m_completion_signalled = false;
        uf2_status_init();
    }
    NRF_LOG_INFO("GhostFAT init. DATA_START=%u TOTAL=%u failure=%d",
                 DATA_START_SECTOR, BPB_TOTAL_SECTORS,
                 (int)uf2_status_has_failure());
}

uint32_t uf2_ghostfat_blocks_written(void) { return m_blocks_written; }
bool     uf2_ghostfat_is_complete(void)    { return m_completion_signalled; }
bool     uf2_ghostfat_has_failure(void)    { return uf2_status_has_failure(); }

int uf2_ghostfat_read_block(uint32_t lba, uint8_t *buf)
{
    memset(buf, 0, UF2_SECTOR_SIZE);

    if (lba >= BPB_TOTAL_SECTORS) return 0;

    if (lba == 0) {
        memcpy(buf, &k_bpb, sizeof(k_bpb));
        /* Change volume ID when failure is present so the kernel
         * invalidates its FAT cache and re-reads the root directory,
         * making FAIL.TXT visible without requiring a manual remount. */
        if (uf2_status_has_failure()) {
            fat_bpb_t *bpb = (fat_bpb_t *)buf;
            bpb->volume_id = 0x00420043;
        }
        buf[510] = 0x55;
        buf[511] = 0xAA;
        return 0;
    }

    if (lba < ROOT_DIR_START_SECTOR) {
        uint32_t fat_idx = (lba - FAT_START_SECTOR) % BPB_SECTORS_PER_FAT;
        if (fat_idx == 0) {
            fat12_put(buf, 0, 0xFF8);
            fat12_put(buf, 1, 0xFFF);
            fat12_put(buf, INFO_FILE_CLUSTER, 0xFFF);
            fat12_put(buf, FAIL_FILE_CLUSTER, 0xFFF);
        }
        return 0;
    }

    if (lba == ROOT_DIR_START_SECTOR) {
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;
        memcpy(&entries[0], &k_vol_label, sizeof(k_vol_label));
        {
            uint32_t info_sz;
            (void)uf2_status_get_info_txt(&info_sz);
            dir_make_file(&entries[1], "INFO_UF2TXT", INFO_FILE_CLUSTER, info_sz);
        }
        if (uf2_status_has_failure()) {
            uint32_t sz;
            (void)uf2_status_get_fail_txt(&sz);
            dir_make_file(&entries[2], "FAIL    TXT", FAIL_FILE_CLUSTER, sz);
            NRF_LOG_WARNING("FAIL.TXT present in root dir");
        }
        return 0;
    }

    if (lba == INFO_FILE_SECTOR) {
        uint32_t sz;
        const char *txt = uf2_status_get_info_txt(&sz);
        if (sz > UF2_SECTOR_SIZE) sz = UF2_SECTOR_SIZE;
        memcpy(buf, txt, sz);
        return 0;
    }
    if (lba == FAIL_FILE_SECTOR) {
        if (uf2_status_has_failure()) {
            uint32_t sz;
            const char *txt = uf2_status_get_fail_txt(&sz);
            if (sz > UF2_SECTOR_SIZE) sz = UF2_SECTOR_SIZE;
            memcpy(buf, txt, sz);
            NRF_LOG_WARNING("FAIL.TXT content read by host");
        }
        return 0;
    }

    return 0;
}

int uf2_ghostfat_write_block(uint32_t lba, const uint8_t *buf)
{
    if (lba < DATA_START_SECTOR) return 0;

    if (!uf2_is_block(buf)) return 0;

    const uf2_block_t *b = (const uf2_block_t *)buf;

    if ((b->flags & UF2_FLAG_FAMILYID) &&
        b->file_size_or_family_id != UF2_FAMILY_ID_NRF52840) {
        NRF_LOG_WARNING("Block %u rejected: wrong family ID 0x%08x",
                        b->block_no, b->file_size_or_family_id);
        uf2_status_record_rejected(b->block_no, b->num_blocks,
                                   b->target_addr, UF2_REJECT_FAMILY);
        uf2_ping_observer();
        return 0;
    }
    if (b->flags & UF2_FLAG_NOFLASH) return 0;

    if (b->target_addr < UF2_FLASH_APP_START ||
        b->target_addr + b->payload_size > UF2_FLASH_APP_END) {
        NRF_LOG_WARNING("Block %u rejected: addr 0x%08x out of bounds [0x%08x..0x%08x]",
                        b->block_no, b->target_addr,
                        UF2_FLASH_APP_START, UF2_FLASH_APP_END);
        uf2_status_record_rejected(b->block_no, b->num_blocks,
                                   b->target_addr, UF2_REJECT_BOUNDS);
        uf2_ping_observer();
        return 0;
    }
    if (b->payload_size == 0 || b->payload_size > sizeof(b->data)) {
        NRF_LOG_WARNING("Block %u rejected: bad payload_size %u",
                        b->block_no, b->payload_size);
        uf2_status_record_rejected(b->block_no, b->num_blocks,
                                   b->target_addr, UF2_REJECT_SEQ);
        uf2_ping_observer();
        return 0;
    }
    if (b->num_blocks != 0 && b->block_no >= b->num_blocks) {
        NRF_LOG_WARNING("Block %u rejected: block_no >= num_blocks %u",
                        b->block_no, b->num_blocks);
        uf2_status_record_rejected(b->block_no, b->num_blocks,
                                   b->target_addr, UF2_REJECT_SEQ);
        uf2_ping_observer();
        return 0;
    }

    NRF_LOG_DEBUG("Block %u/%u addr 0x%08x",
                  b->block_no, b->num_blocks, b->target_addr);

    if (!uf2_flash_write(b->target_addr, b->data, b->payload_size)) {
        NRF_LOG_ERROR("Block %u flash write FAILED at 0x%08x",
                      b->block_no, b->target_addr);
        uf2_status_record_rejected(b->block_no, b->num_blocks,
                                   b->target_addr, UF2_REJECT_WRITE);
        uf2_ping_observer();
        return 0;
    }

    uf2_status_record_accepted(b->block_no, b->num_blocks, b->target_addr);

    if (m_num_blocks_expected == 0 && b->num_blocks != 0) {
        m_num_blocks_expected = b->num_blocks;
        NRF_LOG_INFO("Transfer started: expecting %u blocks", m_num_blocks_expected);
    }
    m_blocks_written++;

    if ((m_blocks_written % 100) == 0) {
        NRF_LOG_INFO("Progress: %u/%u blocks", m_blocks_written, m_num_blocks_expected);
    }

    if (!m_completion_signalled &&
        m_num_blocks_expected != 0 &&
        m_blocks_written >= m_num_blocks_expected) {
        m_completion_signalled = true;
        NRF_LOG_INFO("Transfer complete: %u blocks written", m_blocks_written);
        uf2_dfu_complete();
    }
    return 0;
}
