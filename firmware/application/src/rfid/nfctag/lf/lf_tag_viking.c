#include <stdint.h>

#include "lf_tag_viking.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "fds_util.h"
#include "tag_persistence.h"
#include "bsp_delay.h"

#include "nrf_gpio.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_lpcomp.h"

#define NRF_LOG_MODULE_NAME tag_viking
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


// Get the specified position bit
#define GETBIT(v, bit) ((v >> bit) & 0x01)
// Antenna control
#define ANT_TO_MOD()   nrf_gpio_pin_set(LF_MOD)
#define ANT_NO_MOD()  nrf_gpio_pin_clear(LF_MOD)


// Whether the USB light effect is allowed to enable
extern bool g_usb_led_marquee_enable;

// Bit data carrying 64 -bit ID number
static uint64_t m_id_bit_data = 0;
// The bit position of the card ID currently sent
static uint8_t m_bit_send_position;
// Whether to send the first edge
static bool m_is_send_first_edge;
// The current broadcast ID number is 33ms every few times, and can be broadcast about 30 times a second
static uint8_t m_send_id_count;
// Whether it is currently in the low -frequency card number of broadcasting
static volatile bool m_is_lf_emulating = false;
// The timer of the delivery card number, we use the timer 3
static const nrfx_timer_t m_timer_send_id = NRFX_TIMER_INSTANCE(3);
// Cache label type
static tag_specific_type_t m_tag_type = TAG_TYPE_UNDEFINED;

/**
 * @brief Convert the card number of Viking to the memory layout of U64 and calculate the puppet school inspection
 */
