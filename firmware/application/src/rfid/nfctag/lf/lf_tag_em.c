#include <stdint.h>

#include "lf_tag_em.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "fds_util.h"
#include "tag_persistence.h"
#include "bsp_delay.h"

#include "nrf_gpio.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_lpcomp.h"

#define NRF_LOG_MODULE_NAME tag_em410x
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
const nrfx_timer_t m_timer_send_id = NRFX_TIMER_INSTANCE(3);
// Cache label type
static tag_specific_type_t m_tag_type = TAG_TYPE_UNDEFINED;

/**
 * @brief Convert the card number of EM410X to the memory layout of U64 and calculate the puppet school inspection
 *  According to the instructions of the manual, EM4100 is sufficient to accommodate U64
 */
uint64_t em410x_id_to_memory64(uint8_t id[5]) {
    //Union, what you see is obtained
    union {
        uint64_t u64;
        struct {
            // 9 header bits
            uint8_t h00: 1;
            uint8_t h01: 1;
            uint8_t h02: 1;
            uint8_t h03: 1;
            uint8_t h04: 1;
            uint8_t h05: 1;
            uint8_t h06: 1;
            uint8_t h07: 1;
            uint8_t h08: 1;
            // 8 version bits and 2 bit parity
            uint8_t d00: 1;
            uint8_t d01: 1;
            uint8_t d02: 1;
            uint8_t d03: 1;
            uint8_t p0: 1;
            uint8_t d10: 1;
            uint8_t d11: 1;
            uint8_t d12: 1;
            uint8_t d13: 1;
            uint8_t p1: 1;
            // 32 data bits and 8 bit parity
            uint8_t d20: 1;
            uint8_t d21: 1;
            uint8_t d22: 1;
            uint8_t d23: 1;
            uint8_t p2: 1;
            uint8_t d30: 1;
            uint8_t d31: 1;
            uint8_t d32: 1;
            uint8_t d33: 1;
            uint8_t p3: 1;
            uint8_t d40: 1;
            uint8_t d41: 1;
            uint8_t d42: 1;
            uint8_t d43: 1;
            uint8_t p4: 1;
            uint8_t d50: 1;
            uint8_t d51: 1;
            uint8_t d52: 1;
            uint8_t d53: 1;
            uint8_t p5: 1;
            uint8_t d60: 1;
            uint8_t d61: 1;
            uint8_t d62: 1;
            uint8_t d63: 1;
            uint8_t p6: 1;
            uint8_t d70: 1;
            uint8_t d71: 1;
            uint8_t d72: 1;
            uint8_t d73: 1;
            uint8_t p7: 1;
            uint8_t d80: 1;
            uint8_t d81: 1;
            uint8_t d82: 1;
            uint8_t d83: 1;
            uint8_t p8: 1;
            uint8_t d90: 1;
            uint8_t d91: 1;
            uint8_t d92: 1;
            uint8_t d93: 1;
            uint8_t p9: 1;
            // 5 bit end.
            uint8_t pc0: 1;
            uint8_t pc1: 1;
            uint8_t pc2: 1;
            uint8_t pc3: 1;
            uint8_t s0: 1;
        } bit;
    } memory;

    // Okay, it's the most critical time at present, and now you need to assign and calculate the Qiqi school inspection
    // 1. First assign the front guide code
    memory.bit.h00 = memory.bit.h01 = memory.bit.h02 =
                                          memory.bit.h03 = memory.bit.h04 = memory.bit.h05 =
                                                               memory.bit.h06 = memory.bit.h07 = memory.bit.h08 = 1;
    //2. Assign the 8bit version or custom ID
    memory.bit.d00 = GETBIT(id[0], 7);
    memory.bit.d01 = GETBIT(id[0], 6);
    memory.bit.d02 = GETBIT(id[0], 5);
    memory.bit.d03 = GETBIT(id[0], 4);
    memory.bit.p0 = memory.bit.d00 ^ memory.bit.d01 ^ memory.bit.d02 ^ memory.bit.d03;
    memory.bit.d10 = GETBIT(id[0], 3);
    memory.bit.d11 = GETBIT(id[0], 2);
    memory.bit.d12 = GETBIT(id[0], 1);
    memory.bit.d13 = GETBIT(id[0], 0);
    memory.bit.p1 = memory.bit.d10 ^ memory.bit.d11 ^ memory.bit.d12 ^ memory.bit.d13;
    // 3. Assign the data of 32Bit
    // -byte1
    memory.bit.d20 = GETBIT(id[1], 7);
    memory.bit.d21 = GETBIT(id[1], 6);
    memory.bit.d22 = GETBIT(id[1], 5);
    memory.bit.d23 = GETBIT(id[1], 4);
    memory.bit.p2 = memory.bit.d20 ^ memory.bit.d21 ^ memory.bit.d22 ^ memory.bit.d23;
    memory.bit.d30 = GETBIT(id[1], 3);
    memory.bit.d31 = GETBIT(id[1], 2);
    memory.bit.d32 = GETBIT(id[1], 1);
    memory.bit.d33 = GETBIT(id[1], 0);
    memory.bit.p3 = memory.bit.d30 ^ memory.bit.d31 ^ memory.bit.d32 ^ memory.bit.d33;
    // - byte2
    memory.bit.d40 = GETBIT(id[2], 7);
    memory.bit.d41 = GETBIT(id[2], 6);
    memory.bit.d42 = GETBIT(id[2], 5);
    memory.bit.d43 = GETBIT(id[2], 4);
    memory.bit.p4 = memory.bit.d40 ^ memory.bit.d41 ^ memory.bit.d42 ^ memory.bit.d43;
    memory.bit.d50 = GETBIT(id[2], 3);
    memory.bit.d51 = GETBIT(id[2], 2);
    memory.bit.d52 = GETBIT(id[2], 1);
    memory.bit.d53 = GETBIT(id[2], 0);
    memory.bit.p5 = memory.bit.d50 ^ memory.bit.d51 ^ memory.bit.d52 ^ memory.bit.d53;
    // - byte3
    memory.bit.d60 = GETBIT(id[3], 7);
    memory.bit.d61 = GETBIT(id[3], 6);
    memory.bit.d62 = GETBIT(id[3], 5);
    memory.bit.d63 = GETBIT(id[3], 4);
    memory.bit.p6 = memory.bit.d60 ^ memory.bit.d61 ^ memory.bit.d62 ^ memory.bit.d63;
    memory.bit.d70 = GETBIT(id[3], 3);
    memory.bit.d71 = GETBIT(id[3], 2);
    memory.bit.d72 = GETBIT(id[3], 1);
    memory.bit.d73 = GETBIT(id[3], 0);
    memory.bit.p7 = memory.bit.d70 ^ memory.bit.d71 ^ memory.bit.d72 ^ memory.bit.d73;
    // - byte4
    memory.bit.d80 = GETBIT(id[4], 7);
    memory.bit.d81 = GETBIT(id[4], 6);
    memory.bit.d82 = GETBIT(id[4], 5);
    memory.bit.d83 = GETBIT(id[4], 4);
    memory.bit.p8 = memory.bit.d80 ^ memory.bit.d81 ^ memory.bit.d82 ^ memory.bit.d83;
    memory.bit.d90 = GETBIT(id[4], 3);
    memory.bit.d91 = GETBIT(id[4], 2);
    memory.bit.d92 = GETBIT(id[4], 1);
    memory.bit.d93 = GETBIT(id[4], 0);
    memory.bit.p9 = memory.bit.d90 ^ memory.bit.d91 ^ memory.bit.d92 ^ memory.bit.d93;
    // 4. Calculate the vertical puppet verification
    memory.bit.pc0 = memory.bit.d00 ^ memory.bit.d10 ^ memory.bit.d20 ^ memory.bit.d30 ^ memory.bit.d40 ^ memory.bit.d50 ^ memory.bit.d60 ^ memory.bit.d70 ^ memory.bit.d80 ^ memory.bit.d90;
    memory.bit.pc1 = memory.bit.d01 ^ memory.bit.d11 ^ memory.bit.d21 ^ memory.bit.d31 ^ memory.bit.d41 ^ memory.bit.d51 ^ memory.bit.d61 ^ memory.bit.d71 ^ memory.bit.d81 ^ memory.bit.d91;
    memory.bit.pc2 = memory.bit.d02 ^ memory.bit.d12 ^ memory.bit.d22 ^ memory.bit.d32 ^ memory.bit.d42 ^ memory.bit.d52 ^ memory.bit.d62 ^ memory.bit.d72 ^ memory.bit.d82 ^ memory.bit.d92;
    memory.bit.pc3 = memory.bit.d03 ^ memory.bit.d13 ^ memory.bit.d23 ^ memory.bit.d33 ^ memory.bit.d43 ^ memory.bit.d53 ^ memory.bit.d63 ^ memory.bit.d73 ^ memory.bit.d83 ^ memory.bit.d93;
    //5. Set the position of the last EOF, this wave of conversion is over
    memory.bit.s0 = 0;
    //Return to the U64 data in the combination, this is the data we finally need,
    // In the later stage analog card, just take out each bit to send it
    return memory.u64;
}

