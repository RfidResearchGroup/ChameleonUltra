# Protocol description

**WIP**

## Packets format

The communication with the application is not the easiest but is structured as follows:

`MAGIC BYTE(0x11) LRC(Magic Byte) COMMAND STATUS(0x00) DATA LRC(COMMAND + STATUS + DATA)`

You build the Packet by first adding 0x11, this is the "Magic Byte" to say that there is something coming. This is followed by the LRC ([**L**ongitudinal **R**edundancy **C**heck](https://en.wikipedia.org/wiki/Longitudinal_redundancy_check)) of the "Magic Byte". Then you put in the command in [Big Endian](https://en.wikipedia.org/wiki/Endianness). Each command gets assigned a unique number (e.g. `factoryReset(1020)`), this is what you are sending to the device. Append the status, also in Big Endian. The status is always 0x00. Then you add your Data, this could be anything, for example sending the card keys when reading a block.

For receiving, it is the exact same in reverse.

## Packet payloads

Each command and response have their own payload formats.

TODO:
