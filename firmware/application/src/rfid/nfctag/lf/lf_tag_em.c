#include "lf_tag_em.h"

#include <stdint.h>
#include <string.h>

#include "bsp_delay.h"
#include "fds_util.h"
#include "nrf_gpio.h"
#include "nrfx_gpiote.h"
#include "nrf_soc.h"
#include "nrfx_lpcomp.h"
#include "nrfx_ppi.h"
#include "nrfx_pwm.h"
#include "nrfx_timer.h"
#include "protocols/em410x.h"
#include "protocols/hidprox.h"
#include "protocols/ioprox.h"
#include "protocols/pac.h"
#include "protocols/indala.h"
#include "protocols/viking.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "tag_persistence.h"

#define NRF_LOG_MODULE_NAME tag_em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define ANT_NO_MOD() nrf_gpio_pin_clear(LF_MOD)
#define ANT_MOD()    nrf_gpio_pin_set(LF_MOD)
#define LF_125KHZ_BROADCAST_MAX (10)
// PSK entries are 64x shorter (8µs vs 512µs), so 10 repeats = only ~163ms.
// PM3 captures ~314ms; a gap mid-capture corrupts the demod. 30 repeats = ~491ms, gap-free.
// Tested: broadcast=100 increases PM3 errCnt (more frames in fixed-size capture = more errors).
#define LF_PSK_BROADCAST_MAX (30)

// Whether the USB light effect is allowed to enable
extern bool g_usb_led_marquee_enable;

// Whether it is currently in the low -frequency card number of broadcasting
static volatile bool m_is_lf_emulating = false;
// Cache tag type
static tag_specific_type_t m_tag_type = TAG_TYPE_UNDEFINED;
// Buffer pointer for interactive Hitag2 crypto simulation
// The pwm to broadcast modulated card id (ASK/FSK protocols)
const nrfx_pwm_t m_broadcast = NRFX_PWM_INSTANCE(0);
const nrf_pwm_sequence_t *m_pwm_seq = NULL;

// Track current PWM base clock
static nrf_pwm_clk_t m_current_pwm_clk = NRF_PWM_CLK_125kHz;
static bool m_pwm_initialized = false;

// ---------------------------------------------------------------------------
// Timer-based PSK emulation (jitter-free fc/2 subcarrier)
// ---------------------------------------------------------------------------
// TIMER3 fires every 8us (one 125 kHz carrier cycle). ISR reads from a
// pattern buffer and sets LF_MOD HIGH or LOW. PPI+GPIOTE turns FET OFF
// at a precise sub-cycle offset for asymmetric duty (LC tank recovery).
// Phase training: alternate normal/inverted pattern each frame repeat.

// Dual-PPI architecture: BOTH ON and OFF edges are hardware-timed via PPI+GPIOTE.
// CC[0] → PPI_CH_SET → GPIOTE SET (FET ON, zero latency)
// CC[1] → PPI_CH_CLR → GPIOTE CLR (FET OFF, zero latency)
// ISR on CC[0] only manages which FUTURE cycles get a SET pulse.
// This eliminates ISR latency jitter on the ON edge that corrupted fc/2 alternation.
#define PSK_TIMER_TICKS  (128)  // 16 MHz / 128 = 125 kHz (8us carrier cycle)
#define PSK_PULSE_TICKS  (72)   // 4.5us PPI CLR. Most robust across drive modes.
                                // Resonance band 4.5-5.25us (72-84 ticks). 72 chosen for
                                // cross-unit tolerance: ±1us shift stays in band.
#define PSK_PATTERN_MAX  (3072) // 96 bits * 32 entries/bit (NexWatch)

static nrfx_timer_t m_psk_timer = NRFX_TIMER_INSTANCE(3);
static uint8_t m_psk_pattern[PSK_PATTERN_MAX];
static uint8_t m_psk_pattern_inv[PSK_PATTERN_MAX];
static uint16_t m_psk_pattern_len = 0;
static volatile uint16_t m_psk_idx = 0;
static volatile uint8_t m_psk_repeat = 0;
static volatile bool m_psk_use_inv = false;
static bool m_psk_timer_initialized = false;
static nrf_ppi_channel_t m_psk_ppi_ch_set;  // CC[0] → GPIOTE SET (ON edge)
static nrf_ppi_channel_t m_psk_ppi_ch_clr;  // CC[1] → GPIOTE CLR (OFF edge)

static void psk_broadcast_done(void);

static void psk_timer_handler(nrf_timer_event_t event, void *ctx) {
    (void)ctx;
    if (event != NRF_TIMER_EVENT_COMPARE0) return;

    // Current cycle's ON edge already happened (PPI fired before ISR entered).
    // Now decide the NEXT cycle by enabling/disabling PPI_CH_SET.
    m_psk_idx++;
    if (m_psk_idx >= m_psk_pattern_len) {
        m_psk_idx = 0;
        m_psk_repeat++;
        m_psk_use_inv = !m_psk_use_inv;

        if (m_psk_repeat >= LF_PSK_BROADCAST_MAX) {
            nrf_ppi_channel_disable(m_psk_ppi_ch_set);
            nrfx_timer_disable(&m_psk_timer);
            nrfx_gpiote_clr_task_trigger(LF_MOD);
            psk_broadcast_done();
            return;
        }
    }

    const uint8_t *pat = m_psk_use_inv ? m_psk_pattern_inv : m_psk_pattern;
    if (pat[m_psk_idx]) {
        nrf_ppi_channel_enable(m_psk_ppi_ch_set);   // next CC[0] will SET
    } else {
        nrf_ppi_channel_disable(m_psk_ppi_ch_set);  // next CC[0] won't SET
    }
}

static bool is_psk_tag_type(tag_specific_type_t type);
static uint16_t get_broadcast_count(void);
static void psk_full_teardown(void);
static void psk_timer_init(void);
static void psk_timer_start(void);
static void psk_timer_stop(void);

static void lf_field_lost(void) {
    // Open the incident interruption, so that the next event can be in and out normally
    g_is_tag_emulating = false;  // Reset the flag in the emulation
    m_is_lf_emulating = false;
    TAG_FIELD_LED_OFF()  // Make sure the indicator light of the LF field status
    NRF_LPCOMP->INTENSET = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
    // call sleep_timer_start *after* unsetting g_is_tag_emulating
    sleep_timer_start(SLEEP_DELAY_MS_FIELD_125KHZ_LOST);  // Start the timer to enter the sleep
    NRF_LOG_INFO("LF FIELD LOST");
}

/**
 * @brief Judge field status
 */
bool is_lf_field_exists(void) {
    nrfx_lpcomp_enable();
    bsp_delay_us(30);  // Display for a period of time and sampling to avoid misjudgment
    nrf_lpcomp_task_trigger(NRF_LPCOMP_TASK_SAMPLE);
    return nrf_lpcomp_result_get() == 1;  // Determine the sampling results of the LF field status
}

/**
 * @brief LPCOMP event handler is called when LPCOMP detects voltage drop.
 *
 * This function is called from interrupt context so it is very important
 * to return quickly. Don't put busy loops or any other CPU intensive actions here.
 * It is also not allowed to call soft device functions from it (if LPCOMP IRQ
 * priority is set to APP_IRQ_PRIORITY_HIGH).
 */
static void lpcomp_event_handler(nrf_lpcomp_event_t event) {
    // Only when the lf -frequency emulation is not launched, and the analog card is started
    if (m_is_lf_emulating || event != NRF_LPCOMP_EVENT_UP) {
        return;
    }

    sleep_timer_stop();  // turn off dormant delay
    nrfx_lpcomp_disable();

    // set the emulation status logo bit
    m_is_lf_emulating = true;
    g_is_tag_emulating = true;
    // turn off USB light effect when emulating cards
    g_usb_led_marquee_enable = false;

    // LED status update
    set_slot_light_color(RGB_BLUE);
    TAG_FIELD_LED_ON()

    // Start modulation: PSK uses Timer+GPIO (jitter-free), ASK/FSK use PWM
    if (is_psk_tag_type(m_tag_type)) {
        psk_timer_start();
    } else {
        nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, get_broadcast_count(), NRFX_PWM_FLAG_STOP);
    }
    NRF_LOG_INFO("LF FIELD DETECTED");
}

static void lpcomp_init(void) {
    nrfx_lpcomp_config_t cfg = NRFX_LPCOMP_DEFAULT_CONFIG;
    cfg.input = LF_RSSI;
    cfg.hal.reference = NRF_LPCOMP_REF_SUPPLY_1_16;
    cfg.hal.detection = NRF_LPCOMP_DETECT_UP;
    cfg.hal.hyst = NRF_LPCOMP_HYST_50mV;

    ret_code_t err_code = nrfx_lpcomp_init(&cfg, lpcomp_event_handler);
    APP_ERROR_CHECK(err_code);
}

