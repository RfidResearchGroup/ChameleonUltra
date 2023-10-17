#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_main.h"
#include "lf_125khz_radio.h"


#define NRF_LOG_MODULE_NAME lf_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


// The default card search is available N Millisecond timeout
uint32_t g_timeout_readem_ms = 500;


/**
* Search EM410X tag
*/
uint8_t PcdScanEM410X(uint8_t *uid) {
    uint8_t ret = STATUS_EM410X_TAG_NO_FOUND;
    if (em410x_read(uid, g_timeout_readem_ms) == 1) {
        ret = STATUS_LF_TAG_OK;
    }
    return ret;
}

/**
* Check whether there is a specified UID tag on the current field
*/
uint8_t check_write_ok(uint8_t *uid, uint8_t *newuid, uint8_t on_uid_diff_return) {
    // After the card is written, we need to read it once,
    // If the data I read is incorrect, it means that the writing fails
    if (PcdScanEM410X(newuid) != STATUS_LF_TAG_OK) {
        return STATUS_EM410X_TAG_NO_FOUND;
    }
    // If you read the card number the same
    // Explanation is successful (maybe)
    if (
        uid[0] == newuid[0] &&
        uid[1] == newuid[1] &&
        uid[2] == newuid[2] &&
        uid[3] == newuid[3] &&
        uid[4] == newuid[4]) {
        return STATUS_LF_TAG_OK;
    }
    // If you find the card, the card number is wrong,
    // Then we will return the abnormal value of the inlet
    return on_uid_diff_return;
}

/**
* Write T55XX tag
*/
uint8_t PcdWriteT55XX(uint8_t *uid, uint8_t *newkey, uint8_t *old_keys, uint8_t old_key_count) {
    uint8_t datas[8] = { 255 };
    uint8_t i;

    init_t55xx_hw();
    start_lf_125khz_radio();

    bsp_delay_ms(1);    // Delays for a while after starting the field

    // keys Need at least two, one newkey, one Oldkey
    // one key The length is 4 Byte
    // uid newkey oldkeys * n

    // The key transmitted in iterative,
    // Reset T55XX tags
    // printf("The old keys count: %d\r\n", old_key_count);
    for (i = 0; i < old_key_count; i++) {
        T55xx_Reset_Passwd(old_keys + (i * 4), newkey);
        /*
        printf("oldkey is: %02x%02x%02x%02x\r\n",
            (old_keys + (i * 4))[0],
            (old_keys + (i * 4))[1],
            (old_keys + (i * 4))[2],
            (old_keys + (i * 4))[3]
        );*/
    }

    // In order to avoid the labels of a special control area,
    // We use the new key here to reset the control area
    T55xx_Reset_Passwd(newkey, newkey);

    // The data encoded 410X is the block data to prepare for the card writing
    em410x_encoder(uid, datas);

    // After the key is reset, perform the card writing operation
    /*
    printf("newkey is: %02x%02x%02x%02x\r\n",
        newkey[0],
        newkey[1],
        newkey[2],
        newkey[3]
    );
    */
    T55xx_Write_data(newkey, datas);

    stop_lf_125khz_radio();

    // Read the verification and return the results of the card writing
    // Do not read it here, you can check it by the upper machine
    return STATUS_LF_TAG_OK;
}

/**
* Set the time value of the card search timeout of the EM card
*/
void SetEMScanTagTimeout(uint32_t ms) {
    g_timeout_readem_ms = ms;
}
