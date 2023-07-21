#!/usr/bin/env python3

import asyncio
from bleak import BleakClient
from bleak import BleakScanner

UUID_NORDIC_TX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UUID_NORDIC_RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
DFUCMD=b'\x11\xef\x03\xf2\x00\x00\x00\x00\x0b\x00'

async def main():
    print("Searching ChameleonUltra...")
    device = await BleakScanner.find_device_by_name("ChameleonUltra")
    if device is None:
        print("Could not find ChameleonUltra")
        return
    print("Found!")
    print("Sending DFU command")
    async with BleakClient(device) as client:
        c=DFUCMD
        await client.write_gatt_char(UUID_NORDIC_TX, bytearray(DFUCMD), False)
        print("Done!")

asyncio.run(main())
