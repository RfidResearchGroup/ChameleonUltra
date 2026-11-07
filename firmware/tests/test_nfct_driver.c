// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned nrfx_err_t;
typedef unsigned nrfx_timer_t;
typedef unsigned nrf_nfct_task_t;
typedef unsigned nrfx_nfct_state_t;
typedef unsigned nrfx_drv_state_t;
typedef struct { unsigned evt_id; } nrfx_nfct_evt_t;
typedef struct {
    uint32_t rxtx_int_mask;
    void (*cb)(nrfx_nfct_evt_t const *);
} nrfx_nfct_config_t;
typedef enum {
    NRFX_NFC_FIELD_STATE_NONE, NRFX_NFC_FIELD_STATE_OFF,
    NRFX_NFC_FIELD_STATE_ON, NRFX_NFC_FIELD_STATE_UNKNOWN
} nrfx_nfct_field_state_t;

enum { NRFX_SUCCESS, NRFX_ERROR_INVALID_STATE };
enum { NRFX_DRV_STATE_UNINITIALIZED, NRFX_DRV_STATE_INITIALIZED };
enum { NRFX_NFCT_EVT_FIELD_DETECTED, NRFX_NFCT_EVT_FIELD_LOST };
enum { NRF_NFCT_TASK_ACTIVATE = 1, NRF_NFCT_TASK_DISABLE, NRF_NFCT_TASK_SENSE };
#define NRFX_NFCT_STATE_ACTIVATED NRF_NFCT_TASK_ACTIVATE
#define NRFX_NFCT_NFCID1_DEFAULT_LEN 10
#define NRF_NFCT_SENSRES_NFCID1_SIZE_DEFAULT 0
#define NRF_NFCT_SENSRES_BIT_FRAME_SDD_00100 4
#define NRF_NFCT_FRAME_DELAY_MODE_WINDOWGRID 3
#define NRFX_NFCT_CONFIG_IRQ_PRIORITY 6
#define NFCT_FRAMEDELAYMAX_DEFAULT 4096
#define NRF_NFCT_DISABLE_ALL_INT UINT32_MAX
#define NRF_NFCT_INT_FIELDDETECTED_MASK 1
#define NRF_NFCT_INT_FIELDLOST_MASK 2
#define NRF_NFCT_INT_ERROR_MASK 4
#define NRF_NFCT_INT_SELECTED_MASK 8
#define NRFX_NFCT_RX_INT_MASK 0x10
#define NRFX_NFCT_TX_INT_MASK 0x20
#define NRFX_NFCT_ERROR_STATUS_ALL_MASK 0xff
#define NFCT_IRQn 5
#define APP_TIMER_MODE_SINGLE_SHOT 0
#define APP_TIMER_TICKS(value) (value)
#define NRFX_ASSERT assert
#define APP_ERROR_CHECK(result) assert((result) == NRFX_SUCCESS)
#define NRFX_LOG_INFO(...)
#define NRFX_LOG_DEBUG(...)
#define NRFX_NFCT_CB_HANDLE(callback, event) (callback)(&(event))

static bool field_present;
static bool timer_initialized, timer_enabled, irq_enabled, close_timer_pending;
static unsigned activations, detected, lost, close_timer_stops, critical_depth;
static unsigned pending_irq;
#define NRFX_CRITICAL_SECTION_ENTER() do { critical_depth++; } while (0)
#define NRFX_CRITICAL_SECTION_EXIT() do { assert(critical_depth); critical_depth--; } while (0)
#define NRFX_IRQ_ENABLE(irq) do { (void)(irq); irq_enabled = true; } while (0)
#define NRFX_IRQ_DISABLE(irq) do { (void)(irq); irq_enabled = false; } while (0)
#define NRFX_IRQ_PENDING_CLEAR(irq) do { (void)(irq); pending_irq = 0; } while (0)
#define NRFX_IRQ_PRIORITY_SET(irq, priority) do { (void)(irq); assert((priority) == 6); } while (0)

/* The control/workaround structs and private timer initializer are production
 * declarations, copied verbatim by the runner. The fake timer instance is a
 * scalar so accidental clearing of its immutable identity is observable. */
#define NRFX_NFCT_TIMER_INSTANCE 4
#define NRFX_TIMER_INSTANCE(instance) (instance)
#include "nfct_driver_state.inc"
static unsigned m_nfc_close_timer = 17;

static void nrfx_timer_clear(nrfx_timer_t const *timer) { assert(*timer == 4); }
static void nrfx_timer_enable(nrfx_timer_t const *timer) {
    assert(*timer == 4 && timer_initialized);
    timer_enabled = true;
}
static void nrfx_timer_uninit(nrfx_timer_t const *timer) {
    assert(*timer == 4);
    timer_initialized = timer_enabled = false;
}
static nrfx_err_t nrfx_nfct_field_timer_config(void) {
    timer_initialized = true;
    return NRFX_SUCCESS;
}
static unsigned __attribute__((unused)) app_timer_create(unsigned *timer, unsigned mode, void (*handler)(void *)) {
    assert(*timer == 17 && mode == APP_TIMER_MODE_SINGLE_SHOT && handler);
    return NRFX_SUCCESS;
}
static unsigned __attribute__((unused)) app_timer_start(unsigned timer, unsigned ticks, void *context) {
    assert(timer == 17 && ticks == 3000 && context == NULL);
    close_timer_pending = true;
    return NRFX_SUCCESS;
}
static unsigned __attribute__((unused)) app_timer_stop(unsigned timer) {
    assert(timer == 17);
    close_timer_pending = false;
    close_timer_stops++;
    return NRFX_SUCCESS;
}
static bool nrfx_nfct_field_check(void) { return field_present; }
static void nrf_nfct_task_trigger(nrf_nfct_task_t task) {
    if (task == NRF_NFCT_TASK_ACTIVATE) activations++;
}
static void nrf_nfct_int_disable(unsigned mask) { (void)mask; }
static void nrf_nfct_int_enable(unsigned mask) { (void)mask; }
static void nrf_nfct_error_status_clear(unsigned mask) { (void)mask; }
static void nrf_nfct_frame_delay_max_set(unsigned ticks) { (void)ticks; }
static void nrf_nfct_frame_delay_mode_set(unsigned mode) { (void)mode; }
static void nrf_nfct_sensres_bit_frame_sdd_set(unsigned mode) { (void)mode; }
static nrfx_err_t nrfx_nfct_nfcid1_default_bytes_get(uint8_t *data, unsigned length) {
    memset(data, 0, length);
    return NRFX_SUCCESS;
}
static void nrf_nfct_nfcid1_set(uint8_t *data, unsigned size) { (void)data; (void)size; }
void nrfx_nfct_disable(void);

