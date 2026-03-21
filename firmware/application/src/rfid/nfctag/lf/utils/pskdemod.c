#include "pskdemod.h"

#include <stdlib.h>
#include <string.h>

/*
 * PSK demodulator for ChameleonUltra LF tag emulation stack.
 *
 * Signal model
 * ============
 * The 125 kHz carrier is amplitude-modulated by a square-wave subcarrier.
 * The subcarrier frequency is carrier / rf_div (typically rf_div = 4,
 * giving a 31.25 kHz subcarrier for Indala/Keri).  The GPIOTE edge-
 * capture layer gives us the time between successive envelope edges in
 * units of 125 kHz carrier cycles.
 *
 * One data bit spans rf_div subcarrier half-periods (= rf_div² carrier
 * cycles).  For rf_div=4: 4 half-periods × 4 cycles = 16 cycles per bit.
 *
 * Phase encoding
 * ==============
 * A 180° phase shift is visible as a SHORT-SHORT interval pair or a
 * LONG interval straddling a half-period boundary.
 *
 * PSK1 (absolute):
 *   An odd number of phase shifts in a bit window → bit = '1'.
 *   Even (including zero) → bit = '0'.
 *
 * PSK2 (differential BPSK):
 *   A phase shift at the bit boundary → bit = '1'; no shift → '0'.
 *   From the demodulator's point of view this is identical to PSK1:
 *   we just count whether a shift occurred (shift_count & 1).
 *
 * PSK3 (differential BPSK, inverted):
 *   Same as PSK2 with the bit sense inverted.
 *
 * Interval classification
 * =======================
 * Each call to psk_feed() provides one edge-to-edge interval.
 *
 *   NORMAL ≈ half_period          — regular subcarrier edge, no shift
 *   SHORT  ≈ half_period / 2      — first half of a phase-shifted pair
 *   LONG   ≈ 3 * half_period / 2  — phase shift straddled a boundary
 *   BAD    — none of the above; triggers full resync
 *
 * A phase shift is represented in the signal as either:
 *   (a) A SHORT followed by another SHORT — together they total one
 *       nominal half-period but with the subcarrier phase flipped.
 *   (b) A LONG interval — the subcarrier edge was "pulled" across the
 *       half-period boundary, equivalent to one shift.
 *
 * SHORT pairing
 * =============
 * We use a two-state machine (in_short flag).  When the first SHORT
 * arrives we set in_short=true and wait.  When the paired second SHORT
 * arrives we count one half-period and one phase shift.  If a non-SHORT
 * interval arrives while in_short=true, we declare a desync.
 *
 * Jitter
 * ======
 * jitter = max(half_period / PSK_JITTER_DIV, 1).
 *
 * For rf_div=2 (half_period=2): jitter=1 would make NORMAL and SHORT
 * windows overlap.  We therefore classify by checking SHORT *before*
 * NORMAL only when the interval is strictly less than half_period.
 * Concretely: interval < half_period → try SHORT; otherwise try NORMAL.
 * This is safe because a genuine SHORT (phase shift mid-period) always
 * produces an interval shorter than a genuine NORMAL.
 */

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

typedef enum {
    ITYPE_NORMAL,
    ITYPE_SHORT,
    ITYPE_LONG,
    ITYPE_BAD,
} itype_t;

