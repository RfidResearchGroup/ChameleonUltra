#ifndef BLE_MAIN_H
#define BLE_MAIN_H

#include "ble_gatts.h"
#include "ble_nus.h"
#include "ble_bas.h"


void ble_slave_init(void);
void advertising_start(void);
void nus_data_reponse(uint8_t *p_data, uint16_t length);
bool is_nus_working(void);

#endif
