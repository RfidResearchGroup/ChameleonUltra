# Bounded, complete LF ADC acquisitions

The SAADC delivers 2048 samples per completed DMA buffer. The previous generic
reader tried to enqueue that batch in a 512-element ring inside one ISR, dropping
the remainder before the main loop could drain it. An 800-byte result could
therefore splice unrelated batch prefixes.

The reader now copies directly into a bounded destination. Sampling is prepared
before a 90 ms timing reservation, enabled after a 2 ms field settling interval,
and stopped after the requested prefix or a finite 80 ms maximum wait. The wait
uses microseconds because the existing timing helper masks RTC1. Advertising is
paused with its prior active/idle ownership preserved; BLE connections are
rejected before entering the acquisition. This first path is USB-only.

The legacy 3009 response is still 800 samples. Command 3031 still asks for 4000;
both now reject incomplete acquisitions. A short sniff timeout may therefore
report an error instead of silently returning truncated data. The digital stream
comes from consecutive ADC batches; nominal sample rate is 125000 Hz, not a
calibrated measurement or proof of tag/protocol identity.

## Provisional command 3042

The numeric allocation requires maintainer review and does not replace 3032 or
the T55xx proposal's 3041. Its capabilities entry is present only on Ultra.

Request: `count:u16be`, 1 through 4000. Success status is `LF_TAG_OK`.

Response: `version:u8=1 | bits:u8=8 | count:u16be | nominal_rate_hz:u32be=125000 |
flags:u16be=3 | requested:u16be | samples[count]`.

Flag 1 denotes the direct, sequential DMA window. Flag 2 denotes completeness.
Unknown versions/flags or unequal counts must be rejected by hosts. No partial
or timeout response is a successful capture.

## Verification boundary

`test_lf_sample_window.c` exercises the real bounded accumulator with 2048-sample
bursts, batch boundaries, every target size 1–4000, signed conversion, saturation,
partial state, completed-window immutability and acquisition reset. The GCC/Clang
workflow uses address and undefined-behavior sanitizers. Full firmware compilation
does not replace real USB captures, LF card traces or clock/antenna measurements.
No RF acceptance is claimed until those measurements are recorded.
