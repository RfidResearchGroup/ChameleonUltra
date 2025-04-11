import enum


@enum.unique
class Command(enum.IntEnum):
    GET_APP_VERSION = 1000
    CHANGE_DEVICE_MODE = 1001
    GET_DEVICE_MODE = 1002
    SET_ACTIVE_SLOT = 1003
    SET_SLOT_TAG_TYPE = 1004
    SET_SLOT_DATA_DEFAULT = 1005
    SET_SLOT_ENABLE = 1006

    SET_SLOT_TAG_NICK = 1007
    GET_SLOT_TAG_NICK = 1008

    SLOT_DATA_CONFIG_SAVE = 1009

    ENTER_BOOTLOADER = 1010
    GET_DEVICE_CHIP_ID = 1011
    GET_DEVICE_ADDRESS = 1012

    SAVE_SETTINGS = 1013
    RESET_SETTINGS = 1014
    SET_ANIMATION_MODE = 1015
    GET_ANIMATION_MODE = 1016

    GET_GIT_VERSION = 1017

    GET_ACTIVE_SLOT = 1018
    GET_SLOT_INFO = 1019

    WIPE_FDS = 1020

    DELETE_SLOT_TAG_NICK = 1021

    GET_ENABLED_SLOTS = 1023
    DELETE_SLOT_SENSE_TYPE = 1024

    GET_BATTERY_INFO = 1025

    GET_BUTTON_PRESS_CONFIG = 1026
    SET_BUTTON_PRESS_CONFIG = 1027

    GET_LONG_BUTTON_PRESS_CONFIG = 1028
    SET_LONG_BUTTON_PRESS_CONFIG = 1029

    SET_BLE_PAIRING_KEY = 1030
    GET_BLE_PAIRING_KEY = 1031
    DELETE_ALL_BLE_BONDS = 1032

    GET_DEVICE_MODEL = 1033
    # FIXME: implemented but unused in CLI commands
    GET_DEVICE_SETTINGS = 1034
    GET_DEVICE_CAPABILITIES = 1035
    GET_BLE_PAIRING_ENABLE = 1036
    SET_BLE_PAIRING_ENABLE = 1037

    HF14A_SCAN = 2000
    MF1_DETECT_SUPPORT = 2001
    MF1_DETECT_PRNG = 2002
    MF1_STATIC_NESTED_ACQUIRE = 2003
    MF1_DARKSIDE_ACQUIRE = 2004
    MF1_DETECT_NT_DIST = 2005
    MF1_NESTED_ACQUIRE = 2006
    MF1_AUTH_ONE_KEY_BLOCK = 2007
    MF1_READ_ONE_BLOCK = 2008
    MF1_WRITE_ONE_BLOCK = 2009
    HF14A_RAW = 2010
    MF1_MANIPULATE_VALUE_BLOCK = 2011
    MF1_CHECK_KEYS_OF_SECTORS = 2012
    DATA_CMD_MF1_HARDNESTED_ACQUIRE = 2013

    EM410X_SCAN = 3000
    EM410X_WRITE_TO_T55XX = 3001

    MF1_WRITE_EMU_BLOCK_DATA = 4000
    HF14A_SET_ANTI_COLL_DATA = 4001
    MF1_SET_DETECTION_ENABLE = 4004
    MF1_GET_DETECTION_COUNT = 4005
    MF1_GET_DETECTION_LOG = 4006
    # FIXME: not implemented
    MF1_GET_DETECTION_ENABLE = 4007
    MF1_READ_EMU_BLOCK_DATA = 4008
    MF1_GET_EMULATOR_CONFIG = 4009
    # FIXME: not implemented
    MF1_GET_GEN1A_MODE = 4010
    MF1_SET_GEN1A_MODE = 4011
    # FIXME: not implemented
    MF1_GET_GEN2_MODE = 4012
    MF1_SET_GEN2_MODE = 4013
    # FIXME: not implemented
    MF1_GET_BLOCK_ANTI_COLL_MODE = 4014
    MF1_SET_BLOCK_ANTI_COLL_MODE = 4015
    # FIXME: not implemented
    MF1_GET_WRITE_MODE = 4016
    MF1_SET_WRITE_MODE = 4017
    HF14A_GET_ANTI_COLL_DATA = 4018
    MF0_NTAG_GET_UID_MAGIC_MODE = 4019
    MF0_NTAG_SET_UID_MAGIC_MODE = 4020
    MF0_NTAG_READ_EMU_PAGE_DATA = 4021
    MF0_NTAG_WRITE_EMU_PAGE_DATA = 4022
    MF0_NTAG_GET_VERSION_DATA = 4023
    MF0_NTAG_SET_VERSION_DATA = 4024
    MF0_NTAG_GET_SIGNATURE_DATA = 4025
    MF0_NTAG_SET_SIGNATURE_DATA = 4026
    MF0_NTAG_GET_COUNTER_DATA = 4027
    MF0_NTAG_SET_COUNTER_DATA = 4028
    MF0_NTAG_RESET_AUTH_CNT = 4029
    MF0_NTAG_GET_PAGE_COUNT = 4030
    MF0_NTAG_GET_WRITE_MODE = 4031
    MF0_NTAG_SET_WRITE_MODE = 4032

    EM410X_SET_EMU_ID = 5000
    EM410X_GET_EMU_ID = 5001


@enum.unique
class Status(enum.IntEnum):
    HF_TAG_OK = 0x00     # IC card operation is successful
    HF_TAG_NO = 0x01     # IC card not found
    HF_ERR_STAT = 0x02    # Abnormal IC card communication
    HF_ERR_CRC = 0x03     # IC card communication verification abnormal
    HF_COLLISION = 0x04  # IC card conflict
    HF_ERR_BCC = 0x05     # IC card BCC error
    MF_ERR_AUTH = 0x06    # MF card verification failed
    HF_ERR_PARITY = 0x07  # IC card parity error
    HF_ERR_ATS = 0x08     # ATS should be present but card NAKed, or ATS too large

    # Some operations with low frequency cards succeeded!
    LF_TAG_OK = 0x40
    # Unable to search for a valid EM410X label
    EM410X_TAG_NO_FOUND = 0x41

    # The parameters passed by the BLE instruction are wrong, or the parameters passed
    # by calling some functions are wrong
    PAR_ERR = 0x60
    # The mode of the current device is wrong, and the corresponding API cannot be called
    DEVICE_MODE_ERROR = 0x66
    INVALID_CMD = 0x67
    SUCCESS = 0x68
    NOT_IMPLEMENTED = 0x69
    FLASH_WRITE_FAIL = 0x70
    FLASH_READ_FAIL = 0x71
    INVALID_SLOT_TYPE = 0x72

    def __str__(self):
        if self == Status.HF_TAG_OK:
            return "HF tag operation succeeded"
        elif self == Status.HF_TAG_NO:
            return "HF tag no found or lost"
        elif self == Status.HF_ERR_STAT:
            return "HF tag status error"
        elif self == Status.HF_ERR_CRC:
            return "HF tag data crc error"
        elif self == Status.HF_COLLISION:
            return "HF tag collision"
        elif self == Status.HF_ERR_BCC:
            return "HF tag uid bcc error"
        elif self == Status.MF_ERR_AUTH:
            return "HF tag auth fail"
        elif self == Status.HF_ERR_PARITY:
            return "HF tag data parity error"
        elif self == Status.HF_ERR_ATS:
            return "HF tag was supposed to send ATS but didn't"
        elif self == Status.LF_TAG_OK:
            return "LF tag operation succeeded"
        elif self == Status.EM410X_TAG_NO_FOUND:
            return "EM410x tag no found"
        elif self == Status.PAR_ERR:
            return "API request fail, param error"
        elif self == Status.DEVICE_MODE_ERROR:
            return "API request fail, device mode error"
        elif self == Status.INVALID_CMD:
            return "API request fail, cmd invalid"
        elif self == Status.SUCCESS:
            return "Device operation succeeded"
        elif self == Status.NOT_IMPLEMENTED:
            return "Some api not implemented"
        elif self == Status.FLASH_WRITE_FAIL:
            return "Flash write failed"
        elif self == Status.FLASH_READ_FAIL:
            return "Flash read failed"
        elif self == Status.INVALID_SLOT_TYPE:
            return "Invalid card type in slot"
        return "Invalid status"


@enum.unique
class SlotNumber(enum.IntEnum):
    SLOT_1 = 1
    SLOT_2 = 2
    SLOT_3 = 3
    SLOT_4 = 4
    SLOT_5 = 5
    SLOT_6 = 6
    SLOT_7 = 7
    SLOT_8 = 8

    @staticmethod
    def to_fw(index: int):  # can be int or SlotNumber
        # SlotNumber() will raise error for us if index not in slot range
        return SlotNumber(index).value - 1

    @staticmethod
    def from_fw(index: int):
        # SlotNumber() will raise error for us if index not in fw range
        return SlotNumber(index + 1)


@enum.unique
class TagSenseType(enum.IntEnum):
    # Unknown
    UNDEFINED = 0
    # 125 kHz
    LF = 1
    # 13.56 MHz
    HF = 2


