# MTools BLE Introduction  
MTools BLE supports managing the ChamleonUltra, ChameleonLite and DevKits via BLE connections. 
## Downlaod Link 
- [MTools BLE on iOS](https://apps.apple.com/app/mtools-ble-rfid-reader/id1531345398) 
- [MTools BLE on Google Play](https://play.google.com/store/apps/details?id=com.mtoolstec.mtoolsLite) 

## How to connect with Bluetooth in MTools BLE
1. Click **A** or **B** button to power on.
2. Click **Bluetooth List** icon in App to search devices.
3. Click **Connect** button on the right to connect.

#### Notice for Bluetooth Connection
1. Grant the Bluetooth permission of App on iOS.
2. Allow Location permission to scan Bluetooth devices on Android.

## Features for ChameleonUltra 
### Slot Manager  
1. Fetch all slot status.
2. Enable or disable Slots. 
3. Change LF and HF Slot name. 
4. Set LF and HF Tag Type.
5. Delete and reset all slots. 

### Reader  
1. Fast read LF and HF Tag. 
2. Simulate Mifare Classic Tag with UID, SAK, ATQA and empty dump. 
3. Simulate Mifare Ultralight Tag with UID, SAK, ATQA and empty dump.
4. Simulate EM410X LF tag or manually set the ID then simulate. 

### Mifare Classic Dump 
1. eRead full dump from current active slot to App. 
2. Upload full dump to current active slot and simulate.
3. Read Mifare Mini, 1K, 2K, 4K dump from tag with known keys.
4. Write Gen1A, Gen2, Gen3, Gen4 dump to tag with known keys.
5. Format common and magic Mifare Classic tags.
6. Modify block data and save to new dump file. 

### Mifare Ultralight Dump 
1. eRead full dump from current active slot to App.
2. Upload full dump to current active slot and simulate.
3. Read Mifare Ultralight dump from tag.
4. Write Mifare Ultralight dump to tag.

### Settings
1. Set the Animation of LEDs. 
2. Set press and long press button of A and B. 
3. Set the **Mifare Classic Emulation** of current slot. 
4. Set the **Mifare Ultralight Emulation** of current slot.
5. DFU Tool for updating firmware.
6. Reset Chameleon Device.