static void pwm_handler(nrfx_pwm_evt_type_t event_type) {
    if (event_type != NRFX_PWM_EVT_STOPPED) {
        return;
    }

    // after last broadcast, force NO_MOD on antenna to measure field.
    ANT_NO_MOD();
    bsp_delay_ms(1);
    // We don't need any events, but only need to detect the state of the field
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
    if (is_lf_field_exists()) {
        nrfx_lpcomp_disable();
        nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, get_broadcast_count(), NRFX_PWM_FLAG_STOP);
    } else {
        lf_field_lost();
    }
}

static void pwm_init_with_clock(nrf_pwm_clk_t clk) {
    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG;
    cfg.output_pins[0] = LF_MOD;
    for (uint8_t i = 1; i < NRF_PWM_CHANNEL_COUNT; i++) {
        cfg.output_pins[i] = NRFX_PWM_PIN_NOT_USED;
    }
    cfg.irq_priority = APP_IRQ_PRIORITY_LOW;
    cfg.base_clock = clk;
    cfg.count_mode = NRF_PWM_MODE_UP;
    cfg.load_mode = NRF_PWM_LOAD_WAVE_FORM;
    cfg.step_mode = NRF_PWM_STEP_AUTO;

    nrfx_err_t err_code = nrfx_pwm_init(&m_broadcast, &cfg, pwm_handler);
    APP_ERROR_CHECK(err_code);
    m_current_pwm_clk = clk;
    m_pwm_initialized = true;
}

static bool is_psk_tag_type(tag_specific_type_t type) {
    return type == TAG_TYPE_INDALA;
}

static uint16_t get_broadcast_count(void) {
    return is_psk_tag_type(m_tag_type) ? LF_PSK_BROADCAST_MAX : LF_125KHZ_BROADCAST_MAX;
}

static void pwm_init(void) {
    // PWM is only used for ASK/FSK protocols (125 kHz clock).
    // PSK protocols use Timer3+GPIO instead (see psk_timer_init).
    nrf_pwm_clk_t clk = NRF_PWM_CLK_125kHz;
    pwm_init_with_clock(clk);
}

// Reinitialize PWM with a different base clock if needed.
static void pwm_reinit_clock(nrf_pwm_clk_t clk) {
    if (!m_pwm_initialized) return;
    if (m_current_pwm_clk == clk) return;
    nrfx_pwm_uninit(&m_broadcast);
    m_pwm_initialized = false;
    pwm_init_with_clock(clk);
}

// --- PSK Timer management ---

static void psk_full_teardown(void) {
    if (!m_psk_timer_initialized) return;
    psk_timer_stop();
    nrfx_ppi_channel_disable(m_psk_ppi_ch_set);
    nrfx_ppi_channel_free(m_psk_ppi_ch_set);
    nrfx_ppi_channel_disable(m_psk_ppi_ch_clr);
    nrfx_ppi_channel_free(m_psk_ppi_ch_clr);
    nrfx_gpiote_out_task_disable(LF_MOD);
    nrfx_gpiote_out_uninit(LF_MOD);
    nrfx_timer_uninit(&m_psk_timer);
    m_psk_timer_initialized = false;
    nrf_gpio_cfg_output(LF_MOD);
    ANT_NO_MOD();
}

