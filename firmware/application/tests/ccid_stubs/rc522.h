#ifndef RC522_H
#define RC522_H

#include <stdint.h>

/* Keep the host mock's card layout identical to the RC522 public structure. */
typedef struct {
    uint8_t uid[10];
    uint8_t uid_len;
    uint8_t cascade;
    uint8_t sak;
    uint8_t atqa[2];
    uint8_t ats[0xFF];
    uint8_t ats_len;
} picc_14a_tag_t;

void pcd_14a_reader_reset(void);
void pcd_14a_reader_antenna_on(void);
void pcd_14a_reader_antenna_off(void);
uint16_t pcd_14a_reader_timeout_get(void);
void pcd_14a_reader_timeout_set(uint16_t timeout_ms);
uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag);

#endif
