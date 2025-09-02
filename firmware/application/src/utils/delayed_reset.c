#include "delayed_reset.h"

#include "app_timer.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

APP_TIMER_DEF(m_reset_timer);

static void delayed_reset_event_handler(void *ctx)
{
    while (NRF_LOG_PROCESS())
        ;
    ret_code_t ret = sd_nvic_SystemReset();
    APP_ERROR_CHECK(ret);
    while (1) {
        __NOP();
    }
}

void delayed_reset(uint32_t delay)
{
    NRF_LOG_INFO("Resetting in %d ms...", delay);
    ret_code_t ret;
    ret = app_timer_create(&m_reset_timer, APP_TIMER_MODE_SINGLE_SHOT, delayed_reset_event_handler);
    APP_ERROR_CHECK(ret);
    ret = app_timer_start(m_reset_timer, APP_TIMER_TICKS(delay), NULL);
    APP_ERROR_CHECK(ret);
}
