# InvalidException: Device unsupported cmd

You need to update the firmware of you Chameleon.

# Communication issues between CLI and Chameleon

For example, typical error when performing DFU upgrade:

```
[00:00:00] ------   0% [id:9] Failed, [sdfu] [json.exception.type_error.302] type must be string, but is null
Error: One or more program tasks failed
```

Check the serial port permissions and if under Linux, make sure ModemManager is not interfering with your Chameleon.
The proposed [udev/rules.d file](../resource/driver/79-chameleon-usb-device-blacklist-dialout.rules) may help you (and add your user to the dialout group).

# Difficulties to get the LF working properly

The LF antenna is on a second PCB attached to the main PCB via little screws which also serve as electric connection.
It has reported that on some devices the electric connection is not good, some glue or resin residues mai interfere.
You can try to dismantle very gently the screws and PCB, clean them and put them back in place.
Be very careful the screws have been reported to be quite fragile so be gentle with them!
