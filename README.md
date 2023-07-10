[nrf52_nfc_module_doc]: https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fnfc.html

[nxp_rc522_datasheet]: https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf

![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Header.png)

# ChameleonUltra

Why not keep using ATxmega128?
First of all, it is difficult to buy chips because the lead time for the main chip is too long, and because the price
has skyrocketed. Secondly, because the interaction speed of the ATxmega simulation is slow, the decryption performance
of the READER mode cannot meet the needs, and the low-frequency function cannot be added, so we have been trying to
upgrade it, such as using the latest ARM to replace the AVR framework, and the performance will definitely be greatly
improved.

# Why NRF52840?

NRF52840 has a built-in NFC Tag-A module, but no one seems to care about it. After playing with HydraNFC's TRF7970A and
FlipperZero's ST25R3916, ~~the developers found that they can only simulate MIFARE UID~~. I accidentally tested the NFC of
52840, and found that it is not only surprisingly easy to simulate a complete MIFARE card, but also has very good
simulation performance, friendly data flow interaction, and very fast response, unlike the former which is limited by
the SPI bus clock rate. We also found that it has ultra-low power consumption, ultra-small size, 256kb/1M large RAM and
FLASH, also has BLE5.0 and USB2.0 FS, super CotexM4F, most importantly, he is very cheap! This is undoubtedly a treasure
discovery for us!

Below we will explain in detail how we exploited the performance of the NRF52840, and what seemingly impossible
functions have been realized with it!

**Update**:
*  FlipperZero can simulate mifare sector now, but FDT so high.

# Supported functions

## High Frequency Attack

| Attack Type  |   Tag Type    | Whether the hardware supports | Does the software support | Whether the application layer supports |                    Note |
|--------------|:-------------:|------------------------------:|---------------------------|:--------------------------------------:|------------------------:|
| Sniffing     |      No       |                            No | No                        |                   No                   |                         |
| MFKEY32 V2   | MifareClassic |                       Support | Support                   |                Support                 | MifareClassic Detection |
| Darkside     | MifareClassic |                       Support | Support                   |                Support                 |    Encrypted 4 bit NAck |
| Nested       | MifareClassic |                       Support | Support                   |                Support                 |    PRNG(Distance guess) |
| StaticNested | MifareClassic |                       Support | Support                   |          Not yet implemented           |  PRNG(2NT Fast Decrypt) |
| HardNested   | MifareClassic |                       Support | Support                   |          Not yet implemented           |                      No |
| Relay attack |   ISO14443A   |                       Support | Support                   |          Not yet implemented           |                      No |

## High Frequency Simulation

| Card Type                     |    Encoding Type     | Whether the hardware supports | Does the software support | Whether the application layer supports |                                     Note |
|-------------------------------|:--------------------:|------------------------------:|---------------------------|:--------------------------------------:|-----------------------------------------:|
| Non <13.56MHz or ISO14443A>   |          No          |                            No | No                        |                   No                   | [NRF52 NFC Module][nrf52_nfc_module_doc] |
| NTAG 21x (210-218)            | ISO14443A/106 kbit/s |                       Support | Support                   |          Not yet implemented           |                                          |
| Mifare Ultralight             | ISO14443A/106 kbit/s |                       Support | Support                   |          Not yet implemented           |                                          |
| Mifare Ultralight Ev1         | ISO14443A/106 kbit/s |                       Support | Support                   |          Not yet implemented           |                                          |
| Mifare Ultralight C           | ISO14443A/106 kbit/s |                       Support | Support                   |          Not yet implemented           |                                          |
| MifareClassic1K/2K/4K (4B/7B) | ISO14443A/106 kbit/s |                       Support | Support                   |                Support                 |                                          |
| Mifare DESFire                | ISO14443A High Rate  |       Only supported Low rate | Only supported Low rate   |          Not yet implemented           |                                          |
| Mifare DESFire EV1            | ISO14443A High rate  |       Only supported Low rate | Only supported Low rate   |          Not yet implemented           |                      Backward compatible |
| Mifare DESFire EV2            | ISO14443A High rate  |       Only supported Low rate | Only supported Low rate   |          Not yet implemented           |                                          |
| Mifare PLUS                   | ISO14443A High rate  |       Only supported Low rate | Only supported Low rate   |          Not yet implemented           |                                          |

## High Frequency Reader