uint64_t viking_id_to_memory64(uint8_t id[4]) {
    //Union, what you see is obtained
    union {
        uint64_t u64;
        struct {
            // 24 header bits
            // byte 1
            uint8_t h00: 1;
            uint8_t h01: 1;
            uint8_t h02: 1;
            uint8_t h03: 1;
            uint8_t h04: 1;
            uint8_t h05: 1;
            uint8_t h06: 1;
            uint8_t h07: 1;
            // byte 2
            uint8_t h08: 1;
            uint8_t h09: 1;
            uint8_t h10: 1;
            uint8_t h11: 1;
            uint8_t h12: 1;
            uint8_t h13: 1;
            uint8_t h14: 1;
            uint8_t h15: 1;
            // byte 3
            uint8_t h16: 1;
            uint8_t h17: 1;
            uint8_t h18: 1;
            uint8_t h19: 1;
            uint8_t h20: 1;
            uint8_t h21: 1;
            uint8_t h22: 1;
            uint8_t h23: 1;

            // 32 data bits
            // byte 1
            uint8_t d00: 1;
            uint8_t d01: 1;
            uint8_t d02: 1;
            uint8_t d03: 1;
            uint8_t d04: 1;
            uint8_t d05: 1;
            uint8_t d06: 1;
            uint8_t d07: 1;

            // byte 2
            uint8_t d08: 1;
            uint8_t d09: 1;
            uint8_t d10: 1;
            uint8_t d11: 1;
            uint8_t d12: 1;
            uint8_t d13: 1;
            uint8_t d14: 1;
            uint8_t d15: 1;

            // byte 3
            uint8_t d16: 1;
            uint8_t d17: 1;
            uint8_t d18: 1;
            uint8_t d19: 1;
            uint8_t d20: 1;
            uint8_t d21: 1;
            uint8_t d22: 1;
            uint8_t d23: 1;

            // byte 4
            uint8_t d24: 1;
            uint8_t d25: 1;
            uint8_t d26: 1;
            uint8_t d27: 1;
            uint8_t d28: 1;
            uint8_t d29: 1;
            uint8_t d30: 1;
            uint8_t d31: 1;

            // 8-bit partity
            uint8_t p00: 1;
            uint8_t p01: 1;
            uint8_t p02: 1;
            uint8_t p03: 1;
            uint8_t p04: 1;
            uint8_t p05: 1;
            uint8_t p06: 1;
            uint8_t p07: 1;
        } bit;
    } memory;

    // Okay, it's the most critical time at present, and now you need to assign and calculate the Qiqi school inspection
    // 1. First assign the front guide code
    memory.bit.h00 = memory.bit.h01 = memory.bit.h02 = memory.bit.h03 = memory.bit.h06 = 1;
    memory.bit.h04 = memory.bit.h05 = memory.bit.h07 =0;
    memory.bit.h08 = memory.bit.h09 = memory.bit.h10 = memory.bit.h11 = 0;
    memory.bit.h12 = memory.bit.h13 = memory.bit.h14 = memory.bit.h15 = 0;
    memory.bit.h16 = memory.bit.h17 = memory.bit.h18 = memory.bit.h19 = 0;
    memory.bit.h20 = memory.bit.h21 = memory.bit.h22 = memory.bit.h23 = 0;

    //2. Assign the data of 32-bit
    // -byte1
    memory.bit.d00 = GETBIT(id[0], 7);
    memory.bit.d01 = GETBIT(id[0], 6);
    memory.bit.d02 = GETBIT(id[0], 5);
    memory.bit.d03 = GETBIT(id[0], 4);
    memory.bit.d04 = GETBIT(id[0], 3);
    memory.bit.d05 = GETBIT(id[0], 2);
    memory.bit.d06 = GETBIT(id[0], 1);
    memory.bit.d07 = GETBIT(id[0], 0);
    // -byte2
    memory.bit.d08 = GETBIT(id[1], 7);
    memory.bit.d09 = GETBIT(id[1], 6);
    memory.bit.d10 = GETBIT(id[1], 5);
    memory.bit.d11 = GETBIT(id[1], 4);
    memory.bit.d12 = GETBIT(id[1], 3);
    memory.bit.d13 = GETBIT(id[1], 2);
    memory.bit.d14 = GETBIT(id[1], 1);
    memory.bit.d15 = GETBIT(id[1], 0);
    // - byte3
    memory.bit.d16 = GETBIT(id[2], 7);
    memory.bit.d17 = GETBIT(id[2], 6);
    memory.bit.d18 = GETBIT(id[2], 5);
    memory.bit.d19 = GETBIT(id[2], 4);
    memory.bit.d20 = GETBIT(id[2], 3);
    memory.bit.d21 = GETBIT(id[2], 2);
    memory.bit.d22 = GETBIT(id[2], 1);
    memory.bit.d23 = GETBIT(id[2], 0);
    // - byte4
    memory.bit.d24 = GETBIT(id[3], 7);
    memory.bit.d25 = GETBIT(id[3], 6);
    memory.bit.d26 = GETBIT(id[3], 5);
    memory.bit.d27 = GETBIT(id[3], 4);
    memory.bit.d28 = GETBIT(id[3], 3);
    memory.bit.d29 = GETBIT(id[3], 2);
    memory.bit.d30 = GETBIT(id[3], 1);
    memory.bit.d31 = GETBIT(id[3], 0);
    // 4. Calculate the checksum

    // ^F2^A8 = 5A = 0101 1010
    memory.bit.p00 = memory.bit.d00 ^ memory.bit.d08 ^ memory.bit.d16 ^ memory.bit.d24;
    memory.bit.p01 = memory.bit.d01 ^ memory.bit.d09 ^ memory.bit.d17 ^ memory.bit.d25 ^ 1;
    memory.bit.p02 = memory.bit.d02 ^ memory.bit.d10 ^ memory.bit.d18 ^ memory.bit.d26;
    memory.bit.p03 = memory.bit.d03 ^ memory.bit.d11 ^ memory.bit.d19 ^ memory.bit.d27 ^ 1;
    memory.bit.p04 = memory.bit.d04 ^ memory.bit.d12 ^ memory.bit.d20 ^ memory.bit.d28 ^ 1;
    memory.bit.p05 = memory.bit.d05 ^ memory.bit.d13 ^ memory.bit.d21 ^ memory.bit.d29;
    memory.bit.p06 = memory.bit.d06 ^ memory.bit.d14 ^ memory.bit.d22 ^ memory.bit.d30 ^ 1;
    memory.bit.p07 = memory.bit.d07 ^ memory.bit.d15 ^ memory.bit.d23 ^ memory.bit.d31;
    //Return to the U64 data in the combination, this is the data we finally need,
    // In the later stage analog card, just take out each bit to send it
    return memory.u64;
}

