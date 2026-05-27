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
#include "standalone_led.h"

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
#ifndef CONFIG_STANDALONE_EMUL_TRACE
#define CONFIG_STANDALONE_EMUL_TRACE  1
#endif

/* -------------------------------------------------------------------------
 * FDS record keys (file ID FDS_STANDALONE_FILE_ID defined in fds_ids.h)
 * ------------------------------------------------------------------------- */

#define FDS_KEY_STANDALONE_STATE        0x0001
#define FDS_KEY_STANDALONE_CONFIG_BASE  0x0100   /* + mode_id */
/* Result buffer key base — matches app_standalone.h declaration */
/* FDS_KEY_STANDALONE_RESULT_BASE = 0x0200 defined in app_standalone.h */

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
#if CONFIG_STANDALONE_AUTHTRACE && defined(PROJECT_CHAMELEON_ULTRA)
    &mode_authtrace_iface,
#endif
#if CONFIG_STANDALONE_SLOT_CYCLE
    &mode_slot_cycle_iface,
#endif
#if CONFIG_STANDALONE_DICT_CHECK
    &mode_dict_check_iface,
#endif
#if CONFIG_STANDALONE_EMUL_TRACE
    &mode_emultrace_iface,
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
    standalone_persist_t rec __attribute__((aligned(4))) = {
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

/* FDS write staging buffer for config records. Living in .bss guarantees
 * 4-byte alignment by the linker (uint32_t backing storage). Stack-local
 * arrays with __attribute__((aligned(4))) *should* work too but we hit a
 * runtime crash with that approach on first-time SET_CONFIG; the static
 * buffer matches the settings.c pattern that is known to work. */
static uint32_t m_cfg_save_buf[(STANDALONE_CONFIG_MAX_BYTES + 3) / 4];

static standalone_rc_t persist_config_save(standalone_mode_t mode,
                                           const uint8_t *cfg, size_t len) {
    if (len > STANDALONE_CONFIG_MAX_BYTES) return STANDALONE_RC_INVALID_CFG;
    if (len == 0) return STANDALONE_RC_OK;   /* nothing to write */

    memset(m_cfg_save_buf, 0, sizeof(m_cfg_save_buf));
    memcpy(m_cfg_save_buf, cfg, len);

    bool ok = fds_write_sync(FDS_STANDALONE_FILE_ID,
                             FDS_KEY_STANDALONE_CONFIG_BASE + (uint16_t)mode,
                             (uint16_t)len, m_cfg_save_buf);
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
 * Result-buffer persistence
 *
 * Wire format in flash: [u32 byte_len_le][byte_len bytes of buffer data]
 * byte_len == 0 means the record exists but the buffer is cleared.
 * A missing record is treated identically to byte_len == 0.
 *
 * Uses a file-static staging buffer so fds_write_sync always gets a
 * word-aligned pointer (same pattern as m_cfg_save_buf above).
 * ------------------------------------------------------------------------- */
static uint32_t m_result_save_buf[(STANDALONE_RESULT_PERSIST_MAX + 3) / 4];

standalone_rc_t app_standalone_save_result_buf(standalone_mode_t mode,
                                               const uint32_t *buf_words,
                                               size_t byte_len) {
    if (mode >= STANDALONE_MODE__COUNT) return STANDALONE_RC_INVALID_CFG;

    /* Pack header + data into the staging buffer */
    size_t total = 4 + byte_len;
    if (total > STANDALONE_RESULT_PERSIST_MAX) {
        NRF_LOG_WARNING("standalone: result too large to persist (%u bytes)", byte_len);
        byte_len = STANDALONE_RESULT_PERSIST_MAX - 4;
        total    = STANDALONE_RESULT_PERSIST_MAX;
    }

    uint8_t *p = (uint8_t *)m_result_save_buf;
    p[0] = (uint8_t)(byte_len      );
    p[1] = (uint8_t)(byte_len >>  8);
    p[2] = (uint8_t)(byte_len >> 16);
    p[3] = (uint8_t)(byte_len >> 24);
    if (byte_len > 0 && buf_words != NULL) {
        memcpy(p + 4, buf_words, byte_len);
    }

    bool ok = fds_write_sync(FDS_STANDALONE_FILE_ID,
                             FDS_KEY_STANDALONE_RESULT_BASE + (uint16_t)mode,
                             (uint16_t)total, m_result_save_buf);
    if (!ok) {
        NRF_LOG_WARNING("standalone: fds_write_sync(result %u) failed", mode);
        return STANDALONE_RC_INTERNAL;
    }
    NRF_LOG_DEBUG("standalone: persisted %u result bytes for mode %u", byte_len, mode);
    return STANDALONE_RC_OK;
}

standalone_rc_t app_standalone_load_result_buf(standalone_mode_t mode,
                                               uint32_t *buf_words,
                                               size_t word_buf_bytes,
                                               size_t *out_byte_len) {
    if (mode >= STANDALONE_MODE__COUNT) return STANDALONE_RC_INVALID_CFG;
    if (buf_words == NULL || out_byte_len == NULL) return STANDALONE_RC_INVALID_CFG;

    uint16_t len = (uint16_t)sizeof(m_result_save_buf);
    bool ok = fds_read_sync(FDS_STANDALONE_FILE_ID,
                            FDS_KEY_STANDALONE_RESULT_BASE + (uint16_t)mode,
                            &len, (uint8_t *)m_result_save_buf);
    if (!ok || len < 4) {
        *out_byte_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }

    uint8_t *p = (uint8_t *)m_result_save_buf;
    size_t data_len = (size_t)p[0]
                    | ((size_t)p[1] <<  8)
                    | ((size_t)p[2] << 16)
                    | ((size_t)p[3] << 24);

    if (data_len == 0) {
        *out_byte_len = 0;
        return STANDALONE_RC_NO_RESULT;
    }

    if (data_len > word_buf_bytes) {
        NRF_LOG_WARNING("standalone: persisted result %u > buf %u, truncating",
                        data_len, word_buf_bytes);
        data_len = word_buf_bytes;
    }

    memcpy(buf_words, p + 4, data_len);
    *out_byte_len = data_len;
    NRF_LOG_DEBUG("standalone: loaded %u result bytes for mode %u", data_len, mode);
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
        standalone_feedback(SL_FB_DENIED);
        return;
    }

    standalone_rc_t rc = enter_mode(m);
    if (rc != STANDALONE_RC_OK) {
        NRF_LOG_WARNING("standalone: on_enter returned %d", rc);
        standalone_feedback(SL_FB_ERROR);
        return;
    }

    m_ctx.state = STANDALONE_STATE_ARMED_IDLE;
    NRF_LOG_INFO("standalone: armed in mode %u", m_ctx.mode);
    standalone_feedback(SL_FB_ARMED);
}

static void transition_disarm(void) {
    const standalone_mode_iface_t *m = active_mode();
    (void)exit_mode(m);
    m_ctx.state = STANDALONE_STATE_DISARMED;
    NRF_LOG_INFO("standalone: disarmed");
    standalone_feedback(SL_FB_DISARMED);
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

size_t app_standalone_get_stored_size(standalone_mode_t mode) {
    if (mode >= STANDALONE_MODE__COUNT) return 0;
    uint32_t hdr = 0;
    uint16_t len = 4;
    bool ok = fds_read_sync(FDS_STANDALONE_FILE_ID,
                            FDS_KEY_STANDALONE_RESULT_BASE + (uint16_t)mode,
                            &len, (uint8_t *)&hdr);
    if (!ok || len < 4) return 0;
    size_t byte_len = (size_t)((hdr >>  0) & 0xFF)
                    | (size_t)((hdr >>  8) & 0xFF) << 8
                    | (size_t)((hdr >> 16) & 0xFF) << 16
                    | (size_t)((hdr >> 24) & 0xFF) << 24;
    return byte_len;
}

size_t app_standalone_get_result_avail(standalone_mode_t mode) {
    if (mode >= STANDALONE_MODE__COUNT) return 0;
    const standalone_mode_iface_t *m = find_mode(mode);
    if (m == NULL || m->get_result_size == NULL) return 0;
    /* Trigger lazy-load from FDS into the mode's RAM buffer if not already
     * done.  Each mode has its own static buffer so multiple modes can be
     * loaded simultaneously without conflict. */
    if (m->ensure_loaded != NULL) m->ensure_loaded();
    return m->get_result_size();
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