| Card Type                     |    Encoding Type     |                Whether the hardware supports | Does the software support                    | Whether the application layer supports |                                       Note |
|-------------------------------|:--------------------:|---------------------------------------------:|----------------------------------------------|:--------------------------------------:|-------------------------------------------:|
| Non <13.56MHz or ISO14443A>   |          No          |                                           No | No                                           |                   No                   | [NXP RC522 Datasheet][nxp_rc522_datasheet] |
| NTAG 21x (210-218)            | ISO14443A/106 kbit/s |                                      Support | Support                                      |          Not yet implemented           |                                            |
| Mifare Ultralight             | ISO14443A/106 kbit/s |                                      Support | Support                                      |          Not yet implemented           |                                            |
| Mifare Ultralight Ev1         | ISO14443A/106 kbit/s |                                      Support | Support                                      |          Not yet implemented           |                                            |
| Mifare Ultralight C           | ISO14443A/106 kbit/s |                                      Support | Support                                      |          Not yet implemented           |                                            |
| MifareClassic1K/2K/4K (4B/7B) | ISO14443A/106 kbit/s |                                      Support | Support                                      |                Support                 |                                            |
| Mifare DESFire                | ISO14443A High Rate  | Supports low rates, or possibly higher rates | Supports low rates, or possibly higher rates |          Not yet implemented           |                                            |
| Mifare DESFire EV1            | ISO14443A High rate  | Supports low rates, or possibly higher rates | Supports low rates, or possibly higher rates |          Not yet implemented           |                        Backward compatible |
| Mifare DESFire EV2            | ISO14443A High rate  | Supports low rates, or possibly higher rates | Supports low rates, or possibly higher rates |          Not yet implemented           |                                            |
| Mifare PLUS                   | ISO14443A High rate  | Supports low rates, or possibly higher rates | Supports low rates, or possibly higher rates |          Not yet implemented           |                                            |

## Low Frequency Attack

| Vulnerability Type | Tag Type  | Whether the hardware supports | Does the software support | Whether the application layer supports | Note |
|--------------------|:---------:|------------------------------:|---------------------------|:--------------------------------------:|-----:|
| Sniffing           |  125KHz   |                       Support | Support                   |          Not yet implemented           |      |
| Brute Force        | EM410x ID |                       Support | Support                   |          Not yet implemented           |      |

## Low Frequency Simulation

| Card Type                | Encoding Type | Whether the hardware supports | Does the software support | Whether the application layer supports |                                          Note |
|--------------------------|:-------------:|------------------------------:|---------------------------|:--------------------------------------:|----------------------------------------------:|
| Non <125KHz/ASK/PSK/FSK> |      No       |                            No | No                        |                   No                   | Only 125 khz RF, Modulation ASK, FSK and PSK. |
| EM410x                   |      ASK      |                       Support | Support                   |                Support                 |                   EM4100 is support(AD 64bit) |
| T5577                    |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| HID Prox                 |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Indala                   |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| FDX-B                    |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Paradox                  |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Keri                     |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| AWD                      |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| ioProx                   |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| securakey                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| gallagher                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| PAC/Stanley              |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Presco                   |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Visa2000                 |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Viking                   |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Noralsy                  |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| NexWatch                 |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Jablotron                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |

## Low Frequency Reader

| Card Type                | Encoding Type | Whether the hardware supports | Does the software support | Whether the application layer supports |                                          Note |
|--------------------------|:-------------:|------------------------------:|---------------------------|:--------------------------------------:|----------------------------------------------:|
| Non <125KHz/ASK/PSK/FSK> |      No       |                            No | No                        |                   No                   | Only 125 khz RF, Modulation ASK, FSK and PSK. |
| EM410x                   |      ASK      |                       Support | Support                   |                Support                 |                                               |
| T5577                    |      ASK      |                       Support | Support                   |             Support(Write)             |                                               |
| HID Prox                 |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Indala                   |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| FDX-B                    |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Paradox                  |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Keri                     |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| AWD                      |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| ioProx                   |      FSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| securakey                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| gallagher                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| PAC/Stanley              |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Presco                   |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Visa2000                 |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Viking                   |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Noralsy                  |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |
| NexWatch                 |      PSK      |                       Support | Support                   |          Not yet implemented           |                                               |
| Jablotron                |      ASK      |                       Support | Support                   |          Not yet implemented           |                                               |

## Low Frequency Modulation

