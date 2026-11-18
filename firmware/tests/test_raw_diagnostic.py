"""Compile actual LF lifecycle C functions with host PWM/GPIO doubles, no hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LF = ROOT / "application/src/rfid/nfctag/lf"


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RawDiagnostic(unittest.TestCase):
    def test_mode_toggle_preserves_loaded_sequence(self):
        source = (LF / "lf_tag_em.c").read_text()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
static int sequence, m_broadcast;
static void *m_pwm_seq = &sequence;
static bool m_pwm_initialized = true, m_lpcomp_enabled = true;
static bool m_is_lf_emulating = true, g_is_tag_emulating = true;
static bool m_cancel_playback, led = true;
static unsigned starts;
enum { LF_SENSE_STATE_NONE, LF_SENSE_STATE_DISABLE, LF_SENSE_STATE_ENABLE };
static int m_lf_sense_state = LF_SENSE_STATE_ENABLE;
#define LF_MOD 1
#define TAG_FIELD_LED_OFF() led = false;
#define ANT_NO_MOD() ((void)0)
static void nrf_gpio_cfg_output(int pin) { (void)pin; }
static void playback_cancel(void) { m_cancel_playback = true; }
static void nrfx_pwm_uninit(void *p) { (void)p; }
static void lf_lpcomp_enable(void) { m_lpcomp_enabled = true; }
static void nrfx_lpcomp_uninit(void) {}
static void sd_clock_hfclk_release(void) {}
static void lf_sense_enable(void) {
    assert(m_pwm_seq == &sequence);
    assert(!m_is_lf_emulating && !g_is_tag_emulating);
    m_cancel_playback = false;
    m_pwm_initialized = true;
    ++starts;
}
'''
        code += function(source, "static void lf_sense_disable(void)")
        code += function(source, "void lf_tag_125khz_sense_switch(bool enable)")
        code += r'''
int main(void) {
    for (unsigned i = 0; i < 3; ++i) {
        lf_tag_125khz_sense_switch(false);
        assert(!m_pwm_initialized && !m_is_lf_emulating && !g_is_tag_emulating && !led);
        assert(m_pwm_seq == &sequence);
        lf_tag_125khz_sense_switch(true);
        assert(m_pwm_initialized && !m_cancel_playback && starts == i + 1);
    }
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cfile = Path(temp) / "toggle.c"
            binary = Path(temp) / "toggle"
            cfile.write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(cfile), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_actual_c_lifecycle(self):
        source = (LF / "lf_tag_em.c").read_text()
        sdk = (ROOT / "nrf52_sdk/modules/nrfx/drivers/src/nrfx_lpcomp.c").read_text()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "raw_manchester.h"
typedef enum { LF_RAW_DIAGNOSTIC_IDLE, LF_RAW_DIAGNOSTIC_RUNNING,
               LF_RAW_DIAGNOSTIC_STOPPED } lf_raw_diagnostic_status_t;
enum {TAG_TYPE_UNDEFINED, TAG_TYPE_RAW_MANCHESTER};
enum {NRFX_PWM_EVT_STOPPED, NRFX_PWM_FLAG_STOP};
typedef int nrfx_pwm_evt_type_t;
static int m_broadcast, m_tag_type = TAG_TYPE_RAW_MANCHESTER;
static struct { unsigned length; } m_raw_seq = {4};
static void *m_pwm_seq = &m_raw_seq;
static struct { uint16_t counter_top; } m_raw_pwm[228] = {{32}};
static volatile lf_raw_diagnostic_status_t m_raw_status;
static volatile bool m_cancel_playback;
static bool m_pwm_initialized = true;
static bool m_diagnostic_owned, m_is_lf_emulating, g_is_tag_emulating;
static bool m_lpcomp_enabled = true;
enum { NRFX_DRV_STATE_UNINITIALIZED, NRFX_DRV_STATE_INITIALIZED, NRFX_DRV_STATE_POWERED_ON };
static int m_state = NRFX_DRV_STATE_POWERED_ON;
#define sdk_lpcomp_enabled (m_state == NRFX_DRV_STATE_POWERED_ON)
#define NRFX_ASSERT assert
#define NRFX_LOG_INFO(...) ((void)0)
#define NRFX_CHECK(x) 0
#define NRFX_IRQ_DISABLE(x) ((void)0)
#define NRF_LPCOMP_TASK_START 1
#define NRF_LPCOMP_TASK_STOP 2
static void *m_lpcomp_event_handler;
static unsigned lpcomp_enables, lpcomp_disables, pwm_uninits, clock_releases;
static void nrf_lpcomp_enable(void) { ++lpcomp_enables; }
static void nrf_lpcomp_disable(void) { ++lpcomp_disables; }
static void nrf_lpcomp_task_trigger(int task) { (void)task; }
static void nrfx_lpcomp_disable(void);
static bool g_usb_led_marquee_enable, field, led;
static unsigned device_mode;
#define DEVICE_MODE_READER 1
#define RGB_BLUE 1
#define NRF_LPCOMP_EVENT_UP 1
#define NRF_LOG_INFO(...) ((void)0)
#define TAG_FIELD_LED_ON() led = true;
#define TAG_FIELD_LED_OFF() led = false;
typedef int nrf_lpcomp_event_t;
static unsigned get_device_mode(void) { return device_mode; }
static void nrfx_pwm_uninit(void *p) { (void)p; assert(m_cancel_playback && m_pwm_initialized); ++pwm_uninits; }
static void sd_clock_hfclk_release(void) { ++clock_releases; }
static void pwm_handler(nrfx_pwm_evt_type_t event_type);
static bool inject_stop;
#define CRITICAL_REGION_ENTER() do { if (inject_stop) { inject_stop = false; pwm_handler(NRFX_PWM_EVT_STOPPED); }
#define CRITICAL_REGION_EXIT() } while (0)
static void sleep_timer_stop(void) {}
static void set_slot_light_color(unsigned n) { (void)n; }
static unsigned irq, stops, starts, count, gpio;
#define __get_IPSR() irq
#define ANT_NO_MOD() (++gpio)
static bool nrfx_pwm_stop(void *p, bool wait) {
    (void)p;
    assert(wait && m_cancel_playback);
    ++stops;
    return true;
}
static void nrfx_pwm_simple_playback(void *p, void *s, unsigned n, unsigned flags) {
    (void)p; (void)s;
    assert(flags == NRFX_PWM_FLAG_STOP && !m_cancel_playback);
    ++starts; count = n;
}
static void bsp_delay_ms(unsigned n) { (void)n; }
static bool is_lf_field_exists(void) { return field; }
static void lf_field_lost(void) { m_is_lf_emulating = false; g_is_tag_emulating = false; led = false; }
typedef int tag_specific_type_t;
typedef int tag_data_buffer_t;
static unsigned disabled, enabled;
static int encoded_length = 32;
static void lf_tag_125khz_sense_switch(bool enable) {
    if (enable) { ++enabled; m_pwm_initialized = true; m_cancel_playback = false; }
    else { ++disabled; m_pwm_initialized = false; }
}
static int lf_tag_data_encode(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    (void)buffer;
    assert(m_cancel_playback && !m_pwm_initialized);
    if (encoded_length) { m_pwm_seq = &m_raw_seq; m_tag_type = type; }
    return encoded_length;
}
'''
        for name in ("disable", "enable", "uninit"):
            code += "\n" + function(sdk, f"void nrfx_lpcomp_{name}(void)")
        for signature in (
            "static void lf_lpcomp_enable(void)",
            "static void lf_lpcomp_disable(void)",
            "static void diagnostic_release(void)",
            "static void playback_cancel(void)",
            "static void lf_sense_disable(void)",
            "void lf_tag_raw_diagnostic_stop(void)",
            "lf_raw_diagnostic_status_t lf_tag_raw_diagnostic_status(void)",
            "bool lf_tag_raw_diagnostic_start(uint32_t ms)",
            "void lf_tag_data_clear(void)",
            "static void lpcomp_event_handler(nrf_lpcomp_event_t event)",
            "static void pwm_handler(nrfx_pwm_evt_type_t event_type)",
            "int lf_tag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer)",
        ):
            code += "\n" + function(source, signature)
        code += r'''
int main(void) {
    assert(!lf_tag_raw_diagnostic_start(0));
    assert(!lf_tag_raw_diagnostic_start(5001));
    assert(!starts && !stops);
    device_mode = DEVICE_MODE_READER;
    assert(!lf_tag_raw_diagnostic_start(10));
    device_mode = 0;
    irq = 1;
    assert(!lf_tag_raw_diagnostic_start(10));
    lf_tag_raw_diagnostic_stop();
    assert(!stops);
    irq = 0;
    assert(lf_tag_raw_diagnostic_start(5000));
    assert(count == 19531 && starts == 1 && stops == 1);
    assert(lf_tag_raw_diagnostic_status() == LF_RAW_DIAGNOSTIC_RUNNING);
    assert(!lf_tag_raw_diagnostic_start(10));
    assert(m_diagnostic_owned && led && g_is_tag_emulating);
    irq = 1;
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(starts == 1 && lf_tag_raw_diagnostic_status() == LF_RAW_DIAGNOSTIC_STOPPED);
    assert(!m_diagnostic_owned && !led && !g_is_tag_emulating);
    field = true;
    lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    assert(starts == 2 && count == 10);
    field = false;
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(!g_is_tag_emulating);
    // Natural STOPPED wins just before explicit stop claims ownership.
    irq = 0;
    assert(lf_tag_raw_diagnostic_start(1));
    inject_stop = true;
    lf_tag_raw_diagnostic_stop();
    assert(!m_diagnostic_owned && sdk_lpcomp_enabled);
    // Starting diagnosis while normal raw owns PWM must not double-disable LPCOMP.
    lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    assert(!sdk_lpcomp_enabled);
    assert(lf_tag_raw_diagnostic_start(1));
    lf_tag_raw_diagnostic_stop();
    assert(sdk_lpcomp_enabled);
    stops = 1;
    starts = 1; // Keep following cancellation assertions relative to diagnostic starts.
    irq = 0;
    assert(lf_tag_raw_diagnostic_start(1));
    lf_tag_raw_diagnostic_stop();
    assert(stops == 3 && !m_cancel_playback);
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(starts == 2);
    // Normal raw still starts on field and repeats finite ten-frame bursts.
    field = true;
    lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    assert(starts == 3 && count == 10 && led && g_is_tag_emulating);
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(starts == 4 && count == 10);
    field = false;
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(!led && !g_is_tag_emulating);
    lf_tag_data_clear();
    assert(stops == 4 && !m_pwm_seq && m_tag_type == TAG_TYPE_UNDEFINED);
    pwm_handler(NRFX_PWM_EVT_STOPPED);
    assert(starts == 4);
    assert(!lf_tag_raw_diagnostic_start(10));
    m_pwm_initialized = false;
    lf_tag_data_clear();
    assert(stops == 4);
    m_pwm_initialized = true;
    assert(lf_tag_data_loadcb(TAG_TYPE_RAW_MANCHESTER, NULL) == 32);
    assert(disabled == 1 && enabled == 1 && !m_cancel_playback);
    encoded_length = 0;
    assert(lf_tag_data_loadcb(TAG_TYPE_RAW_MANCHESTER, NULL) == 0);
    assert(disabled == 2 && enabled == 1 && m_cancel_playback && !m_pwm_seq);
    // Real SDK transitions: idle/playing normal and idle/running diagnostic.
    for (unsigned diagnostic = 0; diagnostic < 2; ++diagnostic) {
        for (unsigned playing = 0; playing < 2; ++playing) {
            m_state = NRFX_DRV_STATE_POWERED_ON;
            m_lpcomp_enabled = true;
            m_pwm_initialized = true;
            m_cancel_playback = false;
            m_pwm_seq = &m_raw_seq;
            m_tag_type = TAG_TYPE_RAW_MANCHESTER;
            m_is_lf_emulating = g_is_tag_emulating = led = false;
            m_diagnostic_owned = false;
            m_raw_status = LF_RAW_DIAGNOSTIC_IDLE;
            if (diagnostic) {
                assert(lf_tag_raw_diagnostic_start(1));
                if (!playing) pwm_handler(NRFX_PWM_EVT_STOPPED);
            } else if (playing) {
                lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
            }
            unsigned before_enables = lpcomp_enables, before_disables = lpcomp_disables;
            unsigned before_stops = stops, before_starts = starts;
            unsigned before_uninits = pwm_uninits, before_releases = clock_releases;
            assert(sdk_lpcomp_enabled == !playing);
            lf_sense_disable();
            assert(lpcomp_enables == before_enables + playing);
            assert(lpcomp_disables == before_disables + 1);
            assert(m_state == NRFX_DRV_STATE_UNINITIALIZED && !m_lpcomp_enabled);
            assert(pwm_uninits == before_uninits + 1 && clock_releases == before_releases + 1);
            assert(stops == before_stops + 1 && m_cancel_playback && !m_pwm_initialized);
            assert(m_pwm_seq == &m_raw_seq && !m_diagnostic_owned && !m_is_lf_emulating && !g_is_tag_emulating && !led);
            pwm_handler(NRFX_PWM_EVT_STOPPED);
            lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
            assert(starts == before_starts && lpcomp_enables == before_enables + playing);
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cfile = Path(temp) / "lifecycle.c"
            binary = Path(temp) / "lifecycle"
            cfile.write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(LF / "protocols"), str(cfile),
                            str(LF / "protocols/raw_manchester.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
