#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM4x05 / EM4x69 reader (read-only).
 *
 * EM4x05 and EM4x69 are reader-talk-first (RTF) 125kHz LF tags made by
 * EM Microelectronic.  EM4x69 is identical to EM4x05 with an added
 * password-protected login command; the read protocol is the same.
 *
 * Protocol summary (EM4x05 datasheet rev 1.0, §6):
 *
 *   1. Reader asserts a start gap (≥50 Tc field off).
 *   2. Reader sends a 9-bit command word:
 *        [start_bit=1] [opcode 2 bits] [address 3 bits] [parity 3 bits]
 *      using gap encoding: field on for 56Tc = '1', 24Tc = '0', separated
 *      by 10Tc write gaps.
 *   3. Tag waits ~3Tc then transmits a 45-bit response word:
 *        [header 1 bit] [data 32 bits] [col_parity 4 bits] [stop 1 bit]
 *        [row_parity 4 bits] [stop 1 bit] [trailer 2 bits]
 *      encoded as Manchester at RF/64 (one bit = 64 carrier cycles).
 *
 * Opcodes:
 *   EM4X05_OPCODE_READ   (0b10 = 2)  — read one block
 *   EM4X05_OPCODE_WRITE  (0b01 = 1)  — write one block (not implemented here)
 *   EM4X05_OPCODE_PRCT   (0b11 = 3)  — protect (not implemented here)
 *   EM4X05_OPCODE_DSBL   (0b00 = 0)  — disable  (not implemented here)
 *
 * Block map (EM4x05, 16 blocks × 32 bits):
 *   Block 0:  configuration word
 *   Block 1:  password (write-only; reads as 0)
 *   Block 2:  user data
 *   ...
 *   Block 15: UID (read-only)
 *
 * EM4x69 adds blocks 13–14 for a 64-bit UID and a LOGIN command that must
 * be issued before protected blocks are accessible.  The read protocol for
 * unprotected blocks is identical.
 *
 * This implementation reads the minimum set needed to identify a tag:
 *   - Block 0  (config — tells us encoding, data rate, tag type)
 *   - Block 1  (UID low word for EM4x05; block 13 for EM4x69 64-bit UID)
 *   - Block 15 (UID for EM4x05; per-chip serial for EM4x69)
 *
 * Data returned:
 *   em4x05_data_t packs the raw block words read from the tag.
 */

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

#define EM4X05_OPCODE_READ    0x02   /* 0b10 */
#define EM4X05_OPCODE_WRITE   0x01   /* 0b01 — not used here */
#define EM4X05_OPCODE_PRCT    0x03   /* 0b11 — not used here */
#define EM4X05_OPCODE_DSBL    0x00   /* 0b00 — not used here */

#define EM4X05_BLOCK_CONFIG   0
#define EM4X05_BLOCK_PASSWD   1
#define EM4X05_BLOCK_UID_LO   2     /* EM4x05: first user data block        */
#define EM4X05_BLOCK_UID      15    /* EM4x05: factory UID                  */
#define EM4X69_BLOCK_UID_LO   13    /* EM4x69: 64-bit UID low word          */
#define EM4X69_BLOCK_UID_HI   14    /* EM4x69: 64-bit UID high word         */

/** Number of bits in one tag response word (header+data+parity+stop+trailer) */
#define EM4X05_RESPONSE_BITS  45

/** Manchester clock: one bit = RF/64 = 64 carrier cycles */
#define EM4X05_RF_DIV         64

/**
 * Timeout waiting for the tag response after command, in carrier cycles.
 * The tag begins responding within ~3Tc; we wait up to 300Tc to cover
 * slow wakeup and Manchester sync acquisition.
 */
#define EM4X05_RESPONSE_TIMEOUT_TC  300

/* -----------------------------------------------------------------------
 * Data structures
 * --------------------------------------------------------------------- */

/** Raw data read back from an EM4x05/4x69 tag. */
typedef struct {
    uint32_t config;      /* block 0: configuration word                  */
    uint32_t uid;         /* block 15 (EM4x05) or blocks 13+14 (EM4x69)   */
    uint32_t uid_hi;      /* EM4x69 only: high word of 64-bit UID          */
    bool     is_em4x69;   /* true if 64-bit UID was successfully read       */
} em4x05_data_t;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/**
 * Read an EM4x05 or EM4x69 tag.
 *
 * Sends a READ command for each required block, decodes the Manchester
 * response, validates parity, and fills *out on success.
 *
 * The 125kHz carrier must already be running (call start_lf_125khz_radio()
 * and enable the GPIOTE edge counter before calling this).
 *
 * @param out         Output structure; valid only when returns true.
 * @param timeout_ms  Maximum time to wait for the first response.
 * @return            true on success (at least block 15 / UID read OK).
 */
bool em4x05_read(em4x05_data_t *out, uint32_t timeout_ms);

/**
 * High-level scan entry point matching the pattern of em410x_read() in
 * lf_reader_main.c.  Starts the radio, reads the tag, stops the radio.
 *
 * @param out  Output structure.
 * @return     STATUS_LF_TAG_OK or STATUS_LF_TAG_NO_FOUND.
 */
uint8_t scan_em4x05(em4x05_data_t *out);

#ifdef __cplusplus
}
#endif
