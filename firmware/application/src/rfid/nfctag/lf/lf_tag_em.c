#include "lf_tag_em.h"

#include <stdint.h>

#include "bsp_delay.h"
#include "fds_util.h"
#include "nrf_gpio.h"
#include "nrf_soc.h"
#include "nrfx_lpcomp.h"
#include "nrfx_pwm.h"
#include "protocols/em410x.h"
#include "protocols/hidprox.h"
#include "protocols/idteck.h"
#include "protocols/ioprox.h"
#include "protocols/jablotron.h"
#include "protocols/pac.h"
#include "protocols/pyramid.h"
#include "protocols/viking.h"
#include "protocols/raw_manchester.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "tag_persistence.h"
#include "rfid_main.h"
#include "app_util_platform.h"

#define NRF_LOG_MODULE_NAME tag_em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define ANT_NO_MOD() nrf_gpio_pin_clear(LF_MOD)

// Whether the USB light effect is allowed to enable
extern bool g_usb_led_marquee_enable;

// Whether it is currently in the low -frequency card number of broadcasting
static volatile bool m_is_lf_emulating = false;
// Cache tag type
static tag_specific_type_t m_tag_type = TAG_TYPE_UNDEFINED;

// The pwm to broadcast modulated card id
const nrfx_pwm_t m_broadcast = NRFX_PWM_INSTANCE(0);
const nrf_pwm_sequence_t *m_pwm_seq = NULL;
static lf_raw_manchester_entry_t m_raw_entries[LF_RAW_MANCHESTER_MAX_ENTRIES];
static nrf_pwm_values_wave_form_t m_raw_pwm[LF_RAW_MANCHESTER_MAX_ENTRIES];
static nrf_pwm_sequence_t m_raw_seq = {
    .values.p_wave_form = m_raw_pwm,
};
static volatile lf_raw_diagnostic_status_t m_raw_status = LF_RAW_DIAGNOSTIC_IDLE;
static volatile bool m_cancel_playback;
static bool m_pwm_initialized;
static volatile bool m_diagnostic_owned;
static bool m_lpcomp_enabled;
static void lf_field_lost(void);

static void lf_lpcomp_enable(void) {
    if (!m_lpcomp_enabled) {
        nrfx_lpcomp_enable();
        m_lpcomp_enabled = true;
    }
}

static void lf_lpcomp_disable(void) {
    if (m_lpcomp_enabled) {
        nrfx_lpcomp_disable();
        m_lpcomp_enabled = false;
    }
}

static void diagnostic_release(void) {
    m_diagnostic_owned = false;
    m_raw_status = LF_RAW_DIAGNOSTIC_STOPPED;
    ANT_NO_MOD();
    // Re-arm only: a new field edge, not STOPPED, starts normal emulation.
    lf_lpcomp_enable();
    lf_field_lost();
}

/* SDK nrfx_pwm.c:368-427 polls the hardware STOPPED event even with IRQ pending.
 * Publish cancellation first so a pending STOPPED IRQ cannot restart DMA. */
static void playback_cancel(void) {
    m_cancel_playback = true;
    if (m_pwm_initialized) {
        nrfx_pwm_stop(&m_broadcast, true);
    }
    if (m_raw_status == LF_RAW_DIAGNOSTIC_RUNNING) {
        m_raw_status = LF_RAW_DIAGNOSTIC_STOPPED;
    }
    m_diagnostic_owned = false;
    ANT_NO_MOD();
}

void lf_tag_raw_diagnostic_stop(void) {
    if (__get_IPSR() != 0) return;
    bool owned;
    CRITICAL_REGION_ENTER();
    owned = m_diagnostic_owned;
    if (owned) m_cancel_playback = true;
    CRITICAL_REGION_EXIT();
    if (owned) {
        playback_cancel();
        diagnostic_release();
        m_cancel_playback = false;
    }
}

lf_raw_diagnostic_status_t lf_tag_raw_diagnostic_status(void) {
    return m_raw_status;
}

