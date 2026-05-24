/*
 * app_standalone.c
 *
 * Framework: state machine, mode registry, FDS persistence, button dispatch.
 * Chord detection lives in app_main.c and feeds app_standalone_on_button().
 *
 * Persistence layout (FDS file 0x1010 = FDS_STANDALONE_FILE_ID):
 *   key 0x0001          : state record (mode + flags, 4 bytes)
 *   key 0x0100 + mode   : per-mode config blob (variable, <= 64 bytes)
 */

#include "app_standalone.h"

#include <string.h>

#include "nrf_log.h"
#include "fds_util.h"
#include "fds_ids.h"

/* -------------------------------------------------------------------------
 * Compile-time mode enablement
 * Disable a mode by undef'ing its symbol in the build.
 * ------------------------------------------------------------------------- */

#ifndef CONFIG_STANDALONE_AUTHTRACE
#define CONFIG_STANDALONE_AUTHTRACE   1
#endif
#ifndef CONFIG_STANDALONE_SLOT_CYCLE
#define CONFIG_STANDALONE_SLOT_CYCLE  1
#endif
#ifndef CONFIG_STANDALONE_AUTOCLONE
#define CONFIG_STANDALONE_AUTOCLONE   0  /* not yet implemented */
#endif
#ifndef CONFIG_STANDALONE_READ_REPLAY
#define CONFIG_STANDALONE_READ_REPLAY 0  /* not yet implemented */
#endif
#ifndef CONFIG_STANDALONE_DICT_CHECK
#define CONFIG_STANDALONE_DICT_CHECK  0  /* not yet implemented */
#endif

/* -------------------------------------------------------------------------
 * FDS record keys (file ID FDS_STANDALONE_FILE_ID defined in fds_ids.h)
 * ------------------------------------------------------------------------- */

#define FDS_KEY_STANDALONE_STATE        0x0001
#define FDS_KEY_STANDALONE_CONFIG_BASE  0x0100   /* + mode_id */

#define STANDALONE_CONFIG_MAX_BYTES     64
#define STANDALONE_TICK_THROTTLE_MS     100      /* mode on_tick rate */

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  mode;
    uint8_t  flags;
    uint8_t  reserved;
} standalone_persist_t;

_Static_assert(sizeof(standalone_persist_t) == 4,
               "standalone_persist_t must be 4 bytes (FDS word alignment)");

#define STANDALONE_PERSIST_VERSION  1

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static struct {
    standalone_state_t  state;
    standalone_mode_t   mode;
    uint8_t             flags;
    uint32_t            last_tick_ticks;
    bool                initialised;
} m_ctx;

/* -------------------------------------------------------------------------
 * Mode registry
 * ------------------------------------------------------------------------- */

static const standalone_mode_iface_t * const m_modes[] = {
#if CONFIG_STANDALONE_AUTOCLONE
    &mode_autoclone_iface,
#endif
#if CONFIG_STANDALONE_READ_REPLAY
    &mode_read_replay_iface,
#endif
#if CONFIG_STANDALONE_AUTHTRACE
    &mode_authtrace_iface,
#endif
#if CONFIG_STANDALONE_SLOT_CYCLE
    &mode_slot_cycle_iface,
#endif
#if CONFIG_STANDALONE_DICT_CHECK
    &mode_dict_check_iface,
#endif
};

#define MODE_COUNT  (sizeof(m_modes) / sizeof(m_modes[0]))

static const standalone_mode_iface_t *find_mode(standalone_mode_t id) {
    for (size_t i = 0; i < MODE_COUNT; i++) {
        if (m_modes[i] != NULL && m_modes[i]->id == id) return m_modes[i];
    }
    return NULL;
}

static const standalone_mode_iface_t *active_mode(void) {
    if (m_ctx.mode == STANDALONE_MODE_DISABLED) return NULL;
    return find_mode(m_ctx.mode);
}

/* -------------------------------------------------------------------------
 * Persistence (synchronous FDS - same pattern as settings.c)
 * ------------------------------------------------------------------------- */

static standalone_rc_t persist_state_save(void) {
    standalone_persist_t rec = {
        .version  = STANDALONE_PERSIST_VERSION,
        .mode     = (uint8_t)m_ctx.mode,
        .flags    = m_ctx.flags,
        .reserved = 0,
    };
    bool ok = fds_write_sync(FDS_STANDALONE_FILE_ID,
                             FDS_KEY_STANDALONE_STATE,
                             sizeof(rec), &rec);
    if (!ok) {
        NRF_LOG_WARNING("standalone: fds_write_sync(state) failed");
        return STANDALONE_RC_INTERNAL;
    }
    return STANDALONE_RC_OK;
}

