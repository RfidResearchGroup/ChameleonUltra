# Chameleon Ultra LF Implementation Research

## Hardware Capabilities (from Technical Whitepaper)

### Core Hardware
- **Chip**: nRF52840 with built-in NFC Tag-A module
- **LF Support**: 125kHz RF with ASK/FSK/PSK modulations
- **Performance**: CortexM4F, 256kb/1M RAM/Flash, BLE5.0, USB2.0 FS
- **Advantages**: Ultra-low power, ultra-small size, very cheap, fast response

### LF Card Support Status

| Card Type | Encoding | Hardware Support | Software Support | Application Layer | Notes |
|-----------|----------|------------------|------------------|-------------------|-------|
| EM410x | ASK | ✅ Support | ✅ Support | ✅ Support | EM4100 supported (64bit) |
| T5577 | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| EM4305 | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| HID Prox | FSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Indala | PSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| FDX-B | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Paradox | FSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Keri | PSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| AWD | FSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| ioProx | FSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| securakey | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| gallagher | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| PAC/Stanley | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Presco | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Visa2000 | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Viking | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Noralsy | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |
| NexWatch | PSK | ✅ Support | ✅ Support | ❌ Not implemented | |
| Jablotron | ASK | ✅ Support | ✅ Support | ❌ Not implemented | |

### Key Findings
1. **Hardware is fully capable**: 125kHz RF with all required modulations (ASK/FSK/PSK)
2. **Firmware support exists**: Software layer supports most LF protocols
3. **Missing application layer**: CLI/GUI commands not implemented for most LF cards
4. **Only EM410x fully supported**: Complete stack from hardware to application layer

### LF Attack Capabilities
| Attack Type | Tag Type | Hardware | Software | Application | Notes |
|-------------|----------|----------|----------|-------------|-------|
| Sniffing | 125KHz | ✅ Support | ✅ Support | ❌ Not implemented | |
| Brute Force | EM410x ID | ✅ Support | ✅ Support | ❌ Not implemented | |

### Emulation Support
- T5577 write support exists in software but not exposed in application layer
- Most LF cards have complete hardware/software support but lack CLI commands



## CLI Architecture (from CLI Documentation)

### Current CLI Structure
- **Language**: Python 3.9+ required
- **Main Entry**: `chameleon_cli_main.py` in `software/script/` directory
- **Installation**: Multiple options (ProxSpace, WSL, native build)
- **Dependencies**: cmake, build tools, Python virtual environment

### Command Structure Examples
```bash
# Hardware connection
hw connect
hw slot list
hw slot type -s 8 -t MIFARE_1024
hw slot init -s 8 -t MIFARE_1024
hw slot enable -s 8 --hf
hw slot change -s 8

# High Frequency (HF) commands
hf mf econfig --enable-log
hf mf elog
hf mf elog --decrypt
hf mf econfig --disable-log
```

### Key Observations
1. **Command hierarchy**: `hw` for hardware, `hf` for high frequency
2. **Missing LF commands**: No `lf` command group visible
3. **Slot-based system**: Cards are managed in numbered slots
4. **Configuration system**: Enable/disable features per slot
5. **Log system**: Attack logging and decryption capabilities

### Implementation Requirements
- Need to add `lf` command group parallel to `hf`
- Implement slot management for LF cards
- Add protocol-specific subcommands (em410x, t5577, hid, etc.)
- Integrate with existing hardware connection system


## Protocol Structure (from Protocol Documentation)

### Frame Format
```
SOF (1 byte) | LRC1 (1 byte) | CMD (2 bytes) | STATUS (2 bytes) | LEN (2 bytes) | LRC2 (1 byte) | DATA (LEN bytes) | LRC3 (1 byte)
```

