// SPDX-License-Identifier: GPL-2.0-or-later
/* Generated includes contain the unchanged production lifecycle and SDK clock
 * driver functions. Only hardware and unrelated parser/TX paths are mocked, so
 * these two test files also work independently of the protocol regression suite. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nrf52840_bitfields.h"

#define ASSERT assert
#define UNUSED_VARIABLE(value) (void)(value)
#define NRF_LOG_INFO(...)
#define NRF_NFCT_FRAME_DELAY_MODE_WINDOWGRID NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_WindowGrid
static struct {
    struct { uint32_t AMOUNT; } TXD;
    uint32_t TASKS_STARTTX, FRAMEDELAYMAX;
} test_nfct;
#define NRF_NFCT (&test_nfct)
#define MAX_NFC_TX_BUFFER_SIZE 512
static uint8_t m_nfc_rx_buffer[257], m_nfc_tx_buffer[MAX_NFC_TX_BUFFER_SIZE];
static void (*m_tx_sniff_cb)(const uint8_t *, uint16_t);
static bool m_is_responded, m_selection_timing_active, reset_if_field_lost;
enum { NFC_TAG_STATE_14A_IDLE, NFC_TAG_STATE_14A_READY, NFC_TAG_STATE_14A_ACTIVE };
static unsigned m_tag_state_14a;
static void __attribute__((unused)) nfc_tag_14a_restore_frame_delay(void) {
    m_selection_timing_active = false;
}
static void nfc_tag_14a_data_process(uint8_t *data) { (void)data; }

typedef enum {
    NRF_DRV_CLOCK_EVT_HFCLK_STARTED, NRF_DRV_CLOCK_EVT_LFCLK_STARTED,
    NRF_DRV_CLOCK_EVT_CAL_DONE, NRF_DRV_CLOCK_EVT_CAL_ABORTED
} nrf_drv_clock_evt_type_t;
typedef void (*nrf_drv_clock_event_handler_t)(nrf_drv_clock_evt_type_t);
typedef struct nrf_drv_clock_handler_item_s {
    struct nrf_drv_clock_handler_item_s *p_next;
    nrf_drv_clock_event_handler_t event_handler;
} nrf_drv_clock_handler_item_t;
static struct {
    bool module_initialized;
    volatile bool hfclk_on;
    volatile uint32_t hfclk_requests;
    volatile nrf_drv_clock_handler_item_t *p_hf_head, *p_lf_head;
} m_clock_cb;

static unsigned critical_depth;
static bool physical_hfxo_running, softdevice_app_requested, ble_needs_hfxo;
static bool auto_start_on_poll;
static unsigned clock_starts, clock_stops, clock_initializations;
#define CRITICAL_REGION_ENTER() do { critical_depth++; } while (0)
#define CRITICAL_REGION_EXIT() do { assert(critical_depth > 0); critical_depth--; } while (0)
#define __STATIC_INLINE static inline
#define NRF_EVT_HFCLKSTARTED 1
#define APP_ERROR_CHECK(error) assert((error) == 0)

static bool nrf_sdh_is_enabled(void) { return true; }
static uint32_t sd_clock_hfclk_request(void) {
    clock_starts++;
    softdevice_app_requested = true;
    return 0;
}
static uint32_t sd_clock_hfclk_release(void) {
    clock_stops++;
    softdevice_app_requested = false;
    if (!ble_needs_hfxo) physical_hfxo_running = false;
    return 0;
}
static uint32_t sd_clock_hfclk_is_running(uint32_t *running) {
    if (auto_start_on_poll && softdevice_app_requested) physical_hfxo_running = true;
    *running = physical_hfxo_running;
    return 0;
}
static void nrfx_clock_hfclk_start(void) { (void)sd_clock_hfclk_request(); }
static void nrfx_clock_hfclk_stop(void) { (void)sd_clock_hfclk_release(); }
static bool nrfx_clock_hfclk_is_running(void) { return physical_hfxo_running; }
static int nrf_drv_clock_init(void) {
    assert(!m_clock_cb.module_initialized);
    clock_initializations++;
    m_clock_cb.module_initialized = true;
    return 0;
}

/* Real reference count, callback linked list and SoC event validation. */
#include "clock_driver.inc"