@enum.unique
class TagSpecificType(enum.IntEnum):
    UNDEFINED = 0

    # old HL/LF common types, slots using these ones need to be migrated first
    OLD_EM410X = 1
    OLD_MIFARE_Mini = 2
    OLD_MIFARE_1024 = 3
    OLD_MIFARE_2048 = 4
    OLD_MIFARE_4096 = 5
    OLD_NTAG_213 = 6
    OLD_NTAG_215 = 7
    OLD_NTAG_216 = 8
    OLD_TAG_TYPES_END = 9

    # LF

    # ASK Tag-Talk-First      100
    # EM410x
    EM410X = 100
    # FDX-B
    # securakey
    # gallagher
    # PAC/Stanley
    # Presco
    # Visa2000
    # Viking
    # Noralsy
    # Jablotron

    # FSK Tag-Talk-First      200
    # HID Prox
    # ioProx
    # AWID
    # Paradox

    # PSK Tag-Talk-First      300
    # Indala
    # Keri
    # NexWatch

    # Reader-Talk-First       400
    # T5577
    # EM4x05/4x69
    # EM4x50/4x70
    # Hitag series

    TAG_TYPES_LF_END = 999

    # HF

    # MIFARE Classic series  1000
    MIFARE_Mini = 1000
    MIFARE_1024 = 1001
    MIFARE_2048 = 1002
    MIFARE_4096 = 1003
    # MFUL / NTAG series     1100
    NTAG_213 = 1100
    NTAG_215 = 1101
    NTAG_216 = 1102
    MF0ICU1 = 1103
    MF0ICU2 = 1104
    MF0UL11 = 1105
    MF0UL21 = 1106
    NTAG_210 = 1107
    NTAG_212 = 1108
    # MIFARE Plus series     1200
    # DESFire series         1300

    # ST25TA series          2000

    # HF14A-4 series         3000

    @staticmethod
    def list(exclude_meta=True):
        return [t for t in TagSpecificType
                if (t > TagSpecificType.OLD_TAG_TYPES_END and
                    t != TagSpecificType.TAG_TYPES_LF_END)
                or not exclude_meta]

    @staticmethod
    def list_hf():
        return [t for t in TagSpecificType.list()
                if (t > TagSpecificType.TAG_TYPES_LF_END)]

    @staticmethod
    def list_lf():
        return [t for t in TagSpecificType.list()
                if (TagSpecificType.UNDEFINED < t < TagSpecificType.TAG_TYPES_LF_END)]

    def __str__(self):
        if self == TagSpecificType.UNDEFINED:
            return "Undefined"
        elif self == TagSpecificType.EM410X:
            return "EM410X"
        elif self == TagSpecificType.MIFARE_Mini:
            return "Mifare Mini"
        elif self == TagSpecificType.MIFARE_1024:
            return "Mifare Classic 1k"
        elif self == TagSpecificType.MIFARE_2048:
            return "Mifare Classic 2k"
        elif self == TagSpecificType.MIFARE_4096:
            return "Mifare Classic 4k"
        elif self == TagSpecificType.NTAG_213:
            return "NTAG 213"
        elif self == TagSpecificType.NTAG_215:
            return "NTAG 215"
        elif self == TagSpecificType.NTAG_216:
            return "NTAG 216"
        elif self == TagSpecificType.MF0ICU1:
            return "Mifare Ultralight"
        elif self == TagSpecificType.MF0ICU2:
            return "Mifare Ultralight C"
        elif self == TagSpecificType.MF0UL11:
            return "Mifare Ultralight EV1 (640 bit)"
        elif self == TagSpecificType.MF0UL21:
            return "Mifare Ultralight EV1 (1312 bit)"
        elif self == TagSpecificType.NTAG_210:
            return "NTAG 210"
        elif self == TagSpecificType.NTAG_212:
            return "NTAG 212"
        elif self < TagSpecificType.OLD_TAG_TYPES_END:
            return "Old tag type, must be migrated! Upgrade fw!"
        return "Invalid"