bool lf_tag_raw_diagnostic_start(uint32_t ms) {
    if (__get_IPSR() != 0 || get_device_mode() == DEVICE_MODE_READER ||
        m_raw_status == LF_RAW_DIAGNOSTIC_RUNNING || !m_pwm_initialized ||
        m_tag_type != TAG_TYPE_RAW_MANCHESTER || m_pwm_seq != &m_raw_seq ||
        ms == 0 || ms > 5000) return false;
    uint32_t frame_ticks = 0;
    for (size_t i = 0; i < m_raw_seq.length / 4; i++) {
        frame_ticks += m_raw_pwm[i].counter_top;
    }
    uint16_t count = lf_raw_manchester_repetitions(frame_ticks, ms);
    if (!count) return false;
    playback_cancel();
    lf_lpcomp_disable();
    sleep_timer_stop();
    m_is_lf_emulating = true;
    g_is_tag_emulating = true;
    g_usb_led_marquee_enable = false;
    set_slot_light_color(RGB_BLUE);
    TAG_FIELD_LED_ON()
    m_diagnostic_owned = true;
    m_cancel_playback = false;
    m_raw_status = LF_RAW_DIAGNOSTIC_RUNNING;
    nrfx_pwm_simple_playback(&m_broadcast, &m_raw_seq, count, NRFX_PWM_FLAG_STOP);
    return true;
}

void lf_tag_data_clear(void) {
    playback_cancel();
    m_pwm_seq = NULL;
    m_tag_type = TAG_TYPE_UNDEFINED;
}

static void lf_field_lost(void) {
    // Open the incident interruption, so that the next event can be in and out normally
    g_is_tag_emulating = false;  // Reset the flag in the emulation
    m_is_lf_emulating = false;
    TAG_FIELD_LED_OFF()  // Make sure the indicator light of the LF field status
    // Re-arm LPCOMP so the next field appearance triggers lpcomp_event_handler.
    NRF_LPCOMP->INTENSET = LPCOMP_INTENSET_UP_Msk;
    // call sleep_timer_start *after* unsetting g_is_tag_emulating
    sleep_timer_start(SLEEP_DELAY_MS_FIELD_125KHZ_LOST);  // Start the timer to enter the sleep
    NRF_LOG_INFO("LF FIELD LOST");
}

/**
 * @brief Judge field status
 */
bool is_lf_field_exists(void) {
    lf_lpcomp_enable();
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
    // Only when the lf-frequency emulation is not launched, and the analog card is started
    if (m_cancel_playback || m_diagnostic_owned ||
        m_pwm_seq == NULL || m_is_lf_emulating || event != NRF_LPCOMP_EVENT_UP) {
        return;
    }

    sleep_timer_stop();  // turn off dormant delay
    // Disable LPCOMP during emulation — LF_RSSI fluctuates during load
    // modulation and would trigger spurious DOWN events with DETECT_CROSS.
    // Field-loss is checked periodically via EVT_END_SEQ0 in pwm_handler.
    lf_lpcomp_disable();

    // set the emulation status logo bit
    m_is_lf_emulating = true;
    g_is_tag_emulating = true;
    // turn off USB light effect when emulating cards
    g_usb_led_marquee_enable = false;

    // LED status update
    set_slot_light_color(RGB_BLUE);
    TAG_FIELD_LED_ON()

    // Play a finite burst then stop — field check happens in EVT_STOPPED after
    // PWM has fully released LF_MOD, so ANT_NO_MOD() and the settle delay are
    // effective. NRFX_PWM_FLAG_LOOP kept the pin owned by the peripheral,
    // making the field check always read "present" due to self-drive on LF_RSSI.
    nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, 10, NRFX_PWM_FLAG_STOP);

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
    if (m_cancel_playback) return;
    if (m_diagnostic_owned) {
        diagnostic_release();
        return;
    }
    if (!m_is_lf_emulating || m_pwm_seq == NULL) return;
    // PWM has fully stopped — LF_MOD is released back to GPIO.
    // Now ANT_NO_MOD() and the settle delay are effective.
    ANT_NO_MOD();
    bsp_delay_ms(2);  // let peak detector drain: ~2 ms time constant on LF_RSSI
    if (is_lf_field_exists()) {
        // Field still present — play another finite burst then check again.
        nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, 10, NRFX_PWM_FLAG_STOP);
    } else {
        // Field gone — clean up.
        lf_field_lost();
    }
}

