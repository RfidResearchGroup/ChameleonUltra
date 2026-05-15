#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LF reader-talk-first gap detection and transmission.
 *
 * Reader-talk-first (RTF) protocols like EM4x05/4x69 and EM4x50/4x70
 * communicate with the tag by briefly cutting the 125kHz carrier field.
 * A "gap" — carrier off for a calibrated number of carrier cycles — encodes
 * one bit.  After the command sequence, the reader restores the field and
 * listens for the tag's Manchester- or Biphase-encoded response.
 *
 * Gap timing (EM4x05 / EM4x69, per datasheet):
 *   Start gap:  ~50 Tc  (powers up and resets the tag)
 *   Write gap:  ~10 Tc  (separates command bits during transmission)
 *   Bit '0':    ~24 Tc  field on between gaps
 *   Bit '1':    ~56 Tc  field on between gaps
 *
 * The existing T5577 writer in lf_t55xx_data.c uses the same physical
 * mechanism (stop_lf_125khz_radio / bsp_delay_us / start_lf_125khz_radio)
 * inside a timeslot callback.  This module follows the same pattern.
 *
 * Gap detection on the receive side:
 *   The GPIOTE edge-capture counter fires on each carrier envelope edge.
 *   During a gap the carrier is absent, so no edges arrive.  We detect a
 *   gap by polling the counter and declaring a gap when no edge has arrived
 *   within GAP_DETECT_TIMEOUT_TC carrier cycles.  The gap duration is then
 *   the elapsed counter value.
 *
 * Units: all timing constants are in carrier cycles (Tc = 1/125000 s = 8 µs).
 * bsp_delay_us() is used for gap transmission; the counter captures elapsed
 * carrier cycles on the receive side.
 */

/* -----------------------------------------------------------------------
 * Transmit timing constants (in microseconds = Tc × 8)
 * --------------------------------------------------------------------- */

/** Start gap: resets the tag and signals start of a command sequence. */
#define GAP_START_TC        55  /* PM3 proven: 55*8=440us for EM4x05/4305 */
#define GAP_START_US        (GAP_START_TC * 8)

/** Write gap: separates command bits during transmission. */
#define GAP_WRITE_TC        16  /* PM3 proven: 16*8=128us */
#define GAP_WRITE_US        (GAP_WRITE_TC * 8)

/** Field-on duration encoding bit '0' between write gaps. */
#define GAP_BIT0_TC         23  /* PM3 proven: 23*8=184us */
#define GAP_BIT0_US         (GAP_BIT0_TC * 8)

/** Field-on duration encoding bit '1' between write gaps. */
#define GAP_BIT1_TC         32  /* PM3 proven: 32*8=256us */
#define GAP_BIT1_US         (GAP_BIT1_TC * 8)

/**
 * Listen window after command: time the tag needs before it begins
 * transmitting its response (EM4x05 datasheet: ~3 Tc after last gap).
 * We wait a generous 50 Tc to be safe with slow tags.
 */
#define GAP_LISTEN_TC       50
#define GAP_LISTEN_US       (GAP_LISTEN_TC * 8)

/* -----------------------------------------------------------------------
 * Receive timing constants (in carrier cycles)
 * --------------------------------------------------------------------- */

/**
 * Gap detection timeout: if no edge arrives within this many carrier
 * cycles, the current interval is treated as a gap.
 * Set conservatively above the longest expected normal interval (≈ 2×RF/64
 * = 128 Tc for EM4x05 Manchester at RF/64) but below any deliberate gap.
 */
#define GAP_DETECT_TIMEOUT_TC   200

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

void lf_gap_send_start(void);
void lf_gap_send_bit(uint8_t bit);
void lf_gap_send_u32(uint32_t word);
void lf_gap_send_bits(uint32_t value, uint8_t nbits);
bool lf_gap_detect(uint32_t last_count, uint32_t *gap_tc);

#ifdef __cplusplus
}
#endif