enum {
    NRFX_NFCT_EVT_FIELD_DETECTED, NRFX_NFCT_EVT_FIELD_LOST,
    NRFX_NFCT_EVT_TX_FRAMESTART, NRFX_NFCT_EVT_TX_FRAMEEND,
    NRFX_NFCT_EVT_RX_FRAMEEND, NRFX_NFCT_EVT_ERROR
};
enum { NRFX_NFCT_ERROR_FRAMEDELAYTIMEOUT, NRFX_NFCT_ERROR_NUM };
typedef struct {
    int evt_id;
    union { struct { int reason; } error; } params;
} nrfx_nfct_evt_t;
typedef struct {
    uint32_t rxtx_int_mask;
    void (*cb)(nrfx_nfct_evt_t const *);
} nrfx_nfct_config_t;

static bool nfct_initialized, nfct_enabled;
static unsigned nfct_init_count, nfct_uninit_count, activation_count, rearm_count;
static unsigned reset_count, sleep_start_count, sleep_stop_count;
static unsigned usb_notifications;
bool g_usb_led_marquee_enable, g_is_tag_emulating;

#define NRFX_SUCCESS 0
#define NRF_LOG_ERROR(...)
#define RGB_GREEN 1
#define TAG_FIELD_LED_ON() (void)0;
#define TAG_FIELD_LED_OFF() (void)0;
#define SLEEP_DELAY_MS_FIELD_NFC_LOST 3000
#define NRFX_NFCT_STATE_ACTIVATED 1
#undef NRFX_NFCT_RX_BYTES
#define NRFX_NFCT_RX_BYTES do { assert(nfct_initialized && nfct_enabled); assert(physical_hfxo_running); rearm_count++; } while (0);

static void nfc_fdt_reset(void) {
    test_nfct.TASKS_STARTTX = 0;
    test_nfct.FRAMEDELAYMAX = 4096;
}
static void sleep_timer_stop(void) { sleep_stop_count++; }
static void sleep_timer_start(unsigned delay) { assert(delay == 3000); sleep_start_count++; }
static void set_slot_light_color(unsigned color) { assert(color == RGB_GREEN); }
static void nrfx_nfct_autocolres_disable(void) {
    assert(nfct_initialized && nfct_enabled && physical_hfxo_running);
}
static void nrfx_nfct_state_force(unsigned state) {
    assert(state == NRFX_NFCT_STATE_ACTIVATED);
    assert(nfct_initialized && nfct_enabled && physical_hfxo_running);
    activation_count++;
}
static void nrf_nfct_reset(void) { reset_count++; }
static int nrfx_nfct_init(nrfx_nfct_config_t const *config) {
    assert(config && config->cb);
    assert(!nfct_initialized);
    nfct_initialized = true;
    nfct_init_count++;
    return NRFX_SUCCESS;
}
static void nrfx_nfct_enable(void) {
    assert(nfct_initialized);
    nfct_enabled = true;
}
static void nrfx_nfct_uninit(void);

#include "nfct_clock_lifecycle.inc"
#include "nfct_event_callback.inc"
#include "nfct_sense_switch.inc"

static void nrfx_nfct_uninit(void) {
    assert(nfct_initialized);
    /* Invalidate the callback guard before this hardware ownership boundary. */
    assert(m_nfc_sense_state != NFC_SENSE_STATE_ENABLE);
    nfct_initialized = nfct_enabled = false;
    nfct_uninit_count++;
}

/* Hardware around the source-extracted LF client is outside this test. */
static int m_broadcast;
static void *m_pwm_seq;
static bool m_is_lf_emulating;
#define NRF_LPCOMP_EVENT_UP 1
static void lpcomp_init(void) {}
static void pwm_init(void) {}
static bool is_lf_field_exists(void) { return false; }
static void lpcomp_event_handler(unsigned event) { (void)event; }
static void nrfx_pwm_uninit(void *pwm) { (void)pwm; }
static void nrfx_lpcomp_uninit(void) {}
#include "lf_clock_client.inc"

