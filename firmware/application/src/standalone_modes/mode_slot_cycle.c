/*
 * mode_slot_cycle.c
 *
 * Standalone mode: rotate through enabled emulation slots at a configurable
 * interval. Safe by design - no tag interaction, no slot writes, just calls
 * the existing slot-switch path.
 *
 * Reference implementation for the standalone_mode_iface_t contract.
 *
 * Config blob (6 bytes, host-supplied via DATA_CMD_STANDALONE_SET_CONFIG):
 *   u8  version       schema version, must be CFG_VERSION
 *   u8  slot_mask     bit N set => include slot N in rotation
 *   u8  start_slot    slot to begin in (must be set in slot_mask)
 *   u8  reserved
 *   u16 interval_ms   dwell time per slot, 100..60000
 *
 * Button mapping while armed (chord-only):
 *   BOTH_SHORT   manually advance (primary - also resumes if paused)
 *   BOTH_LONG    arm/disarm (handled by framework)
 *   BOTH_VLONG   pause / resume rotation
 */

#include "app_standalone.h"

#include <string.h>

#include "nrf_log.h"
#include "rfid_main.h"
#include "tag_emulation.h"

#define CFG_VERSION              1
#define CFG_DEFAULT_INTERVAL_MS  3000
#define CFG_DEFAULT_SLOT_MASK    0xFF
#define CFG_DEFAULT_START_SLOT   0

#define INTERVAL_MIN_MS          100
#define INTERVAL_MAX_MS          60000
#define SLOT_COUNT               8

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  slot_mask;
    uint8_t  start_slot;
    uint8_t  reserved;
    uint16_t interval_ms;
} cfg_t;

static struct {
    cfg_t    cfg;
    uint8_t  current_slot;
    uint32_t last_switch_ms;
    bool     paused;
    bool     active;
} m_state;

/* -------------------------------------------------------------------------
 * Config helpers
 * ------------------------------------------------------------------------- */

static void apply_defaults(cfg_t *c) {
    c->version     = CFG_VERSION;
    c->slot_mask   = CFG_DEFAULT_SLOT_MASK;
    c->start_slot  = CFG_DEFAULT_START_SLOT;
    c->reserved    = 0;
    c->interval_ms = CFG_DEFAULT_INTERVAL_MS;
}

static bool cfg_valid(const cfg_t *c) {
    if (c->version != CFG_VERSION)         return false;
    if (c->slot_mask == 0)                 return false;
    if (c->start_slot >= SLOT_COUNT)       return false;
    if (c->interval_ms < INTERVAL_MIN_MS)  return false;
    if (c->interval_ms > INTERVAL_MAX_MS)  return false;
    return true;
}

static uint8_t next_enabled(uint8_t from, uint8_t mask) {
    for (uint8_t step = 1; step <= SLOT_COUNT; step++) {
        uint8_t s = (from + step) % SLOT_COUNT;
        if (mask & (1u << s)) return s;
    }
    return from;
}

static standalone_rc_t switch_to_slot(uint8_t slot) {
    NRF_LOG_DEBUG("slot_cycle: -> slot %u", slot);
    tag_emulation_change_slot(slot, true);
    m_state.current_slot = slot;
    return STANDALONE_RC_OK;
}

/* -------------------------------------------------------------------------
 * Lifecycle callbacks
 * ------------------------------------------------------------------------- */

static standalone_rc_t on_enter(const uint8_t *cfg, size_t cfg_len) {
    apply_defaults(&m_state.cfg);

    if (cfg != NULL && cfg_len == sizeof(cfg_t)) {
        cfg_t parsed;
        memcpy(&parsed, cfg, sizeof(parsed));
        if (cfg_valid(&parsed)) m_state.cfg = parsed;
        else NRF_LOG_WARNING("slot_cycle: invalid cfg, using defaults");
    } else if (cfg_len != 0) {
        NRF_LOG_WARNING("slot_cycle: cfg size %u != %u, using defaults",
                        (unsigned)cfg_len, (unsigned)sizeof(cfg_t));
    }

    uint8_t start = m_state.cfg.start_slot;
    if (!(m_state.cfg.slot_mask & (1u << start))) {
        start = next_enabled(start, m_state.cfg.slot_mask);
    }

    m_state.current_slot   = start;
    m_state.last_switch_ms = 0;
    m_state.paused         = false;
    m_state.active         = true;

    NRF_LOG_INFO("slot_cycle: start slot=%u mask=0x%02x interval=%ums",
                 start, m_state.cfg.slot_mask, m_state.cfg.interval_ms);
    return switch_to_slot(start);
}

static standalone_rc_t on_exit(void) {
    m_state.active = false;
    return STANDALONE_RC_OK;
}

static standalone_rc_t on_tick(uint32_t now_ms) {
    if (!m_state.active || m_state.paused) return STANDALONE_RC_OK;

    if (m_state.last_switch_ms == 0) {
        m_state.last_switch_ms = now_ms ? now_ms : 1;
        return STANDALONE_RC_OK;
    }

    if ((now_ms - m_state.last_switch_ms) < m_state.cfg.interval_ms) {
        return STANDALONE_RC_OK;
    }

    uint8_t next = next_enabled(m_state.current_slot, m_state.cfg.slot_mask);
    m_state.last_switch_ms = now_ms;
    if (next == m_state.current_slot) return STANDALONE_RC_OK;
    return switch_to_slot(next);
}

static standalone_rc_t on_button(standalone_button_evt_t evt) {
    if (!m_state.active) return STANDALONE_RC_INVALID_STATE;

    switch (evt) {
        case STANDALONE_BTN_BOTH_SHORT: {       /* primary: advance slot */
            uint8_t next = next_enabled(m_state.current_slot,
                                        m_state.cfg.slot_mask);
            m_state.paused = false;
            m_state.last_switch_ms = 0;
            return switch_to_slot(next);
        }

        case STANDALONE_BTN_BOTH_VLONG:         /* pause/resume */
            m_state.paused = !m_state.paused;
            NRF_LOG_INFO("slot_cycle: %s",
                         m_state.paused ? "paused" : "resumed");
            if (!m_state.paused) m_state.last_switch_ms = 0;
            return STANDALONE_RC_OK;

        case STANDALONE_BTN_BOTH_LONG:
            /* arm/disarm handled by framework */
        default:
            return STANDALONE_RC_OK;
    }
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */

const standalone_mode_iface_t mode_slot_cycle_iface = {
    .id              = STANDALONE_MODE_SLOT_CYCLE,
    .name            = "slot_cycle",
    .writes_tag      = false,
    .writes_slot     = false,
    .wants_tick      = true,
    .on_enter        = on_enter,
    .on_exit         = on_exit,
    .on_button       = on_button,
    .on_tick         = on_tick,
    .get_result_size = NULL,
    .read_result     = NULL,
    .clear_result    = NULL,
};