static void psk_timer_init(void) {
    psk_full_teardown();

    // --- Timer3: 16 MHz, fires every 8us (CC[0]) ---
    nrfx_timer_config_t cfg = NRFX_TIMER_DEFAULT_CONFIG;
    cfg.frequency = NRF_TIMER_FREQ_16MHz;
    cfg.mode = NRF_TIMER_MODE_TIMER;
    cfg.bit_width = NRF_TIMER_BIT_WIDTH_16;
    cfg.interrupt_priority = APP_IRQ_PRIORITY_HIGH;

    nrfx_err_t err = nrfx_timer_init(&m_psk_timer, &cfg, psk_timer_handler);
    APP_ERROR_CHECK(err);

    // CC[0]: carrier cycle boundary. SHORT: CC[0] → CLEAR (auto-restart).
    // Also generates ISR for pattern advancement.
    nrfx_timer_extended_compare(&m_psk_timer, NRF_TIMER_CC_CHANNEL0,
        PSK_TIMER_TICKS, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

    // CC[1]: FET turn-off point within carrier cycle. No ISR, PPI only.
    nrfx_timer_compare(&m_psk_timer, NRF_TIMER_CC_CHANNEL1, PSK_PULSE_TICKS, false);

    // --- GPIOTE: configure LF_MOD as task pin (supports SET and CLR tasks) ---
    nrfx_gpiote_out_config_t gpiote_cfg = NRFX_GPIOTE_CONFIG_OUT_TASK_LOW;
    err = nrfx_gpiote_out_init(LF_MOD, &gpiote_cfg);
    APP_ERROR_CHECK(err);
    nrfx_gpiote_out_task_enable(LF_MOD);

    // --- PPI_CH_SET: CC[0] event → GPIOTE SET (FET ON at carrier cycle start) ---
    err = nrfx_ppi_channel_alloc(&m_psk_ppi_ch_set);
    APP_ERROR_CHECK(err);
    err = nrfx_ppi_channel_assign(m_psk_ppi_ch_set,
        nrfx_timer_event_address_get(&m_psk_timer, NRF_TIMER_EVENT_COMPARE0),
        nrfx_gpiote_set_task_addr_get(LF_MOD));
    APP_ERROR_CHECK(err);
    // Don't enable yet — psk_timer_start() pre-arms based on pattern[0],
    // ISR manages enable/disable for subsequent cycles.

    // --- PPI_CH_CLR: CC[1] event → GPIOTE CLR (FET OFF after pulse width) ---
    err = nrfx_ppi_channel_alloc(&m_psk_ppi_ch_clr);
    APP_ERROR_CHECK(err);
    err = nrfx_ppi_channel_assign(m_psk_ppi_ch_clr,
        nrfx_timer_event_address_get(&m_psk_timer, NRF_TIMER_EVENT_COMPARE1),
        nrfx_gpiote_clr_task_addr_get(LF_MOD));
    APP_ERROR_CHECK(err);
    err = nrfx_ppi_channel_enable(m_psk_ppi_ch_clr);
    APP_ERROR_CHECK(err);
    // CLR channel is always enabled — fires every cycle. On OFF cycles (pin already LOW)
    // it's a no-op. On ON cycles it creates the precise OFF edge.

    m_psk_timer_initialized = true;
}

static void psk_timer_start(void) {
    m_psk_idx = 0;
    m_psk_repeat = 0;
    m_psk_use_inv = false;
    // Pre-arm PPI_CH_SET for the first carrier cycle based on pattern[0]
    if (m_psk_pattern[0]) {
        nrf_ppi_channel_enable(m_psk_ppi_ch_set);
    } else {
        nrf_ppi_channel_disable(m_psk_ppi_ch_set);
    }
    nrfx_timer_enable(&m_psk_timer);
}

static void psk_timer_stop(void) {
    nrfx_timer_disable(&m_psk_timer);
    nrf_gpio_cfg_output(LF_MOD);
    ANT_NO_MOD();
}

static void psk_broadcast_done(void) {
    nrfx_gpiote_clr_task_trigger(LF_MOD);
    bsp_delay_ms(1);
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk |
                            LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
    if (is_lf_field_exists()) {
        nrfx_lpcomp_disable();
        psk_timer_start();
    } else {
        lf_field_lost();
    }
}

static void psk_build_pattern(const nrf_pwm_sequence_t *seq) {
    const nrf_pwm_values_wave_form_t *vals = seq->values.p_wave_form;
    uint16_t n_entries = seq->length / 4;
    if (n_entries > PSK_PATTERN_MAX) n_entries = PSK_PATTERN_MAX;

    for (uint16_t i = 0; i < n_entries; i++) {
        uint8_t on = (vals[i].channel_0 > 0) ? 1 : 0;
        m_psk_pattern[i] = on;
        m_psk_pattern_inv[i] = on ^ 1;
    }
    m_psk_pattern_len = n_entries;
}

static void lf_sense_enable(void) {
    // PWM bit timing divides HFCLK by a fixed ratio. On HFINT (64 MHz RC,
    // ±1.5% at 25°C after factory trim, wider over temperature) this gives a
    // chip-to-chip spread that NRZ readers — which see cumulative error across
    // runs of same-polarity bits with no intra-run resync — reject even when
    // Manchester/FSK readers don't. Holding HFXO brings the PWM clock to
    // ±40 ppm. We can't lock to the reader's carrier (tag-mode antenna taps
    // on this board are envelope-only), so this is as good as it gets.
    //
    // Paired release in lf_sense_disable(). SD reference-counts HFXO requests,
    // so this coexists with BLE. Both functions run from thread context
    // (tag_mode_enter/tag_emulation_sense_end) where SVCs are safe.
    sd_clock_hfclk_request();
    uint32_t hfclk_running = 0;
    while (!hfclk_running) {
        sd_clock_hfclk_is_running(&hfclk_running);
    }

    lpcomp_init();
    if (is_psk_tag_type(m_tag_type)) {
        psk_timer_init();
    } else {
        pwm_init();
    }
    if (is_lf_field_exists()) {
        lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    }
}

static void lf_sense_disable(void) {
    psk_full_teardown();
    if (m_pwm_initialized) {
        nrfx_pwm_uninit(&m_broadcast);
        m_pwm_initialized = false;
    }
    nrfx_lpcomp_uninit();
    m_pwm_seq = NULL;
    m_is_lf_emulating = false;
    sd_clock_hfclk_release();
}

static enum {
    LF_SENSE_STATE_NONE,
    LF_SENSE_STATE_DISABLE,
    LF_SENSE_STATE_ENABLE,
} m_lf_sense_state = LF_SENSE_STATE_NONE;

static uint16_t lf_em410x_id_size(tag_specific_type_t type) {
    return type == TAG_TYPE_EM410X_ELECTRA ? LF_EM410X_ELECTRA_TAG_ID_SIZE : LF_EM410X_TAG_ID_SIZE;
}

/**
 * @brief switchLfFieldInductionToEnableTheState
 */
void lf_tag_125khz_sense_switch(bool enable) {
    // init modulation PIN as output PIN
    nrf_gpio_cfg_output(LF_MOD);
    // turn off mod, otherwise its hard to judge RSSI
    ANT_NO_MOD();

    if ((m_lf_sense_state == LF_SENSE_STATE_NONE || m_lf_sense_state == LF_SENSE_STATE_DISABLE) && enable) {
        // switch from disable -> enable
        m_lf_sense_state = LF_SENSE_STATE_ENABLE;
        lf_sense_enable();
    } else if (m_lf_sense_state == LF_SENSE_STATE_ENABLE && !enable) {
        // switch from enable -> disable
        m_lf_sense_state = LF_SENSE_STATE_DISABLE;
        lf_sense_disable();
    }
}

/** @brief lf card data loader
 * @param type     Refined tag type
 * @param buffer   Data buffer
 */
int lf_tag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // ASK/FSK protocols use PWM at 125 kHz clock.
    // PSK protocols use Timer3+GPIO (no PWM needed), but still build the
    // modulator's PWM sequence to extract the bit pattern.
    if (!is_psk_tag_type(type)) {
        pwm_reinit_clock(NRF_PWM_CLK_125kHz);
    }

    // ensure buffer size is large enough for specific tag type,
    // so that tag data (e.g., card numbers) can be converted to corresponding pwm sequence here.
    if ((type == TAG_TYPE_EM410X || type == TAG_TYPE_EM410X_ELECTRA) && buffer->length >= lf_em410x_id_size(type)) {
        const protocol *p = type == TAG_TYPE_EM410X_ELECTRA ? &em410x_electra : &em410x_64;
        m_tag_type = type;
        void *codec = p->alloc();
        m_pwm_seq = p->modulator(codec, buffer->buffer);
        p->free(codec);
        NRF_LOG_INFO("load lf em410x%s data finish.", type == TAG_TYPE_EM410X_ELECTRA ? " electra" : "");
        return lf_em410x_id_size(type);
    }

    if (type == TAG_TYPE_HID_PROX && buffer->length >= LF_HIDPROX_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = hidprox.alloc();
        m_pwm_seq = hidprox.modulator(codec, buffer->buffer);
        hidprox.free(codec);
        NRF_LOG_INFO("load lf hidprox data finish.");
        return LF_HIDPROX_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_IOPROX && buffer->length >= LF_IOPROX_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = ioprox.alloc();
        m_pwm_seq = ioprox.modulator(codec, buffer->buffer);
        ioprox.free(codec);
        NRF_LOG_INFO("load lf ioprox data finish.");
        return LF_IOPROX_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_VIKING && buffer->length >= LF_VIKING_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = viking.alloc();
        m_pwm_seq = viking.modulator(codec, buffer->buffer);
        viking.free(codec);
        NRF_LOG_INFO("load lf viking data finish.");
        return LF_VIKING_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_PAC && buffer->length >= LF_PAC_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = pac.alloc();
        m_pwm_seq = pac.modulator(codec, buffer->buffer);
        pac.free(codec);
        NRF_LOG_INFO("load lf pac data finish.");
        return LF_PAC_TAG_ID_SIZE;
    }

    // PSK protocols: build PWM sequence then convert to timer pattern
    if (type == TAG_TYPE_INDALA && buffer->length >= LF_INDALA_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = indala.alloc();
        const nrf_pwm_sequence_t *seq = indala.modulator(codec, buffer->buffer);
        psk_build_pattern(seq);
        indala.free(codec);
        NRF_LOG_INFO("load lf indala data finish.");
        return LF_INDALA_TAG_ID_SIZE;
    }

    NRF_LOG_ERROR("no valid data exists in buffer for tag type: %d.", type);
    return 0;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined tag type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this tag before allowing saving
    // Just save the original card package directly
    if (m_tag_type == TAG_TYPE_EM410X) {
        return LF_EM410X_TAG_ID_SIZE;
    }
    if (m_tag_type == TAG_TYPE_EM410X_ELECTRA) {
        return LF_EM410X_ELECTRA_TAG_ID_SIZE;
    }
    return 0;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined tag type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_hidprox_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this tag before allowing saving
    // Just save the original card package directly
    return m_tag_type == TAG_TYPE_HID_PROX ? LF_HIDPROX_TAG_ID_SIZE : 0;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined tag type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_ioprox_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this tag before allowing saving
    // Just save the original card package directly
    return m_tag_type == TAG_TYPE_IOPROX ? LF_IOPROX_TAG_ID_SIZE : 0;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined tag type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_viking_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this tag before allowing saving
    // Just save the original card package directly
    return m_tag_type == TAG_TYPE_VIKING ? LF_VIKING_TAG_ID_SIZE : 0;
}

