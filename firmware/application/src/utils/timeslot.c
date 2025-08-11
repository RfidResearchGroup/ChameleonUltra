#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_gpio.h"

#define NRF_LOG_MODULE_NAME timeslot
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "timeslot.h"


/**Constants for timeslot API
*/
static nrf_radio_request_t  m_timeslot_request;
static uint32_t             m_slot_length;
static nrf_radio_signal_callback_return_param_t signal_callback_return_param;
static timeslot_callback_t  m_callback = NULL;
static volatile bool m_is_timeslot_running = false;
static volatile bool m_is_timeslot_working = false;


/**@brief Configure next timeslot event in earliest configuration
 */
void configure_next_event_earliest(void) {
    m_timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;       // The first request to ask TimesLot must
    m_timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_NO_GUARANTEE;  // No need to automatically enable external high -frequency crystals
    m_timeslot_request.params.earliest.priority    = NRF_RADIO_PRIORITY_HIGH;           // Must use high priority
    m_timeslot_request.params.earliest.length_us   = m_slot_length;                     // Timeslot duration
    m_timeslot_request.params.earliest.timeout_us  = NRF_RADIO_EARLIEST_TIMEOUT_MAX_US; // Waiting for Timeslot timeout
}

/**@brief Request next timeslot event in earliest configuration
 */
uint32_t request_next_event_earliest(void) {
    configure_next_event_earliest();
    return sd_radio_request(&m_timeslot_request);
}


/**@brief Timeslot signal handler
 */
static void t55xx_soc_evt_handler(uint32_t evt_id, void *p_context) {
    //NRF_LOG_INFO("t55xx_soc_evt_handler: %d", evt_id);
    uint32_t err_code;
    switch (evt_id) {
        case NRF_EVT_RADIO_SIGNAL_CALLBACK_INVALID_RETURN:
            // No implementation needed
            break;
        case NRF_EVT_RADIO_SESSION_IDLE:    //The session is idle, there is no time order to be interspersed
            //NRF_LOG_INFO("NRF_EVT_RADIO_SESSION_IDLE");
            break;
        case NRF_EVT_RADIO_SESSION_CLOSED:  // The session is closed
            // No implementation needed, session ended
            //NRF_LOG_INFO("NRF_EVT_RADIO_SESSION_CLOSED");
            m_is_timeslot_working = false;
            break;
        case NRF_EVT_RADIO_BLOCKED: // Fall through
        case NRF_EVT_RADIO_CANCELED:
            err_code = request_next_event_earliest();
            APP_ERROR_CHECK(err_code);
            break;
        default:
            break;
    }
}
/* Define a nrf_sdh_soc event observer to receive SoftDevice system events. */
NRF_SDH_SOC_OBSERVER(m_sys_obs, 0, t55xx_soc_evt_handler, NULL);


/**@brief Timeslot event handler
 */
nrf_radio_signal_callback_return_param_t *radio_callback(uint8_t signal_type) {
    //NRF_LOG_INFO("radio_callback: %d", signal_type);
    switch (signal_type) {
        case NRF_RADIO_CALLBACK_SIGNAL_TYPE_START:
            signal_callback_return_param.params.request.p_next = NULL;
            signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE;

            // Successful request, set the logo position
            m_is_timeslot_working = true;
            break;

        default:
            // No implementation needed
            break;
    }
    return (&signal_callback_return_param);
}

/**
 * Request a sequence for high -precision operation
 */
void request_timeslot(uint32_t time_us, timeslot_callback_t callback) {
    ret_code_t err_code;

    // Make sure there is only one at the same time
    APP_ERROR_CHECK_BOOL(!m_is_timeslot_running);
    m_is_timeslot_running = true;

    m_slot_length = time_us;    //The time for configuration application
    m_callback    = callback;   // The configuration time application is applied to the operation after execution

    // Open the session
    err_code = sd_radio_session_open(radio_callback);
    APP_ERROR_CHECK(err_code);

    // Request timing
    err_code = request_next_event_earliest();
    APP_ERROR_CHECK(err_code);

    // The sequence request is successful
    while (!m_is_timeslot_working) {
        NRF_LOG_PROCESS();
    }

    // Enter the critical point
    NVIC_DisableIRQ(RADIO_IRQn);
    NVIC_DisableIRQ(TIMER0_IRQn);
    NVIC_DisableIRQ(TIMER2_IRQn);
    NVIC_DisableIRQ(GPIOTE_IRQn);
    NVIC_DisableIRQ(MWU_IRQn);
    NVIC_DisableIRQ(RTC1_IRQn);

    // The request timing is successful, handle the task quickly
    if (m_callback != NULL) {
        m_callback();       // Execute task
        m_callback = NULL;  // Destruction record
    }

    // Exit critical point
    NVIC_EnableIRQ(GPIOTE_IRQn);
    NVIC_EnableIRQ(RTC1_IRQn);
    NVIC_EnableIRQ(MWU_IRQn);

    //Close the session and wait for the closure to complete
    err_code = sd_radio_session_close();
    APP_ERROR_CHECK(err_code);
    while (m_is_timeslot_working) {
        __NOP();
    }

    // The task process is completed and the ending work
    m_is_timeslot_running = false;

    //NRF_LOG_INFO("request timeslot done.");
}

/**
 * Start performing high -precision operations
 */
void timeslot_start(uint32_t time_us) {
    ret_code_t err_code;

    // Make sure there is only one at the same time
    APP_ERROR_CHECK_BOOL(!m_is_timeslot_running);
    m_is_timeslot_running = true;

    m_slot_length = time_us * 1000;    //The time for configuration application

    // Open the session
    err_code = sd_radio_session_open(radio_callback);
    APP_ERROR_CHECK(err_code);

    // Request timing
    err_code = request_next_event_earliest();
    APP_ERROR_CHECK(err_code);

    // The sequence request is successful
    while (!m_is_timeslot_working) {
        NRF_LOG_PROCESS();
    }

    // Enter the critical point
    NVIC_DisableIRQ(RADIO_IRQn);
    NVIC_DisableIRQ(TIMER0_IRQn);
    NVIC_DisableIRQ(TIMER2_IRQn);
    //NVIC_DisableIRQ(GPIOTE_IRQn);
    NVIC_DisableIRQ(MWU_IRQn);
    NVIC_DisableIRQ(RTC1_IRQn);
    //NRF_LOG_INFO("timeslot start.");
}

/**
 * Stop high -precision operation
 */
void timeslot_stop(void) {
    ret_code_t err_code;

    // Make sure that there is already a timeSlot running
    APP_ERROR_CHECK_BOOL(m_is_timeslot_running);
    m_is_timeslot_running = false;

    // Exit critical point
    //NVIC_EnableIRQ(GPIOTE_IRQn);
    NVIC_EnableIRQ(RTC1_IRQn);
    NVIC_EnableIRQ(MWU_IRQn);

    // Close the session and wait for the closure to complete
    err_code = sd_radio_session_close();
    APP_ERROR_CHECK(err_code);
    while (m_is_timeslot_working) {
        __NOP();
    }

    // The task process is completed and the ending work
    m_is_timeslot_running = false;
    //NRF_LOG_INFO("timeslot stop.");
}