static void persist_state_load(void) {
    standalone_persist_t rec = { 0 };
    uint16_t len = sizeof(rec);
    bool ok = fds_read_sync(FDS_STANDALONE_FILE_ID,
                            FDS_KEY_STANDALONE_STATE,
                            &len, (uint8_t *)&rec);
    if (!ok || len != sizeof(rec) ||
        rec.version != STANDALONE_PERSIST_VERSION ||
        rec.mode    >= STANDALONE_MODE__COUNT) {
        m_ctx.mode  = STANDALONE_MODE_DISABLED;
        m_ctx.flags = 0;
        return;
    }
    m_ctx.mode  = (standalone_mode_t)rec.mode;
    m_ctx.flags = rec.flags;
}

static standalone_rc_t persist_config_save(standalone_mode_t mode,
                                           const uint8_t *cfg, size_t len) {
    if (len > STANDALONE_CONFIG_MAX_BYTES) return STANDALONE_RC_INVALID_CFG;
    if (len == 0) return STANDALONE_RC_OK;   /* nothing to write */

    bool ok = fds_write_sync(FDS_STANDALONE_FILE_ID,
                             FDS_KEY_STANDALONE_CONFIG_BASE + (uint16_t)mode,
                             (uint16_t)len, (void *)cfg);
    if (!ok) {
        NRF_LOG_WARNING("standalone: fds_write_sync(cfg %u) failed", mode);
        return STANDALONE_RC_INTERNAL;
    }
    return STANDALONE_RC_OK;
}

static standalone_rc_t persist_config_load(standalone_mode_t mode,
                                           uint8_t *out, size_t out_max,
                                           size_t *out_len) {
    if (out == NULL || out_len == NULL) return STANDALONE_RC_INVALID_CFG;
    uint16_t len = (uint16_t)out_max;
    bool ok = fds_read_sync(FDS_STANDALONE_FILE_ID,
                            FDS_KEY_STANDALONE_CONFIG_BASE + (uint16_t)mode,
                            &len, out);
    if (!ok) {
        *out_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }
    *out_len = len;
    return STANDALONE_RC_OK;
}

/* -------------------------------------------------------------------------
 * Capability gate
 * ------------------------------------------------------------------------- */

