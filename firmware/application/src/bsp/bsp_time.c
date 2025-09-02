#include "bsp_time.h"

#include "app_timer.h"

#define TICK_PERIOD APP_TIMER_TICKS(10)  // Timing

// Define a soft timer
APP_TIMER_DEF(m_app_timer);

// Timer pool
autotimer bsptimers[TIMER_BSP_COUNT] = {0};
// Timer iteration position
static uint8_t g_timer_fori;
// The current timer is running status
static volatile enum {
    UNINIT,
    INIT,
    START,
    STOP,
} bsp_timer_state
    = UNINIT;

/*
 * Get a free timer, this timer
 * 1. Will run automatically
 * 2. It's free
 */
autotimer *bsp_obtain_timer(uint32_t start_value)
{
    uint8_t i;
    for (i = 0; i < TIMER_BSP_COUNT; i++) {
        if (bsptimers[i].busy == 0) {
            bsptimers[i].time = start_value;
            bsptimers[i].busy = 1;
            break;
        }
    }
    return &bsptimers[i];
}

/*
 * Set the timer, the operation will operate the target timer and modify the current value
 */
inline uint8_t bsp_set_timer(autotimer *timer, uint32_t start_value)
{
    if (timer->busy == 0) return 0;
    timer->time = start_value;
    return 1;
}

/*
 * Return the timer, the operation will automatically release the timer
 * And zero to the timer
 */
inline void bsp_return_timer(autotimer *timer)
{
    timer->busy = 0;
    timer->time = 0;
}

/** @brief Test timer callback function
 * @param arg Callback parameter
 * @return none
 */
void timer_app_callback(void *arg)
{
    UNUSED_PARAMETER(arg);
    for (g_timer_fori = 0; g_timer_fori < TIMER_BSP_COUNT; g_timer_fori++) {
        if (bsptimers[g_timer_fori].busy == 1) {
            bsptimers[g_timer_fori].time += 10;
        }
    }
}

// Initialized timer
void bsp_timer_init(void)
{
    if (bsp_timer_state == UNINIT) {
        bsp_timer_state = INIT;
        // Create a timer
        ret_code_t err_code = app_timer_create(&m_app_timer, APP_TIMER_MODE_REPEATED, timer_app_callback);
        APP_ERROR_CHECK(err_code);
    }
}

// Counter -initialization timer
void bsp_timer_uninit(void)
{
    // Can't reverse the initialized soft timer for the time being, it can only be closed
    bsp_timer_stop();
}

// Start the timer
void bsp_timer_start(void)
{
    if (bsp_timer_state != UNINIT) {
        // Make sure the timer is not started
        if (bsp_timer_state != START) {
            app_timer_start(m_app_timer, TICK_PERIOD, NULL);
            bsp_timer_state = START;
        }
    }
}

// Stop timer
void bsp_timer_stop(void)
{
    if (bsp_timer_state != UNINIT) {
        if (bsp_timer_state == START) {
            // Stop timer
            app_timer_stop(m_app_timer);
            bsp_timer_state = STOP;
        }
    }
}
