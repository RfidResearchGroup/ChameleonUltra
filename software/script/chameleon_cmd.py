import enum
import struct
import ctypes

import chameleon_com
import chameleon_status
from chameleon_utils import expect_response

CURRENT_VERSION_SETTINGS = 5

DATA_CMD_GET_APP_VERSION = 1000
DATA_CMD_CHANGE_DEVICE_MODE = 1001
DATA_CMD_GET_DEVICE_MODE = 1002
DATA_CMD_SET_ACTIVE_SLOT = 1003
DATA_CMD_SET_SLOT_TAG_TYPE = 1004
DATA_CMD_SET_SLOT_DATA_DEFAULT = 1005
DATA_CMD_SET_SLOT_ENABLE = 1006

DATA_CMD_SET_SLOT_TAG_NICK = 1007
DATA_CMD_GET_SLOT_TAG_NICK = 1008

DATA_CMD_SLOT_DATA_CONFIG_SAVE = 1009

DATA_CMD_ENTER_BOOTLOADER = 1010
DATA_CMD_GET_DEVICE_CHIP_ID = 1011
DATA_CMD_GET_DEVICE_ADDRESS = 1012

DATA_CMD_SAVE_SETTINGS = 1013
DATA_CMD_RESET_SETTINGS = 1014
DATA_CMD_SET_ANIMATION_MODE = 1015
DATA_CMD_GET_ANIMATION_MODE = 1016

DATA_CMD_GET_GIT_VERSION = 1017

DATA_CMD_GET_ACTIVE_SLOT = 1018
DATA_CMD_GET_SLOT_INFO = 1019

DATA_CMD_WIPE_FDS = 1020

DATA_CMD_GET_ENABLED_SLOTS = 1023
DATA_CMD_DELETE_SLOT_SENSE_TYPE = 1024

DATA_CMD_GET_BATTERY_INFO = 1025

DATA_CMD_GET_BUTTON_PRESS_CONFIG = 1026
DATA_CMD_SET_BUTTON_PRESS_CONFIG = 1027

DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG = 1028
DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG = 1029

DATA_CMD_SET_BLE_PAIRING_KEY = 1030
DATA_CMD_GET_BLE_PAIRING_KEY = 1031
DATA_CMD_DELETE_ALL_BLE_BONDS = 1032

DATA_CMD_GET_DEVICE_MODEL = 1033
# FIXME: implemented but unused in CLI commands
DATA_CMD_GET_DEVICE_SETTINGS = 1034
DATA_CMD_GET_DEVICE_CAPABILITIES = 1035
DATA_CMD_GET_BLE_PAIRING_ENABLE = 1036
DATA_CMD_SET_BLE_PAIRING_ENABLE = 1037

DATA_CMD_HF14A_SCAN = 2000
DATA_CMD_MF1_DETECT_SUPPORT = 2001
DATA_CMD_MF1_DETECT_PRNG = 2002
DATA_CMD_MF1_STATIC_NESTED_ACQUIRE = 2003
DATA_CMD_MF1_DARKSIDE_ACQUIRE = 2004
DATA_CMD_MF1_DETECT_NT_DIST = 2005
DATA_CMD_MF1_NESTED_ACQUIRE = 2006
DATA_CMD_MF1_AUTH_ONE_KEY_BLOCK = 2007
DATA_CMD_MF1_READ_ONE_BLOCK = 2008
DATA_CMD_MF1_WRITE_ONE_BLOCK = 2009
DATA_CMD_HF14A_RAW = 2010

DATA_CMD_EM410X_SCAN = 3000
DATA_CMD_EM410X_WRITE_TO_T55XX = 3001

DATA_CMD_MF1_WRITE_EMU_BLOCK_DATA = 4000
DATA_CMD_HF14A_SET_ANTI_COLL_DATA = 4001
DATA_CMD_MF1_SET_DETECTION_ENABLE = 4004
DATA_CMD_MF1_GET_DETECTION_COUNT = 4005
DATA_CMD_MF1_GET_DETECTION_LOG = 4006
# FIXME: not implemented
DATA_CMD_MF1_GET_DETECTION_ENABLE = 4007
DATA_CMD_MF1_READ_EMU_BLOCK_DATA = 4008
DATA_CMD_MF1_GET_EMULATOR_CONFIG = 4009
# FIXME: not implemented
DATA_CMD_MF1_GET_GEN1A_MODE = 4010
DATA_CMD_MF1_SET_GEN1A_MODE = 4011
# FIXME: not implemented
DATA_CMD_MF1_GET_GEN2_MODE = 4012
DATA_CMD_MF1_SET_GEN2_MODE = 4013
# FIXME: not implemented
DATA_CMD_MF1_GET_BLOCK_ANTI_COLL_MODE = 4014
DATA_CMD_MF1_SET_BLOCK_ANTI_COLL_MODE = 4015
# FIXME: not implemented
DATA_CMD_MF1_GET_WRITE_MODE = 4016
DATA_CMD_MF1_SET_WRITE_MODE = 4017
DATA_CMD_HF14A_GET_ANTI_COLL_DATA = 4018

