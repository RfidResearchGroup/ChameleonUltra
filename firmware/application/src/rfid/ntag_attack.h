#ifndef NTAG_ATTACK_H
#define NTAG_ATTACK_H

#include <stdint.h>
#include <stdbool.h>

bool ntag_attack_run(char *out_buffer, uint16_t max_len);

#endif