static void timer_ce_handler(nrf_timer_event_t event_type, void *p_context) {
    bool mod;
    switch (event_type) {
        // Because we are configured using the CC channel 2, the event recovers
        // Detect nrf_timer_event_compare0 event in the function
        case NRF_TIMER_EVENT_COMPARE2: {
            if (m_is_send_first_edge) {
                if (GETBIT(m_id_bit_data, m_bit_send_position)) {
                    // The first edge of the send 1
                    ANT_TO_MOD();
                    mod = true;
                } else {
                    // The first edge of the send 0
                    ANT_NO_MOD();
                    mod = false;
                }
                m_is_send_first_edge = false;   //The second edge is sent next time
            } else {
                if (GETBIT(m_id_bit_data, m_bit_send_position)) {
                    // Send the second edge of 1
                    ANT_NO_MOD();
                    mod = false;
                } else {
                    //The second edge of the send 0
                    ANT_TO_MOD();
                    mod = true;
                }
                m_is_send_first_edge = true;    //The first edge of the next sends next time
            }

            // measure field only during no-mod half of last bit of last broadcast
            if ((! mod) &&
                    (m_bit_send_position + 1 >= LF_125KHZ_VIKING_BIT_SIZE) &&
                    (m_send_id_count + 1 >= LF_125KHZ_BROADCAST_MAX)) {
                nrfx_timer_disable(&m_timer_send_id);                       // Close the timer of the broadcast venue
                // We don't need any events, but only need to detect the state of the field
                NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
                if (lf_is_field_exists()) {
                    nrf_drv_lpcomp_disable();
                    nrfx_timer_enable(&m_timer_send_id);                    // Open the timer of the broadcaster and continue to simulate
                } else {
                    // Open the incident interruption, so that the next event can be in and out normally
                    g_is_tag_emulating = false;                             // Reset the flag in the simulation
                    m_is_lf_emulating = false;
                    TAG_FIELD_LED_OFF()                                     // Make sure the indicator light of the LF field status
                    NRF_LPCOMP->INTENSET = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;
                    // call sleep_timer_start *after* unsetting g_is_tag_emulating
                    sleep_timer_start(SLEEP_DELAY_MS_FIELD_125KHZ_LOST);    // Start the timer to enter the sleep
                    NRF_LOG_INFO("LF FIELD LOST");
                }
            }

            if (m_is_send_first_edge == true) { // The first edge of the next sends next time
                if (++m_bit_send_position >= LF_125KHZ_VIKING_BIT_SIZE) {
                    m_bit_send_position = 0;    // The broadcast is successful once, and the BIT position is zero
                    if(!lf_is_field_exists()){  // To avoid stopping sending when the reader field is present
                        m_send_id_count++;
                    }
                    if (m_send_id_count >= LF_125KHZ_BROADCAST_MAX) {
                        m_send_id_count = 0;                                        //The number of broadcasts reaches the upper limit, re -identifies the status of the field and re -statistically count the number of broadcast times
                    }
                }
            }
            break;
        }
        default: {
            // Nothing to do.
            break;
        }
    }
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
    // Only when the low -frequency simulation is not launched, and the analog card is started
    if (!m_is_lf_emulating && event == NRF_LPCOMP_EVENT_UP) {
        // Turn off dormant delay
        sleep_timer_stop();
        // Close the comparator
        nrf_drv_lpcomp_disable();

        // Set the simulation status logo bit
        m_is_lf_emulating = true;
        g_is_tag_emulating = true;

        // Simulation card status should be turned off the USB light effect
        g_usb_led_marquee_enable = false;

        // LED status update
        set_slot_light_color(RGB_BLUE);
        TAG_FIELD_LED_ON()

        //In any case, every time the state finds changes, you need to reset the BIT location of the sending
        m_send_id_count = 0;
        m_bit_send_position = 0;
        m_is_send_first_edge = true;

        // openThePreciseHardwareTimerToTheBroadcastCardNumber
        nrfx_timer_enable(&m_timer_send_id);

        NRF_LOG_INFO("LF FIELD DETECTED");
    }
}

