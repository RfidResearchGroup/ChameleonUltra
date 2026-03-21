#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PSK (Phase Shift Keying) demodulator for 125kHz LF tags.
 *
 * Supports PSK1, PSK2, and PSK3 as used by Indala, Keri, and NexWatch.
 *
 * Signal model
 * ============
 * A continuous square-wave subcarrier rides on the 125kHz AM carrier.
 * The subcarrier frequency is carrier / rf_div:
 *   rf_div=2  =>  62500 Hz   subcarrier
 *   rf_div=4  =>  31250 Hz   subcarrier  (Indala, Keri)
 *   rf_div=8  =>  15625 Hz   subcarrier  (NexWatch)
 *
 * One data bit spans rf_div subcarrier half-periods (= rf_div² carrier
 * cycles).  The interval values fed to psk_feed() are carrier-cycle
 * counts between successive envelope edges, identical in unit to the
 * values fed to the Manchester decoder.
 *
 * Phase encoding
 * ==============
 * PSK1 (absolute):
 *   A 180° phase inversion anywhere within a bit window → bit = '1'.
 *   No inversion → bit = '0'.
 *
 * PSK2 (differential BPSK):
 *   A phase inversion at the bit boundary → bit = '1'.
 *   No inversion → bit = '0'.
 *   (Functionally identical to PSK1 from the demodulator's point of view,
 *   because the phase inversion always occurs at the window boundary.)
 *
 * PSK3 (differential BPSK, inverted polarity):
 *   Same as PSK2 with the bit sense flipped:
 *   inversion → '0', no inversion → '1'.
 *
 * Interval classification
 * =======================
 * Each incoming interval (edge-to-edge time in carrier cycles) is one of:
 *   NORMAL  ≈ half_period          — one regular subcarrier half-period
 *   SHORT   ≈ half_period/2        — phase shift occurred; two SHORT edges
 *                                    together equal one NORMAL half-period
 *   LONG    ≈ 3 * half_period / 2  — phase shift straddles a half-period
 *                                    boundary; counts as one half-period
 *   BAD     — none of the above; triggers resync
 *
 * Synchronisation
 * ===============
 * PSK_SYNC_THRESHOLD consecutive NORMAL intervals are required before bits
 * are emitted, preventing garbage output at startup.
 *
 * Jitter tolerance
 * ================
 * An interval is accepted within ±jitter of its nominal value, where
 * jitter = max(half_period / PSK_JITTER_DIV, 1).  PSK_JITTER_DIV=4
 * gives 25% tolerance, which covers typical real-tag variation.
 */

/* Subcarrier divider — must match T5577 PSKCF field in block 0 */
typedef enum {
    PSK_RF_DIV_2 = 2,
    PSK_RF_DIV_4 = 4,
    PSK_RF_DIV_8 = 8,
} psk_rf_div_t;

/* PSK variant */
typedef enum {
    PSK_MODE_1 = 1,  /* absolute phase:              shift → '1' */
    PSK_MODE_2 = 2,  /* differential BPSK:           shift → '1' */
    PSK_MODE_3 = 3,  /* differential BPSK, inverted: shift → '0' */
} psk_mode_t;

/* Jitter tolerance: accept ±(half_period / PSK_JITTER_DIV) */
#define PSK_JITTER_DIV 4

/* Consecutive NORMAL intervals needed to declare carrier lock */
#define PSK_SYNC_THRESHOLD 4

typedef struct {
    /* configuration (set at alloc, read-only after) */
    psk_rf_div_t rf_div;      /* subcarrier divider: 2, 4, or 8            */
    psk_mode_t   mode;        /* PSK variant                                */
    uint8_t      half_period; /* nominal carrier cycles per subcarrier edge */
    uint8_t      jitter;      /* ±acceptance window in carrier cycles       */

    /* carrier-lock state */
    bool         synced;      /* true once PSK_SYNC_THRESHOLD clean edges seen */
    uint8_t      sync_count;  /* consecutive clean NORMAL edges            */

    /* bit-window accumulation */
    uint8_t      clk_count;   /* half-periods accumulated in current bit   */
    uint8_t      shift_count; /* phase shifts seen in current bit window   */

    /* SHORT-interval pairing state */
    bool         in_short;    /* true while waiting for the second SHORT   */
} psk_t;

/**
 * Allocate and initialise a PSK demodulator.
 *
 * @param rf_div  Subcarrier divider — must match the tag's T5577 config.
 * @param mode    PSK variant.
 * @return        Heap-allocated psk_t, or NULL on OOM.
 */
psk_t *psk_alloc(psk_rf_div_t rf_div, psk_mode_t mode);

/** Free a PSK demodulator. */
void psk_free(psk_t *p);

/** Reset decoder state without freeing (call before each read attempt). */
void psk_reset(psk_t *p);

/**
 * Feed one edge interval into the demodulator.
 *
 * @param p        Demodulator instance.
 * @param interval Carrier-cycle count between successive signal edges.
 * @param bit      Output: decoded bit (valid only when returns true).
 * @return         true when a new bit is ready in *bit.
 */
bool psk_feed(psk_t *p, uint8_t interval, bool *bit);

#ifdef __cplusplus
}
#endif