[modulation_psk]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/PSK.png

[modulation_fsk]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/FSK.png

[modulation_ask]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/ASK.png

| Modulation Type |                         wav |
|-----------------|----------------------------:|
| PSK             | ![alt text][modulation_psk] |
| FSK             | ![alt text][modulation_fsk] |
| ASK             | ![alt text][modulation_ask] |

# 1. Ultra-low power consumption

It integrates a high-performance and low-power NFC module inside. When the NFC unit is turned on, the total current of
the chip is only 5mA@3.3V.
The underlying interaction is done independently by the NFC unit and does not occupy the CPU.
In addition, the 52840 itself is a high-performance low-power Bluetooth chip, and the encryption and calculation process
is only 7mA@3.3V. It can greatly reduce the battery volume and prolong the working time. That is to say, the 35mAh 10mm*
40mm button lithium battery can guarantee to be charged once every half a year under the working condition of swiping
the card 8 times a day for 3 seconds each time. Full potential for everyday use.

# 2. Not just UID, but a real and complete MIFARE encrypted data simulation

We can easily and completely simulate all data and password verification of all sectors, and can customize SAK, ATQA,
ATS, etc. Similar to an open CPU card development platform, 14A interaction of various architectures can be easily
realized.

# 3. Super compatibility with low-power locks using batteries

The structure of the old Chameleon AVR is slow to start during simulation. Faced with a battery-powered low-power lock
and an integrated lock on the door, it will be frequently interrupted, and the verification interaction cannot be
completed completely, resulting in no response when swiping the card.

In order to reduce power consumption, the battery lock will send out a field signal as short as possible when searching
for a card, which is no problem for the original card, but it is fatal for the MCU simulated card. Cards or mobile smart
bracelets simulated by the MCU cannot wake up and respond in such a short time, so many battery locks cannot open the
door, which greatly reduces the user experience.

This project specially optimizes the start-up and interaction logic and antenna for low-power reading heads. After
testing a variety of common low-power reading heads, they can open the door perfectly by swiping the card.

# 4. Ultra-fast response speed and low interaction delay(MifareClassic)

[fdt_standard_s50]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Standard_m1_s50.png

[fdt_redmi_k30]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Xiaomi_k30u_smartkey.png

[fdt_pm3_rdv401]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Proxmark3_Rdv4_RRG_(Firmware%20build%20at%2020201026).png

[fdt_chameleon_ultra]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/ChameleonUltra.png

[fdt_chameleon_tiny]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/ChameleonTiny.png

[fdt_flipper_zero]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/FlipperZero%20Simulation.png

| Simulation            |             FDT             |                                "**_FDT_**" Rating                                |
|----------------------|:---------------------------:|:--------------------------------------------------------------------------------:|
| Standard MIFARE Card |  ![alt][fdt_standard_s50]   | &#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50; |
| ChameleonUltra       | ![alt][fdt_chameleon_ultra] |         &#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;         |
| Proxmark3 Rdv4.01    |   ![alt][fdt_pm3_rdv401]    |                         &#x2B50;&#x2B50;&#x2B50;&#x2B50;                         |
| RedMi K30            |    ![alt][fdt_redmi_k30]    |                 &#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;                 |
| ChameleonTiny        | ![alt][fdt_chameleon_tiny]  |                     &#x2B50;&#x2B50;&#x2B50;&#x2B50;&#x2B50;                     |
| FlipperZero          |  ![alt][fdt_flipper_zero]   |                                 &#x2B50;&#x2B50;                                 |

# 5. 256kB super large RAM cooperates with RC522 to replace Proxmark3 magically to complete the decoding

[attack_mifare_darkside]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Darkside.png

[attack_mifare_nested]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Nested.png

[attack_mifare_mfkey32]: https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/MFKEY32V2.png

| Attack Type  |              CLI               |
|--------------|:------------------------------:|
| MFKEY32 V2   | ![alt][attack_mifare_mfkey32]  |
| Darkside     | ![alt][attack_mifare_darkside] |
| Nested       |  ![alt][attack_mifare_nested]  |
| StaticNested |          Coming Soon           |
| HardNested   |          Coming Soon           |
| Relay attack |          Coming Soon           |

# Hardware frame diagram:

![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Hardware%20%20Frame%20Diagram.png)

# Hardware Pictures

![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/resource/picture/Hardware%20Photos.png)