static itype_t classify_interval(const psk_t *p, uint8_t interval) {
    uint8_t hp      = p->half_period;
    uint8_t jitter  = p->jitter;
    uint8_t short_n = hp / 2;
    if (short_n == 0) short_n = 1;
    uint8_t long_n  = hp + short_n;   /* 3/2 × half_period */

    /*
     * Use interval < hp as the primary discriminant between SHORT and NORMAL.
     * This avoids overlap when hp is small (rf_div=2).
     */
    if (interval < hp) {
        /* Could be SHORT or the low tail of NORMAL jitter */
        int16_t ds = (int16_t)interval - (int16_t)short_n;
        if (ds < 0) ds = -ds;
        if ((uint8_t)ds <= jitter) {
            return ITYPE_SHORT;
        }
        /* Low tail of NORMAL */
        int16_t dn = (int16_t)interval - (int16_t)hp;
        if (dn < 0) dn = -dn;
        if ((uint8_t)dn <= jitter) {
            return ITYPE_NORMAL;
        }
        return ITYPE_BAD;
    } else {
        /* interval >= hp: could be NORMAL or LONG */
        int16_t dn = (int16_t)interval - (int16_t)hp;
        if (dn < 0) dn = -dn;
        if ((uint8_t)dn <= jitter) {
            return ITYPE_NORMAL;
        }
        int16_t dl = (int16_t)interval - (int16_t)long_n;
        if (dl < 0) dl = -dl;
        /* Allow 2× jitter for LONG since it covers a wider window */
        if ((uint8_t)dl <= (uint8_t)(jitter * 2)) {
            return ITYPE_LONG;
        }
        return ITYPE_BAD;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

psk_t *psk_alloc(psk_rf_div_t rf_div, psk_mode_t mode) {
    psk_t *p = (psk_t *)malloc(sizeof(psk_t));
    if (p == NULL) {
        return NULL;
    }
    p->rf_div      = rf_div;
    p->mode        = mode;
    p->half_period = (uint8_t)rf_div;
    p->jitter      = p->half_period / PSK_JITTER_DIV;
    if (p->jitter == 0) {
        p->jitter = 1;
    }
    psk_reset(p);
    return p;
}

void psk_free(psk_t *p) {
    if (p != NULL) {
        free(p);
    }
}

void psk_reset(psk_t *p) {
    p->synced      = false;
    p->sync_count  = 0;
    p->clk_count   = 0;
    p->shift_count = 0;
    p->in_short    = false;
}

bool psk_feed(psk_t *p, uint8_t interval, bool *bit) {

    itype_t itype = classify_interval(p, interval);

    if (itype == ITYPE_BAD) {
        psk_reset(p);
        return false;
    }

    /* ----------------------------------------------------------------
     * SHORT pairing: a phase shift in PSK is a SHORT followed by
     * another SHORT.  Enforce this contract strictly.
     * -------------------------------------------------------------- */
    if (p->in_short) {
        if (itype != ITYPE_SHORT) {
            /* Expected second SHORT but got something else → desync */
            psk_reset(p);
            return false;
        }
        /* Paired SHORT received: counts as one half-period + one shift */
        p->in_short = false;
        itype = ITYPE_NORMAL;   /* handle via normal path below */
        p->shift_count++;
    } else if (itype == ITYPE_SHORT) {
        /* First SHORT of a pair — stash and wait */
        p->in_short = true;
        return false;
    }

    /* ----------------------------------------------------------------
     * Synchronisation: require PSK_SYNC_THRESHOLD clean NORMAL edges
     * before emitting bits.
     * -------------------------------------------------------------- */
    if (!p->synced) {
        if (itype == ITYPE_NORMAL) {
            p->sync_count++;
            if (p->sync_count >= PSK_SYNC_THRESHOLD) {
                p->synced      = true;
                p->clk_count   = 0;
                p->shift_count = 0;
            }
        } else {
            /* LONG during sync hunt — reset */
            p->sync_count = 0;
        }
        return false;
    }

    /* ----------------------------------------------------------------
     * Bit-window accumulation.
     *
     * NORMAL: one half-period, no shift (shift already counted above
     *         for SHORT pairs).
     * LONG:   straddles one half-period boundary, counts as one
     *         half-period and one phase shift.
     *
     * We use >= instead of == when testing clk_count against rf_div so
     * a LONG that pushes clk_count over the boundary still fires the
     * bit-ready path rather than silently accumulating.
     * -------------------------------------------------------------- */
    if (itype == ITYPE_LONG) {
        p->clk_count++;
        p->shift_count++;
    } else {
        /* ITYPE_NORMAL (includes completed SHORT pairs) */
        p->clk_count++;
    }

    if (p->clk_count < (uint8_t)p->rf_div) {
        return false;
    }

    /* End of bit window — extract bit */
    bool phase_shifted = (p->shift_count & 1) != 0;

    /* Reset for next bit window */
    p->clk_count   = 0;
    p->shift_count = 0;

    switch (p->mode) {
        case PSK_MODE_1:
            /*
             * PSK1 (absolute): odd number of phase shifts → '1'.
             */
            *bit = phase_shifted;
            break;

        case PSK_MODE_2:
            /*
             * PSK2 (differential BPSK): a phase shift at the bit boundary
             * encodes '1', no shift encodes '0'.
             * From the demodulator's perspective this is the same as PSK1
             * — we detect whether a shift occurred in this window.
             */
            *bit = phase_shifted;
            break;

        case PSK_MODE_3:
            /*
             * PSK3: same as PSK2 with inverted polarity.
             */
            *bit = !phase_shifted;
            break;

        default:
            return false;
    }

    return true;
}