/**
* @brief Judgment field status
 */
bool lf_is_field_exists(void) {
    nrf_drv_lpcomp_enable();
    // With 20ms of delay CU was not able to detect the field of my reader after waking up.
    bsp_delay_us(30);                                   // Display for a period of time and sampling to avoid misjudgment
    nrf_lpcomp_task_trigger(NRF_LPCOMP_TASK_SAMPLE);    //Trigger a sampling
    return nrf_lpcomp_result_get() == 1;                //Determine the sampling results of the LF field status
}

void timer_ce_handler(nrf_timer_event_t event_type, void *p_context) {
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
                    (m_bit_send_position + 1 >= LF_125KHZ_EM410X_BIT_SIZE) &&
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
                if (++m_bit_send_position >= LF_125KHZ_EM410X_BIT_SIZE) {
                    m_bit_send_position = 0;    // The broadcast is successful once, and the BIT position is zero
/* The main part of the idea. The original EM4100 tag continuously sends it's ID.
* The root problem, in my point of view, was that CU started to "feel" the field too far to be able to modulate it deep enough,
* and 3 times (LF_125KHZ_BROADCAST_MAX) of repeating takes only about 100ms, CU is still not close enough to the reader.
* That is why the emulation worked only if the CU moved past the reader quickly (fly by).*/
                    if(!lf_is_field_exists()){
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
    nrfx_timer_extended_compare(&m_timer_send_id, NRF_TIMER_CC_CHANNEL2, nrfx_timer_us_to_ticks(&m_timer_send_id, LF_125KHZ_EM410X_BIT_CLOCK), NRF_TIMER_SHORT_COMPARE2_CLEAR_MASK, true);

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
void lf_tag_125khz_sense_switch(bool enable) {
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

/** @brief EM410X load data
 * @param type     Refined label type
 * @param buffer   Data buffer
 */
int lf_tag_em410x_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    //Make sure that external capacity is enough to convert to an information structure
    if (buffer->length >= LF_EM410X_TAG_ID_SIZE) {
        // The ID card number is directly converted here as the corresponding BIT data stream
        m_tag_type = type;
        m_id_bit_data = em410x_id_to_memory64(buffer->buffer);
        NRF_LOG_INFO("LF Em410x data load finish.");
    } else {
        NRF_LOG_ERROR("LF_EM410X_TAG_ID_SIZE too big.");
    }
    return LF_EM410X_TAG_ID_SIZE;
}

/** @brief Id card deposit card number before callback
 * @param type      Refined label type
 * @param buffer    Data buffer
 * @return The length of the data that needs to be saved is that it does not save when 0
 */
int lf_tag_em410x_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    // Make sure to load this label before allowing saving
    if (m_tag_type != TAG_TYPE_UNDEFINED) {
        // Just save the original card package directly
        return LF_EM410X_TAG_ID_SIZE;
    } else {
        return 0;
    }
}

/** @brief Id card deposit card number before callback
 * @param slot     Card slot number
 * @param tag_type  Refined label type
 * @return Whether the format is successful, if the formatting is successful, it will return to True, otherwise False will be returned
 */
bool lf_tag_em410x_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    // default id, must to align(4), more word...
    uint8_t tag_id[5] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x88 };
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