static void field_event(int event_id) {
    nrfx_nfct_evt_t event = {.evt_id = event_id};
    nfc_tag_14a_event_callback(&event);
    assert(critical_depth == 0);
}

static void started_event(bool clock_physically_ready) {
    physical_hfxo_running = clock_physically_ready;
    soc_evt_handler(NRF_EVT_HFCLKSTARTED, NULL);
    assert(critical_depth == 0);
}

static void usb_ready(nrf_drv_clock_evt_type_t event) {
    assert(event == NRF_DRV_CLOCK_EVT_HFCLK_STARTED);
    assert(physical_hfxo_running);
    usb_notifications++;
}
static nrf_drv_clock_handler_item_t usb_clock_item = {.event_handler = usb_ready};

static void fixture(void) {
    memset(&test_nfct, 0, sizeof(test_nfct));
    memset(&m_clock_cb, 0, sizeof(m_clock_cb));
    critical_depth = 0;
    physical_hfxo_running = softdevice_app_requested = ble_needs_hfxo = false;
    auto_start_on_poll = false;
    clock_starts = clock_stops = clock_initializations = 0;
    nfct_initialized = nfct_enabled = false;
    nfct_init_count = nfct_uninit_count = activation_count = rearm_count = 0;
    reset_count = sleep_start_count = sleep_stop_count = usb_notifications = 0;
    m_nfc_sense_state = NFC_SENSE_STATE_NONE;
    m_nfc_field_present = m_nfc_clock_requested = false;
    m_nfc_clock_handler_item.p_next = NULL;
    usb_clock_item.p_next = NULL;
    reset_if_field_lost = false;
    m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
    nfc_tag_14a_sense_switch(true);
    assert(critical_depth == 0 && nfct_initialized && nfct_enabled);
}