### Key Protocol Details
- **SOF**: Start-of-Frame = 0x11
- **LRC1**: LRC over SOF = 0xEF  
- **CMD**: 2-byte command number (Big Endian)
- **STATUS**: 0x0000 from client, result code from firmware
- **LEN**: Data length (max 512 bytes)
- **LRC**: Longitudinal Redundancy Check (8-bit two's complement)

### Command Number Ranges
- **1000-1999**: General hardware commands
- **2000-2999**: High Frequency (HF) commands  
- **3000-3999**: Low Frequency (LF) commands
- **4000+**: Emulation commands

### Status Codes
- `STATUS_SUCCESS`: General commands
- `STATUS_HF_TAG_OK`: HF commands
- `STATUS_LF_TAG_OK`: LF commands

### Discovered LF Commands
- **3000**: `EM410X_SCAN` - Scan for EM410x cards
- **3001**: `EM410X_WRITE_TO_T55XX` - Write EM410x data to T55xx

### Slot Management
- Slots numbered 0-7 in protocol (1-8 in CLI)
- Each slot can have HF and LF tag types
- Slots can be enabled/disabled per frequency band
- Tag types defined in `tag_specific_type_t` enum


### LF Command Specifications Found

#### 3000: EM410X_SCAN
- **Command**: no data
- **Response**: 5 bytes - `id[5]` (ID as 5 bytes)
- **CLI**: `lf em 410x read`

#### 3001: EM410X_WRITE_TO_T55XX  
- **Command**: 9+N*4 bytes - `id[5]|new_key[4]|old_key1[4]|old_key2[4]|...` (N>=1)
- **Response**: no data
- **CLI**: `lf em 410x write`

#### 5000: EM410X_SET_EMU_ID
- **Command**: 5 bytes - `id[5]` (ID as 5 bytes)
- **Response**: no data  
- **CLI**: `lf em 410x econfig`

#### 5001: EM410X_GET_EMU_ID
- **Command**: no data
- **Response**: 5 bytes - `id[5]` (ID as 5 bytes)
- **CLI**: `lf em 410x econfig`

### Key Insights
1. **LF commands exist**: 3000-3999 range for reader mode, 5000+ for emulation
2. **EM410x support**: Scan, write to T55xx, and emulation configuration
3. **CLI mapping**: Commands map to `lf em 410x` subcommands
4. **Data format**: 5-byte ID format for EM410x cards
5. **T55xx writing**: Supports multiple keys for authentication

### Missing LF Protocols
Based on the technical whitepaper, these protocols have firmware support but no CLI:
- T5577 (general read/write)
- HID Prox (FSK)
- Indala (PSK) 
- FDX-B, Paradox, Keri, AWD, ioProx, etc.


## Current Codebase Structure (from GitHub examination)

### File Structure
```
software/script/
├── chameleon_cli_main.py      # Main CLI entry point
├── chameleon_cli_unit.py      # CLI command definitions (3609 lines)
├── chameleon_cmd.py           # Command protocol layer
├── chameleon_com.py           # Communication layer
├── chameleon_enum.py          # Enumerations and constants
├── chameleon_utils.py         # Utility functions
├── hardnested_utils.py        # Hardnested attack utilities
└── requirements.txt           # Python dependencies
```

### Key Findings
1. **CLI Architecture**: Uses CLITree structure from chameleon_utils
2. **Command Classes**: BaseCLIUnit, DeviceRequiredUnit, ReaderRequiredUnit
3. **Large File**: chameleon_cli_unit.py is 3609 lines with all command definitions
4. **Command Structure**: Uses ArgumentParserNoExit for command parsing
5. **Device Communication**: chameleon_com.ChameleonCom handles protocol
6. **Protocol Layer**: chameleon_cmd.ChameleonCMD wraps protocol commands

### Missing LF Implementation
- No visible LF command group in current structure
- Only HF commands appear to be implemented
- Need to examine the complete command tree structure
- LF commands need to be added parallel to HF structure

### Next Steps
1. Download and examine the complete CLI unit file
2. Understand the CLITree structure
3. Identify where to add LF command group
4. Map LF protocol commands to CLI commands