@enum.unique
class MifareClassicWriteMode(enum.IntEnum):
    # Normal write
    NORMAL = 0
    # Send NACK to write attempts
    DENIED = 1
    # Acknowledge writes, but don't remember contents
    DECEIVE = 2
    # Store data to RAM, but not to ROM
    SHADOW = 3
    # Shadow requested, will be changed to SHADOW and stored to ROM
    SHADOW_REQ = 4

    @staticmethod
    def list(exclude_meta=True):
        return [m for m in MifareClassicWriteMode
                if m != MifareClassicWriteMode.SHADOW_REQ
                or not exclude_meta]

    def __str__(self):
        if self == MifareClassicWriteMode.NORMAL:
            return "Normal"
        elif self == MifareClassicWriteMode.DENIED:
            return "Denied"
        elif self == MifareClassicWriteMode.DECEIVE:
            return "Deceive"
        elif self == MifareClassicWriteMode.SHADOW:
            return "Shadow"
        elif self == MifareClassicWriteMode.SHADOW_REQ:
            return "Shadow requested"
        return "None"

@enum.unique
class MifareUltralightWriteMode(enum.IntEnum):
    # Normal write
    NORMAL = 0
    # Send NACK to write attempts
    DENIED = 1
    # Acknowledge writes, but don't remember contents
    DECEIVE = 2
    # Store data to RAM, but not to ROM
    SHADOW = 3
    # Shadow requested, will be changed to SHADOW and stored to ROM
    SHADOW_REQ = 4

    @staticmethod
    def list(exclude_meta=True):
        return [m for m in MifareUltralightWriteMode
                if m != MifareUltralightWriteMode.SHADOW_REQ
                or not exclude_meta]

    def __str__(self):
        if self == MifareUltralightWriteMode.NORMAL:
            return "Normal"
        elif self == MifareUltralightWriteMode.DENIED:
            return "Denied"
        elif self == MifareUltralightWriteMode.DECEIVE:
            return "Deceive"
        elif self == MifareUltralightWriteMode.SHADOW:
            return "Shadow"
        elif self == MifareUltralightWriteMode.SHADOW_REQ:
            return "Shadow requested"
        return "None"

@enum.unique
class MifareClassicPrngType(enum.IntEnum):
    # the random number of the card response is fixed
    STATIC = 0
    # the random number of the card response is weak
    WEAK = 1
    # the random number of the card response is unpredictable
    HARD = 2

    def __str__(self):
        if self == MifareClassicPrngType.STATIC:
            return "Static"
        elif self == MifareClassicPrngType.WEAK:
            return "Weak"
        elif self == MifareClassicPrngType.HARD:
            return "Hard"
        return "None"


@enum.unique
class MifareClassicDarksideStatus(enum.IntEnum):
    OK = 0
    # Darkside can't fix NT (PRNG is unpredictable)
    CANT_FIX_NT = 1
    # Darkside try to recover a default key
    LUCKY_AUTH_OK = 2
    # Darkside can't get tag response enc(nak)
    NO_NAK_SENT = 3
    # Darkside running, can't change tag
    TAG_CHANGED = 4

    def __str__(self):
        if self == MifareClassicDarksideStatus.OK:
            return "Success"
        elif self == MifareClassicDarksideStatus.CANT_FIX_NT:
            return "Cannot fix NT (unpredictable PRNG)"
        elif self == MifareClassicDarksideStatus.LUCKY_AUTH_OK:
            return "Try to recover a default key"
        elif self == MifareClassicDarksideStatus.NO_NAK_SENT:
            return "Cannot get tag response enc(nak)"
        elif self == MifareClassicDarksideStatus.TAG_CHANGED:
            return "Tag changed during attack"
        return "None"


@enum.unique
class AnimationMode(enum.IntEnum):
    FULL = 0
    MINIMAL = 1
    NONE = 2

    def __str__(self):
        if self == AnimationMode.FULL:
            return "Full animation"
        elif self == AnimationMode.MINIMAL:
            return "Minimal animation"
        elif self == AnimationMode.NONE:
            return "No animation"


@enum.unique
class ButtonType(enum.IntEnum):
    A = ord('A')
    B = ord('B')


@enum.unique
class MfcKeyType(enum.IntEnum):
    A = 0x60
    B = 0x61


@enum.unique
class ButtonPressFunction(enum.IntEnum):
    NONE = 0
    NEXTSLOT = 1
    PREVSLOT = 2
    CLONE = 3
    BATTERY = 4

    def __str__(self):
        if self == ButtonPressFunction.NONE:
            return "No Function"
        elif self == ButtonPressFunction.NEXTSLOT:
            return "Select next slot"
        elif self == ButtonPressFunction.PREVSLOT:
            return "Select previous slot"
        elif self == ButtonPressFunction.CLONE:
            return "Read then simulate the ID/UID card number"
        elif self == ButtonPressFunction.BATTERY:
            return "Show Battery Level"
        return "None"

@enum.unique
class MfcValueBlockOperator(enum.IntEnum):
    DECREMENT = 0xC0
    INCREMENT = 0xC1
    RESTORE = 0xC2