DATA_CMD_EM410X_SET_EMU_ID = 5000
DATA_CMD_EM410X_GET_EMU_ID = 5001


@enum.unique
class SlotNumber(enum.IntEnum):
    SLOT_1 = 1,
    SLOT_2 = 2,
    SLOT_3 = 3,
    SLOT_4 = 4,
    SLOT_5 = 5,
    SLOT_6 = 6,
    SLOT_7 = 7,
    SLOT_8 = 8,

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
    TAG_SENSE_NO = 0
    # 125 kHz
    TAG_SENSE_LF = 1
    # 13.56 MHz
    TAG_SENSE_HF = 2

    @staticmethod
    def list(exclude_unknown=True):
        enum_list = list(map(int, TagSenseType))
        if exclude_unknown:
            enum_list.remove(TagSenseType.TAG_SENSE_NO)
        return enum_list

    def __str__(self):
        if self == TagSenseType.TAG_SENSE_LF:
            return "LF"
        elif self == TagSenseType.TAG_SENSE_HF:
            return "HF"
        return "None"


@enum.unique
class TagSpecificType(enum.IntEnum):
    # Empty slot
    TAG_TYPE_UNKNOWN = 0
    # 125 kHz (id) cards
    TAG_TYPE_EM410X = 1
    # Mifare Classic
    TAG_TYPE_MIFARE_Mini = 2
    TAG_TYPE_MIFARE_1024 = 3
    TAG_TYPE_MIFARE_2048 = 4
    TAG_TYPE_MIFARE_4096 = 5
    # NTAG
    TAG_TYPE_NTAG_213 = 6
    TAG_TYPE_NTAG_215 = 7
    TAG_TYPE_NTAG_216 = 8

    @staticmethod
    def list(exclude_unknown=True):
        enum_list = list(map(int, TagSpecificType))
        if exclude_unknown:
            enum_list.remove(TagSpecificType.TAG_TYPE_UNKNOWN)
        return enum_list

    def __str__(self):
        if self == TagSpecificType.TAG_TYPE_EM410X:
            return "EM410X"
        elif self == TagSpecificType.TAG_TYPE_MIFARE_Mini:
            return "Mifare Mini"
        elif self == TagSpecificType.TAG_TYPE_MIFARE_1024:
            return "Mifare Classic 1k"
        elif self == TagSpecificType.TAG_TYPE_MIFARE_2048:
            return "Mifare Classic 2k"
        elif self == TagSpecificType.TAG_TYPE_MIFARE_4096:
            return "Mifare Classic 4k"
        elif self == TagSpecificType.TAG_TYPE_NTAG_213:
            return "NTAG 213"
        elif self == TagSpecificType.TAG_TYPE_NTAG_215:
            return "NTAG 215"
        elif self == TagSpecificType.TAG_TYPE_NTAG_216:
            return "NTAG 216"
        return "Undefined"


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

    @staticmethod
    def list():
        return list(map(int, MifareClassicWriteMode))

    def __str__(self):
        if self == MifareClassicWriteMode.NORMAL:
            return "Normal"
        elif self == MifareClassicWriteMode.DENIED:
            return "Denied"
        elif self == MifareClassicWriteMode.DECEIVE:
            return "Deceive"
        elif self == MifareClassicWriteMode.SHADOW:
            return "Shadow"
        return "None"


@enum.unique
class MifareClassicPrngType(enum.IntEnum):
    # the random number of the card response is fixed
    STATIC = 0
    # the random number of the card response is weak
    WEAK = 1
    # the random number of the card response is unpredictable
    HARD = 2

    @staticmethod
    def list():
        return list(map(int, MifareClassicPrngType))

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

    @staticmethod
    def list():
        return list(map(int, MifareClassicDarksideStatus))

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
class ButtonType(enum.IntEnum):
    # what, you need the doc for button type? maybe chatgpt known... LOL
    ButtonA = ord('A')
    ButtonB = ord('B')

    @staticmethod
    def list():
        return list(map(int, ButtonType))

    @staticmethod
    def list_str():
        return list(map(chr, ButtonType))

    @staticmethod
    def from_str(val):
        if ButtonType.ButtonA == ord(val):
            return ButtonType.ButtonA
        elif ButtonType.ButtonB == ord(val):
            return ButtonType.ButtonB
        return None

    def __str__(self):
        if self == ButtonType.ButtonA:
            return "Button A"
        elif self == ButtonType.ButtonB:
            return "Button B"
        return "None"


