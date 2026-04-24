#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nrf_pwm.h"

// Shared PSK1 (BPSK on a carrier/2 subcarrier) tag-emulation parameters for the
// tag-emulation PWM path. The PWM base clock is set to 1MHz by pwm_init when an
// active PSK1 tag type is loaded (see tag_base_type.h IS_PSK1_TYPE), so one tick
// is 1us. With counter_top=16 each PWM entry spans one 16us subcarrier cycle.
// Nordic PS: COUNTERTOP in WaveForm mode has a minimum valid value of 3; 16 is
// comfortably above that.
#define LF_PSK1_SUBCARRIER_TOP          (16)
#define LF_PSK1_SUBCARRIER_DUTY         (8)
#define LF_PSK1_RF32_SUBCYCLES_PER_BIT  (16)

// Build a PSK1 wave-form PWM sequence for a frame transmitted MSB first.
//
// Each bit spans LF_PSK1_RF32_SUBCYCLES_PER_BIT PWM entries (one subcarrier
// cycle per entry). On every bit transition the subcarrier phase is inverted
// by flipping the channel_0 polarity MSB, so PSK1 differential encoding is
// expressed as polarity toggles in the sequence.
//
// The initial "last bit" used for the first transition check is the LSB of
// the packed frame, i.e. the final bit of the frame in transmission order.
// This makes the wrap from the last to the first PWM entry of the looped
// sequence produce the same phase relation a continuously-emitting passive
// tag would present to the reader.
//
// Parameters:
//   frame_bytes   - frame bytes, MSB first, bit 0 of byte 0 is the first bit on air
//   bit_count     - number of bits to emit (e.g. 64 for standard IDTECK)
//   out_buf       - destination buffer for wave_form entries
//   out_capacity  - number of entries available in out_buf
//
// Returns the number of entries written, or 0 if out_buf is too small for
// (bit_count * LF_PSK1_RF32_SUBCYCLES_PER_BIT) entries.
size_t lf_psk1_build_sequence(const uint8_t *frame_bytes,
                              size_t bit_count,
                              nrf_pwm_values_wave_form_t *out_buf,
                              size_t out_capacity);
