#include "lf_tag.h"
#include "bsp_delay.h"
#include "nrf_drv_lpcomp.h"

#define NRF_LOG_MODULE_NAME tag_lf
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/**
* @brief Judgment field status
 */
bool lf_is_field_exists(void) {
    nrf_drv_lpcomp_enable();
    bsp_delay_us(30);                                   // Display for a period of time and sampling to avoid misjudgment
    nrf_lpcomp_task_trigger(NRF_LPCOMP_TASK_SAMPLE);    //Trigger a sampling
    return nrf_lpcomp_result_get() == 1;                //Determine the sampling results of the LF field status
}
