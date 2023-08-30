# Protocol description

**WIP**

## Packets format

The communication with the application is not the easiest but is structured as follows:

![](images/protocol-packet.png)

- **SOF**: `1 Byte`, the "Magic Byte" represent the start of a packet, must be `0x11`.
- **LRC1**: `1 Byte`, the LRC ([**L**ongitudinal **R**edundancy **C**heck](https://en.wikipedia.org/wiki/Longitudinal_redundancy_check)) of the `SOF`, must be `0xEF`.
- **CMD**: `2 Bytes` in unsigned [Big Endian](https://en.wikipedia.org/wiki/Endianness) format, each command have been assigned a unique number (e.g. `factoryReset(1020)`), this is what you are sending to the device.
- **STATUS**: `2 Bytes` in unsigned [Big Endian](https://en.wikipedia.org/wiki/Endianness) format. If the direction is from APP to hardware, the status is always `0x0000`. If the direction is from hardware to APP, the status is the result of the command.
- **LEN**: `2 Bytes` in unsigned [Big Endian](https://en.wikipedia.org/wiki/Endianness) format, the length of the data, maximum is `512`.
- **LRC2**: `1 Byte`, the LRC ([**L**ongitudinal **R**edundancy **C**heck](https://en.wikipedia.org/wiki/Longitudinal_redundancy_check)) of the `CMD`, `STATUS` and `LEN`.
- **DATA**: `LEN Bytes`, the data to send or receive, maximum is `512 Bytes`. This could be anything, for example you should sending key type, block number, and the card keys when reading a block.
- **LRC3**: `1 Byte`, the LRC ([**L**ongitudinal **R**edundancy **C**heck](https://en.wikipedia.org/wiki/Longitudinal_redundancy_check)) of the `DATA`.

The total length of the packet is `LEN + 10` Bytes. For receiving, it is the exact same format.

## Packet payloads

Each command and response have their own payload formats.

TODO:
