/*
 * uf2_status.c — INFO_UF2.TXT + FAIL.TXT for UF2 transfer diagnostics.
 */

#include "uf2_status.h"
#include <string.h>

static const char m_info_txt[] =
    "UF2 Bootloader -- ChameleonUltra\r\n"
    "================================\r\n"
    "Chip     : nRF52840\r\n"
    "Family   : 0x1B57745F\r\n"
    "App addr : 0x00027000 - 0x000EB000\r\n"
    "\r\n"
    "Drag a .uf2 onto this drive to flash.\r\n"
    "If FAIL.TXT appears after, read it to\r\n"
    "see what went wrong.\r\n"
    "\r\n"
    "If a transfer fails, try:\r\n"
    "  sudo dd if=app.uf2 of=/dev/sdX \\\r\n"
    "    bs=512 conv=notrunc \\\r\n"
    "    oflag=direct,sync\r\n"
    "\r\n"
    "Source: github.com/nieldk/ChameleonUltra\r\n";

static char m_fail_txt[UF2_STATUS_FAIL_TXT_MAX];
static uint32_t m_fail_len;

static struct {
    uint32_t blocks_accepted;
    uint32_t blocks_rejected;
    uint32_t num_blocks_expected;
    uint32_t first_fail_block;
    uint32_t first_fail_addr;
    uf2_reject_reason_t first_fail_reason;
    bool transfer_in_progress;
    bool has_failure;
} m_session;


static char *put_str(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    return dst;
}

static char *put_hex32(char *dst, uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    *dst++ = '0'; *dst++ = 'x';
    for (int i = 28; i >= 0; i -= 4)
        *dst++ = hex[(val >> i) & 0xF];
    return dst;
}

static char *put_dec32(char *dst, uint32_t val)
{
    if (val == 0) { *dst++ = '0'; return dst; }
    char tmp[10]; int len = 0;
    while (val > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (len > 0) *dst++ = tmp[--len];
    return dst;
}

static const char *reason_label(uf2_reject_reason_t r)
{
    switch (r) {
        case UF2_REJECT_MAGIC:  return "MAGIC";
        case UF2_REJECT_FAMILY: return "FAMILY";
        case UF2_REJECT_BOUNDS: return "BOUNDS";
        case UF2_REJECT_WRITE:  return "WRITE";
        case UF2_REJECT_SEQ:    return "SEQ";
        default:                return "UNKNOWN";
    }
}

static const char *reason_hint(uf2_reject_reason_t r)
{
    switch (r) {
    case UF2_REJECT_MAGIC:
        return "  UF2 file truncated or corrupted.\r\n"
               "  Regenerate it from source.";
    case UF2_REJECT_FAMILY:
        return "  Wrong family ID. Use uf2conv.py\r\n"
               "  with -f 0x1B57745F (nRF52840).";
    case UF2_REJECT_BOUNDS:
        return "  Target address outside app region\r\n"
               "  [0x27000, 0xEB000). Most common:\r\n"
               "  uf2conv.py missing Intel HEX type 02\r\n"
               "  records. Add elif rtype==0x02 case\r\n"
               "  in parse_hex().";
    case UF2_REJECT_WRITE:
        return "  Flash write failed. BPROT engaged or\r\n"
               "  flash worn out. Needs SWD to recover.";
    case UF2_REJECT_SEQ:
        return "  Bad block sequence. UF2 malformed,\r\n"
               "  regenerate it.";
    default:
        return "  Unknown rejection reason.";
    }
}

void uf2_status_init(void)
{
    memset(&m_session, 0, sizeof(m_session));
    m_session.first_fail_reason = UF2_REJECT_NONE;
    m_fail_len = 0;
}

static void session_begin_if_needed(uint32_t num_blocks)
{
    if (!m_session.transfer_in_progress) {
        m_session.blocks_accepted      = 0;
        m_session.blocks_rejected      = 0;
        m_session.num_blocks_expected  = num_blocks;
        m_session.first_fail_block     = 0;
        m_session.first_fail_addr      = 0;
        m_session.first_fail_reason    = UF2_REJECT_NONE;
        m_session.has_failure          = false;
        m_session.transfer_in_progress = true;
        m_fail_len = 0;
    }
}

void uf2_status_record_accepted(uint32_t block_no,
                                uint32_t num_blocks,
                                uint32_t target_addr)
{
    (void)block_no; (void)target_addr;
    session_begin_if_needed(num_blocks);
    m_session.blocks_accepted++;
}

void uf2_status_record_rejected(uint32_t block_no,
                                uint32_t num_blocks,
                                uint32_t target_addr,
                                uf2_reject_reason_t reason)
{
    session_begin_if_needed(num_blocks);

    if (m_session.blocks_rejected == 0) {
        m_session.first_fail_block  = block_no;
        m_session.first_fail_addr   = target_addr;
        m_session.first_fail_reason = reason;

        char *p = m_fail_txt;
        p = put_str(p, "UF2 transfer FAILED\r\n");
        p = put_str(p, "===================\r\n");
        p = put_str(p, "Reason  : ");
        p = put_str(p, reason_label(reason));
        p = put_str(p, "\r\n");
        p = put_str(p, "Block   : ");
        p = put_dec32(p, block_no);
        p = put_str(p, " / ");
        p = put_dec32(p, num_blocks);
        p = put_str(p, "\r\n");
        p = put_str(p, "Address : ");
        p = put_hex32(p, target_addr);
        p = put_str(p, "\r\n");
        p = put_str(p, "Accepted: ");
        p = put_dec32(p, m_session.blocks_accepted);
        p = put_str(p, "\r\n\r\n");
        p = put_str(p, reason_hint(reason));
        p = put_str(p, "\r\n");

        m_fail_len = (uint32_t)(p - m_fail_txt);
        if (m_fail_len > UF2_STATUS_FAIL_TXT_MAX)
            m_fail_len = UF2_STATUS_FAIL_TXT_MAX;
    }
    m_session.blocks_rejected++;
    m_session.has_failure = true;
}

bool uf2_status_has_failure(void) { return m_session.has_failure; }

const char *uf2_status_get_info_txt(uint32_t *out_size)
{
    if (out_size) *out_size = sizeof(m_info_txt) - 1;
    return m_info_txt;
}

const char *uf2_status_get_fail_txt(uint32_t *out_size)
{
    if (out_size) *out_size = m_fail_len;
    return m_fail_txt;
}