static void assert_active_once(void) {
    assert(activation_count == 1 && rearm_count == 1);
    assert(physical_hfxo_running && m_nfc_clock_requested && m_nfc_field_present);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    fixture();
    const char *name = argv[1];
    if (!strcmp(name, "clock_init_on_enable")) {
        assert(clock_initializations == 1 && clock_starts == 0);
        assert(m_clock_cb.hfclk_requests == 0 && !m_nfc_clock_requested);
        nfc_tag_14a_sense_switch(true);
        assert(clock_initializations == 1 && nfct_init_count == 1);
    } else if (!strcmp(name, "delayed_hfxo_start")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert(m_clock_cb.hfclk_requests == 1 && clock_starts == 1);
        assert(activation_count == 0 && rearm_count == 0);
        started_event(true);
        assert_active_once();
    } else if (!strcmp(name, "synchronous_hfxo_start")) {
        nrf_drv_clock_hfclk_request(NULL); /* USB owns an already-ready clock. */
        started_event(true);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert_active_once();
        assert(m_clock_cb.hfclk_requests == 2 && clock_starts == 1);
    } else if (!strcmp(name, "duplicate_field_events")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert(m_clock_cb.hfclk_requests == 1 && clock_starts == 1);
        started_event(true);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert_active_once();
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        assert(m_clock_cb.hfclk_requests == 0 && clock_stops == 1);
    } else if (!strcmp(name, "duplicate_clock_notifications")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        started_event(true);
        nfc_tag_14a_clock_handler(NRF_DRV_CLOCK_EVT_HFCLK_STARTED);
        started_event(true);
        assert_active_once();
    } else if (!strcmp(name, "field_loss_before_start")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        assert(m_clock_cb.hfclk_requests == 0 && !m_nfc_clock_requested);
        started_event(false);
        assert(activation_count == 0 && rearm_count == 0 && !m_clock_cb.hfclk_on);
    } else if (!strcmp(name, "field_loss_then_redetect")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert(m_clock_cb.hfclk_requests == 1 && clock_starts == 2);
        started_event(true);
        assert_active_once();
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        assert(m_clock_cb.hfclk_requests == 0 && clock_stops == 2);
    } else if (!strcmp(name, "disable_before_start")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        nfc_tag_14a_sense_switch(false);
        started_event(false);
        assert(m_clock_cb.hfclk_requests == 0 && !m_nfc_clock_requested);
        assert(activation_count == 0 && rearm_count == 0 && nfct_uninit_count == 1);
    } else if (!strcmp(name, "disable_then_reenable")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        nfc_tag_14a_sense_switch(false);
        nfc_tag_14a_sense_switch(true);
        started_event(false);
        assert(activation_count == 0 && !m_clock_cb.hfclk_on);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        started_event(true);
        assert_active_once();
    } else if (!strcmp(name, "stale_start_without_owner")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        ble_needs_hfxo = true;
        started_event(true); /* BLE running is not an application request. */
        assert(m_clock_cb.hfclk_requests == 0 && !m_clock_cb.hfclk_on);
        assert(activation_count == 0 && rearm_count == 0);
    } else if (!strcmp(name, "stale_start_during_restart") ||
               !strcmp(name, "stale_start_after_slot_switch")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        physical_hfxo_running = true; /* Old event was queued before cancellation. */
        if (!strcmp(name, "stale_start_after_slot_switch")) {
            nfc_tag_14a_sense_switch(false);
            nfc_tag_14a_sense_switch(true);
        } else field_event(NRFX_NFCT_EVT_FIELD_LOST);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        started_event(false); /* Old notification arrives during new startup. */
        assert(activation_count == 0 && rearm_count == 0 && !m_clock_cb.hfclk_on);
        assert(m_clock_cb.p_hf_head != NULL); /* Keep the callback for real readiness. */
        started_event(true);
        assert_active_once();
    } else if (!strcmp(name, "coexist_usb_lf_nfct")) {
        nrf_drv_clock_hfclk_request(&usb_clock_item);
        started_event(true);
        auto_start_on_poll = true;
        lf_sense_enable();
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        assert_active_once();
        assert(m_clock_cb.hfclk_requests == 3 && usb_notifications == 1);
        lf_sense_disable();
        assert(m_clock_cb.hfclk_requests == 2 && physical_hfxo_running);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        assert(m_clock_cb.hfclk_requests == 1 && physical_hfxo_running);
        nrf_drv_clock_hfclk_release();
        assert(m_clock_cb.hfclk_requests == 0 && !physical_hfxo_running);
    } else if (!strcmp(name, "late_nfct_callback_with_usb")) {
        nrf_drv_clock_hfclk_request(&usb_clock_item);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        nfc_tag_14a_sense_switch(false);
        started_event(true);
        assert(activation_count == 0 && rearm_count == 0 && usb_notifications == 1);
        assert(m_clock_cb.hfclk_requests == 1 && physical_hfxo_running);
    } else if (!strcmp(name, "queued_usb_handler_preserved")) {
        nrf_drv_clock_hfclk_request(&usb_clock_item);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        started_event(true);
        assert_active_once();
        assert(usb_notifications == 1 && m_clock_cb.hfclk_requests == 2);
        assert(m_clock_cb.p_hf_head == NULL);
    } else if (!strcmp(name, "clock_handler_ignores_other_events")) {
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        nfc_tag_14a_clock_handler(NRF_DRV_CLOCK_EVT_LFCLK_STARTED);
        assert(activation_count == 0 && rearm_count == 0);
    } else if (!strcmp(name, "reset_on_field_loss")) {
        reset_if_field_lost = true;
        field_event(NRFX_NFCT_EVT_FIELD_DETECTED);
        started_event(true);
        field_event(NRFX_NFCT_EVT_FIELD_LOST);
        assert(reset_count == 1 && m_clock_cb.hfclk_requests == 0);
        nfc_tag_14a_clock_handler(NRF_DRV_CLOCK_EVT_HFCLK_STARTED);
        assert(activation_count == 1 && rearm_count == 1);
    } else if (!strcmp(name, "repeated_enable_disable")) {
        nfc_tag_14a_sense_switch(false);
        nfc_tag_14a_sense_switch(false);
        nfc_tag_14a_sense_switch(true);
        nfc_tag_14a_sense_switch(true);
        assert(nfct_init_count == 2 && nfct_uninit_count == 1);
        assert(m_clock_cb.hfclk_requests == 0 && clock_stops == 0);
    } else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    assert(critical_depth == 0);
    return 0;
}
