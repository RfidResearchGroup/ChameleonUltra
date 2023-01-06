![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Header.png)

# ChameleonUltra

  Why not keep using ATxmega128?
First of all, it is difficult to buy chips because the lead time for the main chip is too long, and because the price has skyrocketed. Secondly, because the interaction speed of the ATxmega simulation is slow, the decryption performance of the READER mode cannot meet the needs, and the low-frequency function cannot be added, so we have been trying to upgrade it, such as using the latest ARM to replace the AVR framework, and the performance will definitely be greatly improved. 

# Why NRF52840?

NRF52840 has a built-in NFC Tag-A module, but no one seems to care about it. After playing with HydraNFC's TRF7970A and FlipperZero's ST25R3916, the developers found that they can only simulate MIFARE UID. I accidentally tested the NFC of 52840, and found that it is not only surprisingly easy to simulate a complete MIFARE card, but also has very good simulation performance, friendly data flow interaction, and very fast response, unlike the former which is limited by the SPI bus clock rate. We also found that it has ultra-low power consumption, ultra-small size, 256kb/1M large RAM and FLASH, also has BLE5.0 and USB2.0 FS, super CotexM4F, most importantly, he is very cheap! This is undoubtedly a treasure discovery for us! 

Below we will explain in detail how we exploited the performance of the NRF52840, and what seemingly impossible functions have been realized with it!

# Supported functions

High-frequency decoding: 
1）MFKEY32 V2 
2) Sniffing
3）Darkside
4）Nested
5）Staticnest
6）Hardnest
7）Relay attack

High frequency analog: 
1) ISO 14443
2) NFC
3) NXP MIFARE Classic
4) NXP MIFARE Plus 
5) Ultralight 
6) Ultralight C 
7) NTAG
8) DESFIRE 
9) DESFIRE EV1

Low frequency support: 
1) ASK
2) FSK
3) PSK
4) Card reader 
5) Simulation card
6) Brute Force
7) EM410X
8) T5577 
9) HID prox
10) Indala

# PSK
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/PSK.png)

# FSK
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/FSK.png)

# ASK
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/ASK.png)

# 1. Ultra-low power consumption

It integrates a high-performance and low-power NFC module inside. When the NFC unit is turned on, the total current of the chip is only 5mA@3.3V.
The underlying interaction is done independently by the NFC unit and does not occupy the CPU.
In addition, the 52840 itself is a high-performance low-power Bluetooth chip, and the encryption and calculation process is only 7mA@3.3V. It can greatly reduce the battery volume and prolong the working time. That is to say, the 35mAh 10mm*40mm button lithium battery can guarantee to be charged once every half a year under the working condition of swiping the card 8 times a day for 3 seconds each time. Full potential for everyday use.

# 2. Not just UID, but a real and complete MIFARE encrypted data simulation

We can easily and completely simulate all data and password verification of all sectors, and can customize SAK, ATQA, ATS, etc. Similar to an open CPU card development platform, 14A interaction of various architectures can be easily realized.

# 3. Super compatibility with low-power locks using batteries

The structure of the old Chameleon AVR is slow to start during simulation. Faced with a battery-powered low-power lock and an integrated lock on the door, it will be frequently interrupted, and the verification interaction cannot be completed completely, resulting in no response when swiping the card.

In order to reduce power consumption, the battery lock will send out a field signal as short as possible when searching for a card, which is no problem for the original card, but it is fatal for the MCU simulated card. Cards or mobile smart bracelets simulated by the MCU cannot wake up and respond in such a short time, so many battery locks cannot open the door, which greatly reduces the user experience.

This project specially optimizes the start-up and interaction logic and antenna for low-power reading heads. After testing a variety of common low-power reading heads, they can open the door perfectly by swiping the card.

# 4. Ultra-fast response speed and low interaction delay

# Standard MIFARE Card
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Standard_m1_s50.png)

# Proxmark3 Rdv4.01 Simulation
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Proxmark3_Rdv4_RRG_(Firmware%20build%20at%2020201026).png)

# Mi K30 Simulation
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Xiaomi_k30u_smartkey.png)

# ChameleonUltra Simulation
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/ChameleonUltra.png)

# ChameleonTiny Simulation
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/ChameleonTiny.png)

# FlipperZero Simulation
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/FlipperZero%20Simulation.png)

# 5. 256kB super large RAM cooperates with RC522 to replace Proxmark3 magically to complete the decoding

# Darkside decoding function
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Darkside.png)

# Nested decoding function
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Nested.png)

# MFKEY32 V2 decoding function
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Nested.png) 

# Staticnest decoding function
Coming Soon

# Hardnest decoding function
Coming Soon 

# Hardware frame diagram:
![alt text](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/Photos/Hardware%20%20Frame%20Diagram.png)

# Hardware Pictures
Coming Soon

