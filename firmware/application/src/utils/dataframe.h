#ifndef DATAFRAME_H
#define DATAFRAME_H

#include <stdint.h>
#include <stdbool.h>

// Data frame process callback
typedef void (*data_frame_cbk_t)(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

// TX buffer
typedef struct {
    uint8_t *const buffer;
    uint16_t length;
} data_frame_tx_t;

typedef enum {
    DATA_FRAME_ORIGIN_UNKNOWN = 0,
    DATA_FRAME_ORIGIN_USB,
    DATA_FRAME_ORIGIN_BLE,
} data_frame_origin_t;

// Legacy ingress has unknown origin and must not authorize transport-only commands.
void data_frame_receive(uint8_t *data, uint16_t length);
// Overlapping ingress is dropped; parser ownership never masks IRQs for payload copies.
void data_frame_receive_from(uint8_t *data, uint16_t length, data_frame_origin_t origin);
void data_frame_reset(void);
// Discard this transport's partial/queued frame; a running callback finishes normally.
void data_frame_disconnect(data_frame_origin_t origin);
// Origin of the dispatched frame, stable throughout callback; UNKNOWN otherwise.
data_frame_origin_t data_frame_get_origin(void);
void data_frame_process(void);
void on_data_frame_complete(data_frame_cbk_t callback);

data_frame_tx_t *data_frame_make(
    uint16_t cmd,
    uint16_t status,
    uint16_t length,
    uint8_t *data
);


#endif // DATAFRAME_H