@enum.unique
class ButtonPressFunction(enum.IntEnum):
    SettingsButtonDisable = 0
    SettingsButtonCycleSlot = 1
    SettingsButtonCycleSlotDec = 2
    SettingsButtonCloneIcUid = 3

    @staticmethod
    def list():
        return list(map(int, ButtonPressFunction))

    def __str__(self):
        if self == ButtonPressFunction.SettingsButtonDisable:
            return "No Function"
        elif self == ButtonPressFunction.SettingsButtonCycleSlot:
            return "Cycle Slot"
        elif self == ButtonPressFunction.SettingsButtonCycleSlotDec:
            return "Cycle Slot Dec"
        elif self == ButtonPressFunction.SettingsButtonCloneIcUid:
            return "Quickly Copy Ic Uid"
        return "None"

    @staticmethod
    def from_int(val):
        return ButtonPressFunction(val)

    # get usage for button function
    def usage(self):
        if self == ButtonPressFunction.SettingsButtonDisable:
            return "This button have no function"
        elif self == ButtonPressFunction.SettingsButtonCycleSlot:
            return "Card slot number sequence will increase after pressing"
        elif self == ButtonPressFunction.SettingsButtonCycleSlotDec:
            return "Card slot number sequence decreases after pressing"
        elif self == ButtonPressFunction.SettingsButtonCloneIcUid:
            return ("Read the UID card number immediately after pressing, continue searching," +
                    "and simulate immediately after reading the card")
        return "Unknown"