static void pwm_init(void) {
    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG;
    cfg.output_pins[0] = LF_MOD;
    for (uint8_t i = 1; i < NRF_PWM_CHANNEL_COUNT; i++) {
        cfg.output_pins[i] = NRFX_PWM_PIN_NOT_USED;
    }
    cfg.irq_priority = APP_IRQ_PRIORITY_LOW;
    // Base clock depends on the currently-loaded tag type. Legacy ASK/FSK
    // protocols (EM410x, HID, ioProx, Viking, PAC) use 125kHz base so that
    // their hardcoded counter_top values (8-64 range) produce the correct
    // absolute timing. PSK1 protocols need finer resolution for the 16us
    // subcarrier period, so pwm_init uses 1MHz base with counter_top=16.
    // See tag_base_type.h IS_PSK1_TYPE for the list of qualifying types.
    cfg.base_clock = IS_PSK1_TYPE(m_tag_type) ? NRF_PWM_CLK_1MHz : NRF_PWM_CLK_125kHz;
    cfg.count_mode = NRF_PWM_MODE_UP;
    cfg.load_mode = NRF_PWM_LOAD_WAVE_FORM;
    cfg.step_mode = NRF_PWM_STEP_AUTO;

    nrfx_err_t err_code = nrfx_pwm_init(&m_broadcast, &cfg, pwm_handler);
    APP_ERROR_CHECK(err_code);
    m_pwm_initialized = true;
}

static void lf_sense_enable(void) {
    m_cancel_playback = false;
    // PWM bit timing divides HFCLK by a fixed ratio. On HFINT (64 MHz RC,
    // ±1.5% at 25°C after factory trim, wider over temperature) this gives a
    // chip-to-chip spread that NRZ readers — which see cumulative error across
    // runs of same-polarity bits with no intra-run resync — reject even when
    // Manchester/FSK readers don't. Holding HFXO brings the PWM clock to
    // ±40 ppm, which is also tight enough for differential PSK encodings
    // (e.g. IDTECK) where what the reader decodes are bit-to-bit phase
    // transitions, so absolute phase lock to the reader's carrier is not
    // required. The tag-mode antenna taps on this board are envelope-only,
    // which rules out coherent demodulation or phase-lock-based approaches,
    // but does not preclude the differential-phase encodings supported here.
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
    pwm_init();  // use precise hardware pwm to broadcast card id
    if (is_lf_field_exists()) {
        lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    }
}