static bool mode_permitted(const standalone_mode_iface_t *m, uint8_t flags) {
    if (m == NULL) return false;
    if ((m->writes_tag || m->writes_slot) &&
        !(flags & STANDALONE_FLAG_HOST_OPTED_IN)) {
        return false;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Mode lifecycle
 * ------------------------------------------------------------------------- */

static standalone_rc_t enter_mode(const standalone_mode_iface_t *m) {
    if (m == NULL || m->on_enter == NULL) return STANDALONE_RC_OK;

    uint8_t cfg[STANDALONE_CONFIG_MAX_BYTES];
    size_t  cfg_len = 0;
    (void)persist_config_load(m->id, cfg, sizeof(cfg), &cfg_len);

    return m->on_enter(cfg_len ? cfg : NULL, cfg_len);
}

static standalone_rc_t exit_mode(const standalone_mode_iface_t *m) {
    if (m == NULL || m->on_exit == NULL) return STANDALONE_RC_OK;
    return m->on_exit();
}

/* -------------------------------------------------------------------------
 * State transitions
 * ------------------------------------------------------------------------- */

static void transition_arm(void) {
    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL) return;
    if (!mode_permitted(m, m_ctx.flags)) {
        NRF_LOG_WARNING("standalone: mode %u not permitted (missing opt-in)",
                        m_ctx.mode);
        return;
    }

    standalone_rc_t rc = enter_mode(m);
    if (rc != STANDALONE_RC_OK) {
        NRF_LOG_WARNING("standalone: on_enter returned %d", rc);
        return;
    }

    m_ctx.state = STANDALONE_STATE_ARMED_IDLE;
    NRF_LOG_INFO("standalone: armed in mode %u", m_ctx.mode);
}

static void transition_disarm(void) {
    const standalone_mode_iface_t *m = active_mode();
    (void)exit_mode(m);
    m_ctx.state = STANDALONE_STATE_DISARMED;
    NRF_LOG_INFO("standalone: disarmed");
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void app_standalone_init(void) {
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.state = STANDALONE_STATE_DISARMED;
    m_ctx.mode  = STANDALONE_MODE_DISABLED;
    persist_state_load();
    m_ctx.initialised = true;
    NRF_LOG_INFO("standalone: init, persisted mode=%u flags=0x%02x",
                 m_ctx.mode, m_ctx.flags);
}

standalone_state_t app_standalone_get_state(void) { return m_ctx.state; }
standalone_mode_t  app_standalone_get_mode (void) { return m_ctx.mode;  }
uint8_t            app_standalone_get_flags(void) { return m_ctx.flags; }

standalone_rc_t app_standalone_set_mode(standalone_mode_t mode, uint8_t flags) {
    if (mode >= STANDALONE_MODE__COUNT) return STANDALONE_RC_INVALID_CFG;

    if (mode == STANDALONE_MODE_DISABLED) {
        if (m_ctx.state != STANDALONE_STATE_DISARMED) transition_disarm();
        m_ctx.mode  = STANDALONE_MODE_DISABLED;
        m_ctx.flags = 0;
        return persist_state_save();
    }

    const standalone_mode_iface_t *m = find_mode(mode);
    if (m == NULL)                       return STANDALONE_RC_INVALID_CFG;
    if (!mode_permitted(m, flags))       return STANDALONE_RC_NOT_PERMITTED;

    if (m_ctx.state != STANDALONE_STATE_DISARMED && m_ctx.mode != mode) {
        (void)exit_mode(active_mode());
        m_ctx.state = STANDALONE_STATE_DISARMED;
    }

    m_ctx.mode  = mode;
    m_ctx.flags = flags;
    return persist_state_save();
}

standalone_rc_t app_standalone_set_config(standalone_mode_t mode,
                                          const uint8_t *cfg, size_t cfg_len) {
    if (mode >= STANDALONE_MODE__COUNT) return STANDALONE_RC_INVALID_CFG;
    if (cfg == NULL && cfg_len > 0)     return STANDALONE_RC_INVALID_CFG;
    return persist_config_save(mode, cfg, cfg_len);
}

standalone_rc_t app_standalone_get_config(standalone_mode_t mode,
                                          uint8_t *cfg, size_t cfg_max,
                                          size_t *cfg_len) {
    if (mode >= STANDALONE_MODE__COUNT) return STANDALONE_RC_INVALID_CFG;
    if (cfg == NULL || cfg_len == NULL) return STANDALONE_RC_INVALID_CFG;
    return persist_config_load(mode, cfg, cfg_max, cfg_len);
}

bool app_standalone_on_button(standalone_button_evt_t evt) {
    if (!m_ctx.initialised) return false;
    if (m_ctx.mode == STANDALONE_MODE_DISABLED) return false;

    if (evt == STANDALONE_BTN_BOTH_LONG) {
        if (m_ctx.state == STANDALONE_STATE_DISARMED) transition_arm();
        else                                          transition_disarm();
        return true;
    }

    if (m_ctx.state == STANDALONE_STATE_DISARMED) return false;

    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL || m->on_button == NULL) return false;

    standalone_rc_t rc = m->on_button(evt);
    if (rc != STANDALONE_RC_OK) {
        NRF_LOG_WARNING("standalone: mode rc=%d", rc);
    }
    return true;
}

void app_standalone_tick(uint32_t now_ticks) {
    if (!m_ctx.initialised) return;
    if (m_ctx.state == STANDALONE_STATE_DISARMED) return;

    /* Cheap throttle: use raw ticks deltas. The framework only needs
     * "approximately 10 Hz", exact wall-time isn't important. */
    if ((now_ticks - m_ctx.last_tick_ticks) < STANDALONE_TICK_THROTTLE_MS) return;
    m_ctx.last_tick_ticks = now_ticks;

    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL || !m->wants_tick || m->on_tick == NULL) return;
    (void)m->on_tick(now_ticks);
}

standalone_rc_t app_standalone_read_result(uint8_t *out, size_t out_max,
                                           size_t *out_len) {
    if (out == NULL || out_len == NULL) return STANDALONE_RC_INVALID_CFG;

    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL || m->read_result == NULL) {
        *out_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }
    return m->read_result(out, out_max, out_len);
}

standalone_rc_t app_standalone_clear_result(void) {
    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL || m->clear_result == NULL) return STANDALONE_RC_NO_RESULT;
    m->clear_result();
    return STANDALONE_RC_OK;
}

standalone_rc_t app_standalone_trigger(void) {
    if (m_ctx.mode == STANDALONE_MODE_DISABLED) return STANDALONE_RC_INVALID_STATE;
    if (m_ctx.state == STANDALONE_STATE_DISARMED) {
        transition_arm();
        if (m_ctx.state == STANDALONE_STATE_DISARMED) {
            return STANDALONE_RC_NOT_PERMITTED;
        }
    }

    const standalone_mode_iface_t *m = active_mode();
    if (m == NULL || m->on_button == NULL) return STANDALONE_RC_INVALID_STATE;
    return m->on_button(STANDALONE_BTN_BOTH_SHORT);
}