static void lf_sense_enable(void) {
    ret_code_t err_code;

    nrf_drv_lpcomp_config_t config = NRF_DRV_LPCOMP_DEFAULT_CONFIG;
    config.hal.reference = NRF_LPCOMP_REF_SUPPLY_1_16;
    config.input = LF_RSSI;
    config.hal.detection = NRF_LPCOMP_DETECT_UP;
    config.hal.hyst = NRF_LPCOMP_HYST_50mV;

    err_code = nrf_drv_lpcomp_init(&config, lpcomp_event_handler);
    APP_ERROR_CHECK(err_code);

    // TAG id broadcast
    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    err_code = nrfx_timer_init(&m_timer_send_id, &timer_cfg, timer_ce_handler);
    APP_ERROR_CHECK(err_code);
    nrfx_timer_extended_compare(&m_timer_send_id, NRF_TIMER_CC_CHANNEL2, nrfx_timer_us_to_ticks(&m_timer_send_id, LF_125KHZ_VIKING_BIT_CLOCK), NRF_TIMER_SHORT_COMPARE2_CLEAR_MASK, true);

    if (lf_is_field_exists() && !m_is_lf_emulating) {
        lpcomp_event_handler(NRF_LPCOMP_EVENT_UP);
    }
}

static void lf_sense_disable(void) {
    nrfx_timer_uninit(&m_timer_send_id);    //counterInitializationTimer
    nrfx_lpcomp_uninit();                   //antiInitializationComparator
    m_is_lf_emulating = false;              //setAsNonSimulatedState
}

static enum  {
    LF_SENSE_STATE_NONE,
    LF_SENSE_STATE_DISABLE,
    LF_SENSE_STATE_ENABLE,
} m_lf_sense_state = LF_SENSE_STATE_NONE;

/**
 * @brief switchLfFieldInductionToEnableTheState
 */
void lf_tag_125khz_viking_sense_switch(bool enable) {
    // initializationModulationFootIsOutput
    nrf_gpio_cfg_output(LF_MOD);
    //theDefaultIsNotShortCircuitAntenna (shortCircuitWillCauseRssiToBeUnableToJudge)
    ANT_NO_MOD();

    //forTheFirstTimeOrDisabled,OnlyInitializationIsAllowed
    if (m_lf_sense_state == LF_SENSE_STATE_NONE || m_lf_sense_state == LF_SENSE_STATE_DISABLE) {
        if (enable) {
            m_lf_sense_state = LF_SENSE_STATE_ENABLE;
            lf_sense_enable();
        }
    } else {    // inOtherCases,OnlyAntiInitializationIsAllowed
        if (!enable) {
            m_lf_sense_state = LF_SENSE_STATE_DISABLE;
            lf_sense_disable();
        }
    }
}

/** @brief Viking load data
 * @param type     Refined label type
 * @param buffer   Data buffer
 */
int lf_tag_viking_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    //Make sure that external capacity is enough to convert to an information structure
    if (buffer->length >= LF_VIKING_TAG_ID_SIZE) {
        // The ID card number is directly converted here as the corresponding BIT data stream
        m_tag_type = type;
        m_id_bit_data = viking_id_to_memory64(buffer->buffer);
        NRF_LOG_INFO("LF Viking data load finish.");
    } else {
        NRF_LOG_ERROR("LF_VIKING_TAG_ID_SIZE too big.");
    }
    return LF_VIKING_TAG_ID_SIZE;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined label type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_viking_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this label before allowing saving
    if (m_tag_type != TAG_TYPE_UNDEFINED) {
        // Just save the original card package directly
        return LF_VIKING_TAG_ID_SIZE;
    } else {
        return 0;
    }
}

/** @brief Id card deposit card number before callback
 * @param slot     Card slot number
 * @param tag_type  Refined label type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_viking_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id, must to align(4), more word...
    uint8_t tag_id[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    // Write the data in Flash
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    fds_slot_record_map_t map_info; // Get the special card slot FDS record information
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    //Call the blocked FDS to write the function, and write the data of the specified field type of the card slot into the Flash
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(tag_id), (uint8_t *)tag_id);
    if (ret) {
        NRF_LOG_INFO("Factory slot data success.");
    } else {
        NRF_LOG_ERROR("Factory slot data error.");
    }
    return ret;
}
