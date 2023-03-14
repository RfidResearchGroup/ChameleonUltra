/**
 * Copyright (c) 2018 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef SHIELD_H__
#define SHIELD_H__

#define JOYSTICK_V_SAADC_INPUT      NRF_SAADC_INPUT_AIN2
#define JOYSTICK_H_SAADC_INPUT      NRF_SAADC_INPUT_AIN1
#define JOYSTICK_SEL_PIN            ARDUINO_8_PIN

#define BUTTON_A_PIN                ARDUINO_2_PIN
#define BUTTON_B_PIN                ARDUINO_3_PIN
#define BUTTON_C_PIN                ARDUINO_4_PIN
#define BUTTON_D_PIN                ARDUINO_5_PIN
#define BUTTON_E_PIN                ARDUINO_6_PIN
#define BUTTON_F_PIN                ARDUINO_7_PIN

#undef BUTTONS_NUMBER
#define BUTTONS_NUMBER 7

#undef BUTTON_1
#undef BUTTON_2
#undef BUTTON_3
#undef BUTTON_4

#define BUTTON_1       BUTTON_A_PIN
#define BUTTON_3       BUTTON_B_PIN
#define BUTTON_2       BUTTON_C_PIN
#define BUTTON_4       BUTTON_E_PIN


#define BUTTON_5       BUTTON_D_PIN
#define BUTTON_6       BUTTON_F_PIN
#define BUTTON_7       JOYSTICK_SEL_PIN

#define BSP_BUTTON_4   BUTTON_5
#define BSP_BUTTON_5   BUTTON_6
#define BSP_BUTTON_6   BUTTON_7


#undef BUTTONS_LIST
#define BUTTONS_LIST { BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, \
                       BUTTON_5, BUTTON_6, BUTTON_7 }

#endif /* SHIELD_H__ */
