#!/usr/bin/env python3

import serial
import serial.tools.list_ports as list_ports

DFUCMD = b'\x11\xef\x03\xf2\x00\x00\x00\x00\x0b\x00'

port = None
for comport in list_ports.comports():
    if comport.vid == 0x1915 and comport.pid == 0x521f:
        print("Chameleon already in DFU mode")
        exit(0)
    if comport.vid == 0x6868 and comport.pid == 0x8686:
        port = comport.device
        break

if port is None:
    print("Chameleon not found")
    exit(1)

try:
    serial_instance = serial.Serial(port=port, baudrate=115200)
    serial_instance.dtr = 1  # must make dtr enable
    serial_instance.timeout = 0  # noblock
    serial_instance.write(DFUCMD)
except Exception as e:
    print(f"Serial Error {e}.")
    exit(1)
finally:
    serial_instance.close()
