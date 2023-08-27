#ifndef BLE_MAIN_H
#define BLE_MAIN_H

#include "ble_gatts.h"
#include "ble_nus.h"
#include "ble_bas.h"


extern uint16_t batt_lvl_in_milli_volts;
extern uint8_t  percentage_batt_lvl;

void ble_slave_init(void);
void advertising_start(bool erase_bonds);
void advertising_stop(void);
void delete_bonds_all(void);
void nus_data_response(uint8_t *p_data, uint16_t length);
bool is_nus_working(void);
void set_ble_connect_key(uint8_t* key);

#endif