bool lf_tag_data_factory(uint8_t slot, tag_specific_type_t tag_type, uint8_t *tag_id, uint16_t length) {
    // write data to flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info;  // Get the special card slot FDS record information
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // Call the blocked FDS to write the function, and write the data of the specified field type of the card slot into the Flash
    bool ret = fds_write_sync(map_info.id, map_info.key, length, (uint8_t *)tag_id);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}

/** @brief Id card deposit card number before callback
 * @param slot      Card slot number
 * @param tag_type  Refined tag type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    static const uint8_t tag_id_base[LF_EM410X_TAG_ID_SIZE] = {0xDE, 0xAD, 0xBE, 0xEF, 0x88};
    static const uint8_t tag_id_electra[LF_EM410X_ELECTRA_TAG_ID_SIZE] = {0xDE, 0xAD, 0xBE, 0xEF, 0x88,
                                                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    switch (tag_type) {
        case TAG_TYPE_EM410X_ELECTRA:
            return lf_tag_data_factory(slot, tag_type, (uint8_t *)tag_id_electra, sizeof(tag_id_electra));
        case TAG_TYPE_EM410X:
            return lf_tag_data_factory(slot, tag_type, (uint8_t *)tag_id_base, sizeof(tag_id_base));
        default:
            return false;
    }
}

/** @brief Id card deposit card number before callback
 * @param slot      Card slot number
 * @param tag_type  Refined tag type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_hidprox_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id, must to align(4), more word...
    uint8_t tag_id[13] = {0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x51, 0x45, 0x00, 0x00, 0x00};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

/** @brief Id card deposit card number before callback
 * @param slot      Card slot number
 * @param tag_type  Refined tag type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_ioprox_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    uint8_t tag_id[16] = {
        0x01,0xAA,0x30,0x39,0x00,0x78,0x6A,0xA0,0x33,0x09,0xCF,0xEF,0x00,0x00,0x00,0x00
    };
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

/** @brief Id card deposit card number before callback
 * @param slot      Card slot number
 * @param tag_type  Refined tag type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id
    uint8_t tag_id[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

int lf_tag_pac_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_PAC ? LF_PAC_TAG_ID_SIZE : 0;
}

bool lf_tag_pac_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id: 8 ASCII bytes
    uint8_t tag_id[8] = {'C', 'A', 'R', 'D', '0', '0', '0', '1'};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

int lf_tag_indala_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_INDALA ? LF_INDALA_TAG_ID_SIZE : 0;
}

/** @brief Id card deposit card number before callback
 * @param slot      Card slot number
 * @param tag_type  Refined tag type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_indala_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    uint8_t tag_id[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x88, 0x77, 0x66, 0x55};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}
