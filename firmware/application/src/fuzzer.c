#include <stdint.h>
#include <stdlib.h>
#include "app_cmd.h"
#include "data_cmd_fuzz.h"
#include "nrf_log.h"

// Simple firmware fuzzer: on command, run a loop sending random data to command processors
static data_frame_tx_t *cmd_processor_fuzzer_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data)
{
    NRF_LOG_INFO("[FUZZ] Fuzzer mode activated");
    for (int i = 0; i < 100; ++i)
    {
        uint16_t fuzz_cmd = (rand() % 2000) + 1000; // valid command range
        uint8_t fuzz_data[32];
        for (int j = 0; j < sizeof(fuzz_data); ++j)
            fuzz_data[j] = rand() & 0xFF;
        NRF_LOG_INFO("[FUZZ] Sending cmd=0x%04X, len=%d", fuzz_cmd, (int)sizeof(fuzz_data));
        // Call the normal command processor (simulate as if received from host)
        on_data_frame_received(fuzz_cmd, 0, sizeof(fuzz_data), fuzz_data);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

// Add prototype for registration
static data_frame_tx_t *cmd_processor_fuzzer_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