#include "nfct_driver_functions.inc"

static void observe(nrfx_nfct_evt_t const *event) {
    if (event->evt_id == NRFX_NFCT_EVT_FIELD_DETECTED) detected++;
    else if (event->evt_id == NRFX_NFCT_EVT_FIELD_LOST) lost++;
    else assert(false);
}
static void initialize(void) {
    const nrfx_nfct_config_t config = {.rxtx_int_mask = 0x1234, .cb = observe};
    assert(nrfx_nfct_init(&config) == NRFX_SUCCESS);
    nrfx_nfct_enable();
}
static void enter_field(bool validate) {
    field_present = true;
    nrfx_nfct_field_event_handler(NRFX_NFC_FIELD_STATE_ON);
    if (validate) {
        nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        m_timer_workaround.is_delayed = true;
        nrfx_nfct_activate_check();
        nrfx_nfct_activate_check();
#endif
    }
}
static void reinitialize(void) {
    pending_irq = 1;
    nrfx_nfct_uninit();
    assert(!irq_enabled && !timer_initialized && pending_irq == 0);
    initialize();
    assert(irq_enabled && timer_initialized);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    initialize();
    if (!strcmp(argv[1], "active_field_reenable")) {
        enter_field(true);
        assert(detected == 1 && m_nfct_cb.field_on);
        reinitialize();
        enter_field(true);
        assert(detected == 2 && activations == 2);
    } else if (!strcmp(argv[1], "filter_active_reenable")) {
        enter_field(false);
        assert(detected == 1);
        reinitialize();
        enter_field(true);
        assert(detected == 2);
    } else if (!strcmp(argv[1], "pending_validation_reenable")) {
        enter_field(false);
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        m_timer_workaround.is_delayed = true;
#endif
        nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
        assert(activations == 1);
        reinitialize();
        enter_field(false);
        assert(detected == 2);
        nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        assert(activations == 1 && m_timer_workaround.fieldevents_filter_active);
        m_timer_workaround.is_delayed = true;
        nrfx_nfct_activate_check();
#endif
        assert(activations == 2);
    } else if (!strcmp(argv[1], "future_field_lost")) {
        enter_field(true);
        reinitialize();
        enter_field(true);
        assert(detected == 2);
        field_present = false;
        nrfx_nfct_field_event_handler(NRFX_NFC_FIELD_STATE_OFF);
        assert(lost == 1 && !m_nfct_cb.field_on);
        enter_field(true);
        assert(detected == 3 && activations == 3);
    } else if (!strcmp(argv[1], "preserve_driver_resources")) {
        reinitialize();
        assert(m_timer_workaround.timer == 4 && m_nfc_close_timer == 17);
        assert(m_nfct_cb.config.cb == observe && m_nfct_cb.config.rxtx_int_mask == 0x1234);
        assert(m_nfct_cb.state == NRFX_DRV_STATE_INITIALIZED);
    } else if (!strcmp(argv[1], "duplicate_init_preserves_live_state")) {
        enter_field(false);
        const nrfx_nfct_config_t other = {.rxtx_int_mask = 0x4321, .cb = NULL};
        assert(nrfx_nfct_init(&other) == NRFX_ERROR_INVALID_STATE);
        assert(m_nfct_cb.field_on && m_nfct_cb.config.cb == observe);
        assert(m_nfct_cb.config.rxtx_int_mask == 0x1234 && timer_enabled);
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        assert(m_timer_workaround.fieldevents_filter_active);
#endif
    } else if (!strcmp(argv[1], "uninit_clears_workaround_flags")) {
        m_nfct_cb.field_on = true;
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        m_timer_workaround.fieldevents_filter_active = true;
        m_timer_workaround.is_delayed = m_timer_workaround.is_hfclk_on = true;
        nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
#ifdef TEST_RESETTABLE_VALIDATION_STATE
        assert(m_timer_workaround.is_field_validation_pending);
#endif
#else
        m_timer_workaround.field_state_cnt = 5;
#endif
        nrfx_nfct_uninit();
        assert(!m_nfct_cb.field_on);
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
        assert(!m_timer_workaround.fieldevents_filter_active &&
               !m_timer_workaround.is_delayed && !m_timer_workaround.is_hfclk_on);
#ifdef TEST_RESETTABLE_VALIDATION_STATE
        assert(!m_timer_workaround.is_field_validation_pending);
#endif
#else
        assert(m_timer_workaround.field_state_cnt == 0);
#endif
        assert(m_timer_workaround.timer == 4 && m_nfc_close_timer == 17);
    } else {
        return 2;
    }
    assert(critical_depth == 0);
    return 0;
}