static void lf_sense_disable(void) {
    playback_cancel();
    nrfx_pwm_uninit(&m_broadcast);
    m_pwm_initialized = false;
    // SDK uninit calls disable(), which requires POWERED_ON even after playback.
    lf_lpcomp_enable();
    nrfx_lpcomp_uninit();
    m_lpcomp_enabled = false;
    // Keep loaded slot data for reader -> emulator re-entry; data_clear invalidates it.
    m_is_lf_emulating = false;
    g_is_tag_emulating = false;
    TAG_FIELD_LED_OFF()
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
    if (enable && m_pwm_seq == NULL) {
        return;
    }
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
static int lf_tag_data_encode(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (type == TAG_TYPE_RAW_MANCHESTER) {
        if (!lf_raw_manchester_validate(buffer->buffer, buffer->length)) {
            return 0;
        }
        const uint8_t *record = buffer->buffer;
        size_t count = lf_raw_manchester_encode(record + 4, (record[3] + 7) / 8,
                record[3], record[1], record[2] & 2, record[2] & 1,
                m_raw_entries, LF_RAW_MANCHESTER_MAX_ENTRIES);
        if (!count) {
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            m_raw_pwm[i].channel_0 = m_raw_entries[i].compare[0];
            m_raw_pwm[i].channel_1 = 0;
            m_raw_pwm[i].channel_2 = 0;
            m_raw_pwm[i].counter_top = m_raw_entries[i].top;
        }
        m_raw_seq.length = count * 4;
        m_pwm_seq = &m_raw_seq;
        m_tag_type = type;
        return LF_RAW_MANCHESTER_RECORD_SIZE;
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

    if (type == TAG_TYPE_JABLOTRON && buffer->length >= LF_JABLOTRON_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = jablotron.alloc();
        m_pwm_seq = jablotron.modulator(codec, buffer->buffer);
        jablotron.free(codec);
        NRF_LOG_INFO("load lf jablotron data finish.");
        return LF_JABLOTRON_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_IDTECK && buffer->length >= LF_IDTECK_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = idteck.alloc();
        m_pwm_seq = idteck.modulator(codec, buffer->buffer);
        idteck.free(codec);
        NRF_LOG_INFO("load lf idteck data finish.");
        return LF_IDTECK_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_FARPOINTE_PYRAMID && buffer->length >= LF_PYRAMID_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = pyramid.alloc();
        m_pwm_seq = pyramid.modulator(codec, buffer->buffer);
        pyramid.free(codec);
        NRF_LOG_INFO("load lf pyramid data finish.");
        return LF_PYRAMID_TAG_ID_SIZE;
    }

    NRF_LOG_ERROR("no valid data exists in buffer for tag type: %d.", type);
    return 0;
}

int lf_tag_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (__get_IPSR() != 0) return 0;
    bool resume = m_pwm_initialized;
    if (resume) lf_tag_125khz_sense_switch(false);
    lf_tag_data_clear(); // Confirm EasyDMA released all protocol buffers before encoding.
    m_raw_status = LF_RAW_DIAGNOSTIC_IDLE;
    int length = lf_tag_data_encode(type, buffer);
    if (resume && length > 0) lf_tag_125khz_sense_switch(true);
    return length;
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
                                                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                                                         };

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
        0x01, 0xAA, 0x30, 0x39, 0x00, 0x78, 0x6A, 0xA0, 0x33, 0x09, 0xCF, 0xEF, 0x00, 0x00, 0x00, 0x00
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

int lf_tag_raw_manchester_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    if (type != TAG_TYPE_RAW_MANCHESTER || m_tag_type != type) return 0;
    return lf_raw_manchester_validate(buffer->buffer, buffer->length) ? LF_RAW_MANCHESTER_RECORD_SIZE : -1;
}

bool lf_tag_raw_manchester_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // Synthetic alternating bits, not a credential or protocol-specific frame.
    uint8_t record[LF_RAW_MANCHESTER_RECORD_SIZE] = {1, 32, 0, 8, 0xAA};
    return lf_tag_data_factory(slot, tag_type, record, sizeof(record));
}

int lf_tag_pac_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_PAC ? LF_PAC_TAG_ID_SIZE : 0;
}

bool lf_tag_pac_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id: 8 ASCII bytes
    uint8_t tag_id[8] = {'C', 'A', 'R', 'D', '0', '0', '0', '1'};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

int lf_tag_jablotron_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_JABLOTRON ? LF_JABLOTRON_TAG_ID_SIZE : 0;
}

bool lf_tag_jablotron_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id: 5 bytes (top bit must be 0)
    uint8_t tag_id[5] = {0x01, 0xB6, 0x69, 0x00, 0x00};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

/** @brief IDTECK data save callback. */
int lf_tag_idteck_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_IDTECK ? LF_IDTECK_TAG_ID_SIZE : 0;
}

/** @brief IDTECK default frame: preamble "IDTK" + 32-bit placeholder card data. */
bool lf_tag_idteck_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    uint8_t tag_id[LF_IDTECK_TAG_ID_SIZE] = {
        0x49, 0x44, 0x54, 0x4B,   // "IDTK" preamble (MSB first)
        0xDE, 0xAD, 0xBE, 0xEF,   // default card data
    };
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}

/** @brief Pyramid data save callback. */
int lf_tag_pyramid_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return m_tag_type == TAG_TYPE_FARPOINTE_PYRAMID ? LF_PYRAMID_TAG_ID_SIZE : 0;
}

/** @brief Pyramid default frame: full 128-bit on-air frame, replayed verbatim. */
bool lf_tag_pyramid_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // A valid 26-bit Pyramid frame (preamble + Wiegand + parity + CRC-8/Maxim).
    uint8_t tag_id[LF_PYRAMID_TAG_ID_SIZE] = {
        0x00, 0x01, 0x01, 0x4A, 0x3E, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x4A, 0xE3, 0x5E, 0x7F, 0x4F,
    };
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}