class ChameleonCMD:
    """
        Chameleon cmd function
    """

    def __init__(self, chameleon: chameleon_com.ChameleonCom):
        """
        :param chameleon: chameleon instance, @see chameleon_device.Chameleon
        """
        self.device = chameleon

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_app_version(self):
        """
            Get firmware version number(application)
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_APP_VERSION)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = struct.unpack('!BB', resp.data)
        # older protocol, must upgrade!
        if resp.status == 0 and resp.data == b'\x00\x01':
            print("Chameleon does not understand new protocol. Please update firmware")
            return chameleon_com.Response(cmd=DATA_CMD_GET_APP_VERSION,
                                          status=chameleon_status.Device.STATUS_NOT_IMPLEMENTED)
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_chip_id(self):
        """
            Get device chip id
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_CHIP_ID)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data.hex()
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_address(self):
        """
            Get device address
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_ADDRESS)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data.hex()
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_git_version(self) -> str:
        resp = self.device.send_cmd_sync(DATA_CMD_GET_GIT_VERSION)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data.decode('utf-8')
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_mode(self):
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_MODE)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data, = struct.unpack('!?', resp.data)
        return resp

    def is_device_reader_mode(self) -> bool:
        """
            Get device mode, reader or tag
        :return: True is reader mode, else tag mode
        """
        return self.get_device_mode()

    # Note: Will return NOT_IMPLEMENTED if one tries to set reader mode on Lite
    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def change_device_mode(self, mode):
        data = struct.pack('!B', mode)
        return self.device.send_cmd_sync(DATA_CMD_CHANGE_DEVICE_MODE, data)

    def set_device_reader_mode(self, reader_mode: bool = True):
        """
            Change device mode, reader or tag
        :param reader_mode: True if reader mode, False if tag mode.
        :return:
        """
        self.change_device_mode(reader_mode)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def hf14a_scan(self):
        """
        14a tags in the scanning field
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_HF14A_SCAN)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            # uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
            offset = 0
            data = []
            while offset < len(resp.data):
                uidlen, = struct.unpack_from('!B', resp.data, offset)
                offset += struct.calcsize('!B')
                uid, atqa, sak, atslen = struct.unpack_from(f'!{uidlen}s2s1sB', resp.data, offset)
                offset += struct.calcsize(f'!{uidlen}s2s1sB')
                ats, = struct.unpack_from(f'!{atslen}s', resp.data, offset)
                offset += struct.calcsize(f'!{atslen}s')
                data.append({'uid': uid, 'atqa': atqa, 'sak': sak, 'ats': ats})
            resp.data = data
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_detect_support(self):
        """
        Detect whether it is mifare classic tag
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_DETECT_SUPPORT)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            resp.data, = struct.unpack('!?', resp.data)
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_detect_prng(self):
        """
        detect mifare Class of classic nt vulnerabilities
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_DETECT_PRNG)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_detect_nt_dist(self, block_known, type_known, key_known):
        """
        Detect the random number distance of the card
        :return:
        """
        data = struct.pack('!BB6s', type_known, block_known, key_known)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_DETECT_NT_DIST, data)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            uid, dist = struct.unpack('!II', resp.data)
            resp.data = {'uid': uid, 'dist': dist}
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_nested_acquire(self, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the key NT parameters needed for Nested decryption
        :return:
        """
        data = struct.pack('!BB6sBB', type_known, block_known, key_known, type_target, block_target)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_NESTED_ACQUIRE, data)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            resp.data = [{'nt': nt, 'nt_enc': nt_enc, 'par': par}
                         for nt, nt_enc, par in struct.iter_unpack('!IIB', resp.data)]
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_darkside_acquire(self, block_target, type_target, first_recover: int or bool, sync_max):
        """
        Collect the key parameters needed for Darkside decryption
        :param block_target:
        :param type_target:
        :param first_recover:
        :param sync_max:
        :return:
        """
        data = struct.pack('!BBBB', type_target, block_target, first_recover, sync_max)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_DARKSIDE_ACQUIRE, data, timeout=sync_max * 10)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            if resp.data[0] == MifareClassicDarksideStatus.OK:
                darkside_status, uid, nt1, par, ks1, nr, ar = struct.unpack('!BIIQQII', resp.data)
                resp.data = (darkside_status, {'uid': uid, 'nt1': nt1, 'par': par, 'ks1': ks1, 'nr': nr, 'ar': ar})
            else:
                resp.data = (resp.data[0],)
        return resp

    @expect_response([chameleon_status.Device.HF_TAG_OK, chameleon_status.Device.MF_ERR_AUTH])
    def mf1_auth_one_key_block(self, block, type_value, key):
        """
        Verify the mf1 key, only verify the specified type of key for a single sector
        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = struct.pack('!BB6s', type_value, block, key)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_AUTH_ONE_KEY_BLOCK, data)
        resp.data = resp.status == chameleon_status.Device.HF_TAG_OK
        return resp

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_read_one_block(self, block, type_value, key):
        """
        read one mf1 block
        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = struct.pack('!BB6s', type_value, block, key)
        return self.device.send_cmd_sync(DATA_CMD_MF1_READ_ONE_BLOCK, data)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_write_one_block(self, block, type_value, key, block_data):
        """
        Write mf1 single block
        :param block:
        :param type_value:
        :param key:
        :param block_data:
        :return:
        """
        data = struct.pack('!BB6s16s', type_value, block, key, block_data)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_WRITE_ONE_BLOCK, data)
        resp.data = resp.status == chameleon_status.Device.HF_TAG_OK
        return resp

    def hf14a_raw(self, options, resp_timeout_ms=100, data=[], bit_owned_by_the_last_byte=None):
        """
        Send raw cmd to 14a tag
        :param options:
        :param resp_timeout_ms:
        :param data:
        :param bit_owned_by_the_last_byte:
        :return:
        """
        
        class CStruct(ctypes.BigEndianStructure):
            _fields_ = [
                ("open_rf_field", ctypes.c_uint8, 1),
                ("wait_response", ctypes.c_uint8, 1),
                ("append_crc", ctypes.c_uint8, 1),
                ("bit_frame", ctypes.c_uint8, 1),
                ("auto_select", ctypes.c_uint8, 1),
                ("keep_rf_field", ctypes.c_uint8, 1),
                ("check_response_crc", ctypes.c_uint8, 1),
                ("reserved", ctypes.c_uint8, 1),
            ]

        cs = CStruct()
        cs.open_rf_field = options['open_rf_field']
        cs.wait_response = options['wait_response']
        cs.append_crc = options['append_crc']
        cs.bit_frame = options['bit_frame']
        cs.auto_select = options['auto_select']
        cs.keep_rf_field = options['keep_rf_field']
        cs.check_response_crc = options['check_response_crc']

        if options['bit_frame'] == 1:
            bits_or_bytes = len(data) * 8 # bits = bytes * 8(bit)
            if bit_owned_by_the_last_byte is not None and bit_owned_by_the_last_byte != 8:
                bits_or_bytes = bits_or_bytes - (8 - bit_owned_by_the_last_byte)
        else:
            bits_or_bytes = len(data) # bytes length

        if len(data) > 0:
            data = struct.pack(f'!BHH{len(data)}s', bytes(cs)[0], resp_timeout_ms, bits_or_bytes, bytearray(data))
        else:
            data = struct.pack(f'!BHH', bytes(cs)[0], resp_timeout_ms, 0)

        return self.device.send_cmd_sync(DATA_CMD_HF14A_RAW, data, timeout=(resp_timeout_ms / 1000) + 1)


    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def mf1_static_nested_acquire(self, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the key NT parameters needed for StaticNested decryption
        :return:
        """
        data = struct.pack('!BB6sBB', type_known, block_known, key_known, type_target, block_target)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_STATIC_NESTED_ACQUIRE, data)
        if resp.status == chameleon_status.Device.HF_TAG_OK:
            resp.data = {
                'uid': struct.unpack('!I', resp.data[0:4])[0],
                'nts': [
                    {
                        'nt': nt,
                        'nt_enc': nt_enc
                    } for nt, nt_enc in struct.iter_unpack('!II', resp.data[4:])
                ]
            }
        return resp

    @expect_response(chameleon_status.Device.LF_TAG_OK)
    def em410x_scan(self):
        """
        Read the card number of EM410X
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_EM410X_SCAN)

    @expect_response(chameleon_status.Device.LF_TAG_OK)
    def em410x_write_to_t55xx(self, id_bytes: bytes):
        """
        Write EM410X card number into T55XX
        :param id_bytes: ID card number
        :return:
        """
        new_key = b'\x20\x20\x66\x66'
        old_keys = [b'\x51\x24\x36\x48', b'\x19\x92\x04\x27']
        if len(id_bytes) != 5:
            raise ValueError("The id bytes length must equal 5")
        data = struct.pack(f'!5s4s{4*len(old_keys)}s', id_bytes, new_key, b''.join(old_keys))
        return self.device.send_cmd_sync(DATA_CMD_EM410X_WRITE_TO_T55XX, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_slot_info(self):
        """
            Get slots info
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_SLOT_INFO)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = [struct.unpack_from('!HH', resp.data, i)
                         for i in range(0, len(resp.data), struct.calcsize('!HH'))]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_active_slot(self):
        """
            Get selected slot
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_ACTIVE_SLOT)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_active_slot(self, slot_index: SlotNumber):
        """
            Set the card slot currently active for use
        :param slot_index: Card slot index
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!B', SlotNumber.to_fw(slot_index))
        return self.device.send_cmd_sync(DATA_CMD_SET_ACTIVE_SLOT, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_tag_type(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
        Set the label type of the simulated card of the current card slot
        Note: This operation will not change the data in the flash,
              and the change of the data in the flash will only be updated at the next save
        :param slot_index:  Card slot number
        :param tag_type:  label type
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BH', SlotNumber.to_fw(slot_index), tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_TYPE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def delete_slot_sense_type(self, slot_index: SlotNumber, sense_type: TagSenseType):
        """
            Delete a sense type for a specific slot.
        :param slot_index: Slot index
        :param sense_type: Sense type to disable
        :return:
        """
        data = struct.pack('!BB', SlotNumber.to_fw(slot_index), sense_type)
        return self.device.send_cmd_sync(DATA_CMD_DELETE_SLOT_SENSE_TYPE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_data_default(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
        Set the data of the simulated card in the specified card slot as the default data
        Note: This API will set the data in the flash together
        :param slot_index: Card slot number
        :param tag_type:  The default label type to set
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BH', SlotNumber.to_fw(slot_index), tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_DATA_DEFAULT, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_enable(self, slot_index: SlotNumber, enabled: bool):
        """
        Set whether the specified card slot is enabled
        :param slot_index: Card slot number
        :param enable: Whether to enable
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BB', SlotNumber.to_fw(slot_index), enabled)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ENABLE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def em410x_set_emu_id(self, id: bytes):
        """
        Set the card number simulated by EM410x
        :param id_bytes: byte of the card number
        :return:
        """
        if len(id) != 5:
            raise ValueError("The id bytes length must equal 5")
        data = struct.pack('5s', id)
        return self.device.send_cmd_sync(DATA_CMD_EM410X_SET_EMU_ID, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def em410x_get_emu_id(self):
        """
            Get the simulated EM410x card id
        """
        return self.device.send_cmd_sync(DATA_CMD_EM410X_GET_EMU_ID)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_set_detection_enable(self, enabled: bool):
        """
        Set whether to enable the detection of the current card slot
        :param enable: Whether to enable
        :return:
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(DATA_CMD_MF1_SET_DETECTION_ENABLE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_get_detection_count(self):
        """
        Get the statistics of the current detection records
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_GET_DETECTION_COUNT)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data, = struct.unpack('!I', resp.data)
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_get_detection_log(self, index: int):
        """
        Get detection logs from the specified index position
        :param index: start index
        :return:
        """
        data = struct.pack('!I', index)
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_GET_DETECTION_LOG, data)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            # convert
            result_list = []
            pos = 0
            while pos < len(resp.data):
                block, bitfield, uid, nt, nr, ar = struct.unpack_from('!BB4s4s4s4s', resp.data, pos)
                result_list.append({
                    'block': block,
                    'type': ['A', 'B'][bitfield & 0x01],
                    'is_nested': bool(bitfield & 0x02),
                    'uid': uid.hex(),
                    'nt': nt.hex(),
                    'nr': nr.hex(),
                    'ar': ar.hex()
                })
                pos += struct.calcsize('!BB4s4s4s4s')
            resp.data = result_list
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_write_emu_block_data(self, block_start: int, block_data: bytes):
        """
        Set the block data of the analog card of MF1
        :param block_start:  Start setting the location of block data, including this location
        :param block_data:  The byte buffer of the block data to be set
        can contain multiple block data, automatically from block_start  increment
        :return:
        """
        data = struct.pack(f'!B{len(block_data)}s', block_start, block_data)
        return self.device.send_cmd_sync(DATA_CMD_MF1_WRITE_EMU_BLOCK_DATA, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_read_emu_block_data(self, block_start: int, block_count: int):
        """
            Gets data for selected block range
        """
        data = struct.pack('!BB', block_start, block_count)
        return self.device.send_cmd_sync(DATA_CMD_MF1_READ_EMU_BLOCK_DATA, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def hf14a_set_anti_coll_data(self, uid: bytes, atqa: bytes, sak: bytes, ats: bytes = b''):
        """
        Set anti-collision data of current HF slot (UID/SAK/ATQA/ATS)
        :param uid:  uid bytes
        :param atqa: atqa bytes
        :param sak:  sak bytes
        :param ats:  ats bytes (optional)
        :return:
        """
        data = struct.pack(f'!B{len(uid)}s2s1sB{len(ats)}s', len(uid), uid, atqa, sak, len(ats), ats)
        return self.device.send_cmd_sync(DATA_CMD_HF14A_SET_ANTI_COLL_DATA, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_tag_nick(self, slot: SlotNumber, sense_type: TagSenseType, name: bytes):
        """
        Set the nick name of the slot
        :param slot:  Card slot number
        :param sense_type:  field type
        :param name:  Card slot nickname
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = struct.pack(f'!BB{len(name)}s', SlotNumber.to_fw(slot), sense_type, name)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_NICK, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_slot_tag_nick(self, slot: SlotNumber, sense_type: TagSenseType):
        """
        Get the nick name of the slot
        :param slot:  Card slot number
        :param sense_type:  field type
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = struct.pack('!BB', SlotNumber.to_fw(slot), sense_type)
        return self.device.send_cmd_sync(DATA_CMD_GET_SLOT_TAG_NICK, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_get_emulator_config(self):
        """
            Get array of Mifare Classic emulators settings:
            [0] - mf1_is_detection_enable (mfkey32)
            [1] - mf1_is_gen1a_magic_mode
            [2] - mf1_is_gen2_magic_mode
            [3] - mf1_is_use_mf1_coll_res (use UID/BCC/SAK/ATQA from 0 block)
            [4] - mf1_get_write_mode
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_MF1_GET_EMULATOR_CONFIG)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            b1, b2, b3, b4, b5 = struct.unpack('!????B', resp.data)
            resp.data = {'detection': b1,
                         'gen1a_mode': b2,
                         'gen2_mode': b3,
                         'block_anti_coll_mode': b4,
                         'write_mode': b5}
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_set_gen1a_mode(self, enabled: bool):
        """
        Set gen1a magic mode
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(DATA_CMD_MF1_SET_GEN1A_MODE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_set_gen2_mode(self, enabled: bool):
        """
        Set gen2 magic mode
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(DATA_CMD_MF1_SET_GEN2_MODE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_set_block_anti_coll_mode(self, enabled: bool):
        """
        Set 0 block anti-collision data
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(DATA_CMD_MF1_SET_BLOCK_ANTI_COLL_MODE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def mf1_set_write_mode(self, mode: int):
        """
        Set write mode
        """
        data = struct.pack('!B', mode)
        return self.device.send_cmd_sync(DATA_CMD_MF1_SET_WRITE_MODE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def slot_data_config_save(self):
        """
        Update the configuration and data of the card slot to flash.
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SLOT_DATA_CONFIG_SAVE)

    def enter_bootloader(self):
        """
        Reboot into DFU mode (bootloader)
        :return:
        """
        self.device.send_cmd_auto(DATA_CMD_ENTER_BOOTLOADER, close=True)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_animation_mode(self):
        """
        Get animation mode value
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_ANIMATION_MODE)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_enabled_slots(self):
        """
        Get enabled slots
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_ENABLED_SLOTS)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_animation_mode(self, value: int):
        """
        Set animation mode value
        """
        data = struct.pack('!B', value)
        return self.device.send_cmd_sync(DATA_CMD_SET_ANIMATION_MODE, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def reset_settings(self):
        """
        Reset settings stored in flash memory
        """
        resp = self.device.send_cmd_sync(DATA_CMD_RESET_SETTINGS)
        resp.data = resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def save_settings(self):
        """
        Store settings to flash memory
        """
        resp = self.device.send_cmd_sync(DATA_CMD_SAVE_SETTINGS)
        resp.data = resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def wipe_fds(self):
        """
        Reset to factory settings
        """
        resp = self.device.send_cmd_sync(DATA_CMD_WIPE_FDS)
        resp.data = resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS
        self.device.close()
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_battery_info(self):
        """
        Get battery info
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_BATTERY_INFO)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = struct.unpack('!HB', resp.data)
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_button_press_config(self, button: ButtonType):
        """
        Get config of button press function
        """
        data = struct.pack('!B', button)
        resp = self.device.send_cmd_sync(DATA_CMD_GET_BUTTON_PRESS_CONFIG, data)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_button_press_config(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of button press function
        """
        data = struct.pack('!BB', button, function)
        return self.device.send_cmd_sync(DATA_CMD_SET_BUTTON_PRESS_CONFIG, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_long_button_press_config(self, button: ButtonType):
        """
        Get config of long button press function
        """
        data = struct.pack('!B', button)
        resp = self.device.send_cmd_sync(DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG, data)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_long_button_press_config(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of long button press function
        """
        data = struct.pack('!BB', button, function)
        return self.device.send_cmd_sync(DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_ble_connect_key(self, key: str):
        """
        Set config of ble connect key
        """
        data_bytes = key.encode(encoding='ascii')

        # check key length
        if len(data_bytes) != 6:
            raise ValueError("The ble connect key length must be 6")

        data = struct.pack('6s', data_bytes)
        return self.device.send_cmd_sync(DATA_CMD_SET_BLE_PAIRING_KEY, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_ble_pairing_key(self):
        """
        Get config of ble connect key
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_BLE_PAIRING_KEY)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def delete_ble_all_bonds(self):
        """
        From peer manager delete all bonds.
        """
        return self.device.send_cmd_sync(DATA_CMD_DELETE_ALL_BLE_BONDS)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_capabilities(self):
        """
        Get list of commands that client understands
        """
        try:
            resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_CAPABILITIES)
        except chameleon_com.CMDInvalidException:
            print("Chameleon does not understand get_device_capabilities command. Please update firmware")
            return chameleon_com.Response(cmd=DATA_CMD_GET_DEVICE_CAPABILITIES,
                                          status=chameleon_status.Device.STATUS_NOT_IMPLEMENTED)
        else:
            if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
                resp.data = [x[0] for x in struct.iter_unpack('!H', resp.data)]
            return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_model(self):
        """
        Get device model
        0 - Chameleon Ultra
        1 - Chameleon Lite
        """

        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_MODEL)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data = resp.data[0]
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_device_settings(self):
        """
        Get all possible settings
        For version 5:
        settings[0] = SETTINGS_CURRENT_VERSION; // current version
        settings[1] = settings_get_animation_config(); // animation mode
        settings[2] = settings_get_button_press_config('A'); // short A button press mode
        settings[3] = settings_get_button_press_config('B'); // short B button press mode
        settings[4] = settings_get_long_button_press_config('A'); // long A button press mode
        settings[5] = settings_get_long_button_press_config('B'); // long B button press mode
        settings[6] = settings_get_ble_pairing_enable(); // does device require pairing
        settings[7:13] = settings_get_ble_pairing_key(); // BLE pairing key
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_SETTINGS)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            if resp.data[0] > CURRENT_VERSION_SETTINGS:
                raise ValueError("Settings version in app older than Chameleon. "
                                 "Please upgrade client")
            if resp.data[0] < CURRENT_VERSION_SETTINGS:
                raise ValueError("Settings version in app newer than Chameleon. "
                                 "Please upgrade Chameleon firmware")
            settings_version, animation_mode, btn_press_A, btn_press_B, btn_long_press_A, btn_long_press_B, ble_pairing_enable, ble_pairing_key = struct.unpack(
                '!BBBBBBB6s', resp.data)
            resp.data = {'settings_version': settings_version,
                         'animation_mode': animation_mode,
                         'btn_press_A': btn_press_A,
                         'btn_press_B': btn_press_B,
                         'btn_long_press_A': btn_long_press_A,
                         'btn_long_press_B': btn_long_press_B,
                         'ble_pairing_enable': ble_pairing_enable,
                         'ble_pairing_key': ble_pairing_key}
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def hf14a_get_anti_coll_data(self):
        """
        Get anti-collision data from current HF slot (UID/SAK/ATQA/ATS)
        :return:
        """
        resp = self.device.send_cmd_sync(DATA_CMD_HF14A_GET_ANTI_COLL_DATA)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS and len(resp.data) > 0:
            # uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
            offset = 0
            uidlen, = struct.unpack_from('!B', resp.data, offset)
            offset += struct.calcsize('!B')
            uid, atqa, sak, atslen = struct.unpack_from(f'!{uidlen}s2s1sB', resp.data, offset)
            offset += struct.calcsize(f'!{uidlen}s2s1sB')
            ats, = struct.unpack_from(f'!{atslen}s', resp.data, offset)
            offset += struct.calcsize(f'!{atslen}s')
            resp.data = {'uid': uid, 'atqa': atqa, 'sak': sak, 'ats': ats}
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_ble_pairing_enable(self):
        """
        Is ble pairing enable?
        :return: True if pairing is enable, False if pairing disabled
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_BLE_PAIRING_ENABLE)
        if resp.status == chameleon_status.Device.STATUS_DEVICE_SUCCESS:
            resp.data, = struct.unpack('!?', resp.data)
        return resp

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_ble_pairing_enable(self, enabled: bool):
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(DATA_CMD_SET_BLE_PAIRING_ENABLE, data)


def test_fn():
    # connect to chameleon
    dev = chameleon_com.ChameleonCom()
    dev.open("com19")
    cml = ChameleonCMD(dev)
    ver = cml.get_app_version()
    print(f"Firmware number of application: {ver[0]}.{ver[1]}")
    chip = cml.get_device_chip_id()
    print(f"Device chip id: {chip}")

    # change to reader mode
    cml.set_device_reader_mode()

    options = {
        'open_rf_field': 1,
        'wait_response': 1,
        'append_crc': 0,
        'bit_frame': 1,
        'auto_select': 0,
        'keep_rf_field': 1,
        'check_response_crc': 0,
    }

    # unlock 1
    resp = cml.hf14a_raw(options=options, resp_timeout_ms=1000, data=[0x40], bit_owned_by_the_last_byte=7)

    if resp.status == 0x00 and resp.data[0] == 0x0a:
        print("Gen1A unlock 1 success")
        # unlock 2
        options['bit_frame'] = 0
        resp = cml.hf14a_raw(options=options, resp_timeout_ms=1000, data=[0x43])
        if resp.status == 0x00 and resp.data[0] == 0x0a:
            print("Gen1A unlock 2 success")
            print("Start dump gen1a memeory...")
            block = 0
            while block < 64:
                # Tag read block cmd
                cmd_read_gen1a_block = [0x30, block]
                
                # Transfer with crc
                options['append_crc'] = 1
                options['check_response_crc'] = 1
                resp = cml.hf14a_raw(options=options, resp_timeout_ms=100, data=cmd_read_gen1a_block)
                
                print(f"Block {block} : {resp.data.hex()}")
                block += 1

            # Close rf field
            options['keep_rf_field'] = 0
            resp = cml.hf14a_raw(options=options)
        else:
            print("Gen1A unlock 2 fail")
    else:
        print("Gen1A unlock 1 fail")

    # disconnect
    dev.close()

    # never exit
    while True:
        pass


if __name__ == '__main__':
    test_fn()
