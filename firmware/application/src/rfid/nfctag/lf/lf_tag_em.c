#include "lf_tag_em.h"

#include <stdint.h>

#include "bsp_delay.h"
#include "fds_util.h"
#include "nrf_gpio.h"
#include "nrfx_lpcomp.h"
#include "nrfx_pwm.h"
#include "protocols/em410x.h"
#include "protocols/hidprox.h"
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
#define LF_125KHZ_BROADCAST_MAX (10)

// Whether the USB light effect is allowed to enable
extern bool g_usb_led_marquee_enable;

// Whether it is currently in the low -frequency card number of broadcasting
static volatile bool m_is_lf_emulating = false;
// Cache tag type
static tag_specific_type_t m_tag_type = TAG_TYPE_UNDEFINED;

// The pwm to broadcast modulated card id
const nrfx_pwm_t m_broadcast = NRFX_PWM_INSTANCE(0);
const nrf_pwm_sequence_t *m_pwm_seq = NULL;

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

    // use precise hardware timer to broadcast card id
    nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, LF_125KHZ_BROADCAST_MAX, NRFX_PWM_FLAG_STOP);

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
        nrfx_pwm_simple_playback(&m_broadcast, m_pwm_seq, LF_125KHZ_BROADCAST_MAX, NRFX_PWM_FLAG_STOP);
    } else {
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
    cfg.base_clock = NRF_PWM_CLK_125kHz;
    cfg.count_mode = NRF_PWM_MODE_UP;
    cfg.load_mode = NRF_PWM_LOAD_WAVE_FORM;
    cfg.step_mode = NRF_PWM_STEP_AUTO;

    nrfx_err_t err_code = nrfx_pwm_init(&m_broadcast, &cfg, pwm_handler);
    APP_ERROR_CHECK(err_code);
}

static void lf_sense_enable(void) {
    lpcomp_init();
    pwm_init();  // use precise hardware pwm to broadcast card id
    if (is_lf_field_exists()) {
        lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    }
}

static void lf_sense_disable(void) {
    nrfx_pwm_uninit(&m_broadcast);
    nrfx_lpcomp_uninit();
    m_pwm_seq = NULL;
    m_is_lf_emulating = false;
}

static enum {
    LF_SENSE_STATE_NONE,
    LF_SENSE_STATE_DISABLE,
    LF_SENSE_STATE_ENABLE,
} m_lf_sense_state = LF_SENSE_STATE_NONE;

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
    // ensure buffer size is large enough for specific tag type,
    // so that tag data (e.g., card numbers) can be converted to corresponding pwm sequence here.
    if (type == TAG_TYPE_EM410X && buffer->length >= LF_EM410X_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = em410x_64.alloc();
        m_pwm_seq = em410x_64.modulator(codec, buffer->buffer);
        em410x_64.free(codec);
        NRF_LOG_INFO("load lf em410x data finish.");
        return LF_EM410X_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_HID_PROX && buffer->length >= LF_HIDPROX_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = hidprox.alloc();
        m_pwm_seq = hidprox.modulator(codec, buffer->buffer);
        hidprox.free(codec);
        NRF_LOG_INFO("load lf hidprox data finish.");
        return LF_HIDPROX_TAG_ID_SIZE;
    }

    if (type == TAG_TYPE_VIKING && buffer->length >= LF_VIKING_TAG_ID_SIZE) {
        m_tag_type = type;
        void *codec = viking.alloc();
        m_pwm_seq = viking.modulator(codec, buffer->buffer);
        viking.free(codec);
        NRF_LOG_INFO("load lf viking data finish.");
        return LF_VIKING_TAG_ID_SIZE;
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
    return m_tag_type == TAG_TYPE_EM410X ? LF_EM410X_TAG_ID_SIZE : 0;
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
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(tag_id), (uint8_t *)tag_id);
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
    // default id, must to align(4), more word...
    uint8_t tag_id[13] = {0xDE, 0xAD, 0xBE, 0xEF, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
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
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id
    uint8_t tag_id[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    return lf_tag_data_factory(slot, tag_type, tag_id, sizeof(tag_id));
}
