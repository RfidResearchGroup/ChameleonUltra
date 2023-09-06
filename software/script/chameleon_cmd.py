import enum
import struct

import chameleon_com
import chameleon_status
from chameleon_utils import expect_response

DATA_CMD_GET_APP_VERSION = 1000
DATA_CMD_CHANGE_MODE = 1001
DATA_CMD_GET_DEVICE_MODE = 1002
DATA_CMD_SET_SLOT_ACTIVATED = 1003
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

DATA_CMD_SET_BLE_CONNECT_KEY_CONFIG = 1030
DATA_CMD_GET_BLE_CONNECT_KEY_CONFIG = 1031

DATA_CMD_DELETE_ALL_BLE_BONDS = 1032

DATA_CMD_GET_DEVICE = 1033
DATA_CMD_GET_SETTINGS = 1034
DATA_CMD_GET_DEVICE_CAPABILITIES = 1035

DATA_CMD_SCAN_14A_TAG = 2000
DATA_CMD_MF1_SUPPORT_DETECT = 2001
DATA_CMD_MF1_NT_LEVEL_DETECT = 2002
DATA_CMD_MF1_DARKSIDE_DETECT = 2003
DATA_CMD_MF1_DARKSIDE_ACQUIRE = 2004
DATA_CMD_MF1_NT_DIST_DETECT = 2005
DATA_CMD_MF1_NESTED_ACQUIRE = 2006
DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK = 2007
DATA_CMD_MF1_READ_ONE_BLOCK = 2008
DATA_CMD_MF1_WRITE_ONE_BLOCK = 2009

DATA_CMD_SCAN_EM410X_TAG = 3000
DATA_CMD_WRITE_EM410X_TO_T5577 = 3001

DATA_CMD_LOAD_MF1_EMU_BLOCK_DATA = 4000
DATA_CMD_SET_MF1_ANTI_COLLISION_RES = 4001

DATA_CMD_SET_MF1_DETECTION_ENABLE = 4004
DATA_CMD_GET_MF1_DETECTION_COUNT = 4005
DATA_CMD_GET_MF1_DETECTION_RESULT = 4006

DATA_CMD_READ_MF1_EMU_BLOCK_DATA = 4008

DATA_CMD_GET_MF1_EMULATOR_CONFIG = 4009
DATA_CMD_GET_MF1_GEN1A_MODE = 4010
DATA_CMD_SET_MF1_GEN1A_MODE = 4011
DATA_CMD_GET_MF1_GEN2_MODE = 4012
DATA_CMD_SET_MF1_GEN2_MODE = 4013
DATA_CMD_GET_MF1_USE_FIRST_BLOCK_COLL = 4014
DATA_CMD_SET_MF1_USE_FIRST_BLOCK_COLL = 4015
DATA_CMD_GET_MF1_WRITE_MODE = 4016
DATA_CMD_SET_MF1_WRITE_MODE = 4017

DATA_CMD_SET_EM410X_EMU_ID = 5000
DATA_CMD_GET_EM410X_EMU_ID = 5001


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
        return "Unknown"


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
            return "Read the UID card number immediately after pressing, continue searching," + \
                   "and simulate immediately after reading the card"
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
        if not len(self.device.commands):
            self.get_device_capabilities()

    def get_firmware_version(self) -> int:
        """
            Get firmware version number(application)
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_APP_VERSION, 0x00)
        return int.from_bytes(resp.data, 'little')

    def get_device_chip_id(self) -> str:
        """
            Get device chip id
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_CHIP_ID, 0x00)
        return resp.data.hex()

    def get_device_address(self) -> str:
        """
            Get device address
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_ADDRESS, 0x00)
        return resp.data[::-1].hex()

    def get_git_version(self) -> str:
        resp = self.device.send_cmd_sync(DATA_CMD_GET_GIT_VERSION, 0x00)
        return resp.data.decode('utf-8')

    def is_reader_device_mode(self) -> bool:
        """
            Get device mode, reader or tag
        :return: True is reader mode, else tag mode
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_MODE, 0x00)
        return True if resp.data[0] == 1 else False

    # Note: Will return NOT_IMPLEMENTED if one tries to set reader mode on Lite
    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_reader_device_mode(self, reader_mode: bool = True):
        """
            Change device mode, reader or tag
        :param reader_mode: True if reader mode, False if tag mode.
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_CHANGE_MODE, 0x00, 0x0001 if reader_mode else 0x0000)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def scan_tag_14a(self):
        """
        14a tags in the scanning field
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SCAN_14A_TAG, 0x00)

    def detect_mf1_support(self):
        """
        Detect whether it is mifare classic label
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_SUPPORT_DETECT, 0x00)

    def detect_mf1_nt_level(self):
        """
        detect mifare Class of classic nt vulnerabilities
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_NT_LEVEL_DETECT, 0x00)

    def detect_darkside_support(self):
        """
        Check if the card is vulnerable to mifare classic darkside attack
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_DARKSIDE_DETECT, 0x00, None, timeout=20)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def detect_nt_distance(self, block_known, type_known, key_known):
        """
        Detect the random number distance of the card
        :return:
        """
        data = bytearray()
        data.append(type_known)
        data.append(block_known)
        data.extend(key_known)
        return self.device.send_cmd_sync(DATA_CMD_MF1_NT_DIST_DETECT, 0x00, data)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def acquire_nested(self, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the key NT parameters needed for Nested decryption
        :return:
        """
        data = bytearray()
        data.append(type_known)
        data.append(block_known)
        data.extend(key_known)
        data.append(type_target)
        data.append(block_target)
        return self.device.send_cmd_sync(DATA_CMD_MF1_NESTED_ACQUIRE, 0x00, data)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def acquire_darkside(self, block_target, type_target, first_recover: int or bool, sync_max):
        """
        Collect the key parameters needed for Darkside decryption
        :param block_target:
        :param type_target:
        :param first_recover:
        :param sync_max:
        :return:
        """
        data = bytearray()
        data.append(type_target)
        data.append(block_target)
        if isinstance(first_recover, bool):
            first_recover = 0x01 if first_recover else 0x00
        data.append(first_recover)
        data.append(sync_max)
        return self.device.send_cmd_sync(DATA_CMD_MF1_DARKSIDE_ACQUIRE, 0x00, data, timeout=sync_max + 5)

    @expect_response([
        chameleon_status.Device.HF_TAG_OK,
        chameleon_status.Device.MF_ERR_AUTH,
    ])
    def auth_mf1_key(self, block, type_value, key):
        """
        Verify the mf1 key, only verify the specified type of key for a single sector
        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = bytearray()
        data.append(type_value)
        data.append(block)
        data.extend(key)
        return self.device.send_cmd_sync(DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK, 0x00, data)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def read_mf1_block(self, block, type_value, key):
        """
        read one mf1 block
        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = bytearray()
        data.append(type_value)
        data.append(block)
        data.extend(key)
        return self.device.send_cmd_sync(DATA_CMD_MF1_READ_ONE_BLOCK, 0x00, data)

    @expect_response(chameleon_status.Device.HF_TAG_OK)
    def write_mf1_block(self, block, type_value, key, block_data):
        """
        Write mf1 single block
        :param block:
        :param type_value:
        :param key:
        :param block_data:
        :return:
        """
        data = bytearray()
        data.append(type_value)
        data.append(block)
        data.extend(key)
        data.extend(block_data)
        return self.device.send_cmd_sync(DATA_CMD_MF1_WRITE_ONE_BLOCK, 0x00, data)

    @expect_response(chameleon_status.Device.LF_TAG_OK)
    def read_em_410x(self):
        """
        Read the card number of EM410X
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SCAN_EM410X_TAG, 0x00)

    @expect_response(chameleon_status.Device.LF_TAG_OK)
    def write_em_410x_to_t55xx(self, id_bytes: bytearray):
        """
        Write EM410X card number into T55XX
        :param id_bytes: ID card number
        :return:
        """
        new_key = [0x20, 0x20, 0x66, 0x66]
        old_keys = [[0x51, 0x24, 0x36, 0x48], [0x19, 0x92, 0x04, 0x27]]
        if len(id_bytes) != 5:
            raise ValueError("The id bytes length must equal 5")
        data = bytearray()
        data.extend(id_bytes)
        data.extend(new_key)
        for key in old_keys:
            data.extend(key)
        return self.device.send_cmd_sync(DATA_CMD_WRITE_EM410X_TO_T5577, 0x00, data)

    def get_slot_info(self):
        """
            Get slots info
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_SLOT_INFO, 0x00)

    def get_active_slot(self):
        """
            Get selected slot
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_ACTIVE_SLOT, 0x00)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_activated(self, slot_index: SlotNumber):
        """
            Set the card slot currently active for use
        :param slot_index: Card slot index
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ACTIVATED, 0x00, data)

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
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_TYPE, 0x00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def delete_slot_sense_type(self, slot_index: SlotNumber, sense_type: TagSenseType):
        """
            Delete a sense type for a specific slot.
        :param slot_index: Slot index
        :param sense_type: Sense type to disable
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_DELETE_SLOT_SENSE_TYPE, 0x00, bytearray([
            SlotNumber.to_fw(slot_index),
            sense_type,
        ]))

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
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_DATA_DEFAULT, 0x00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_enable(self, slot_index: SlotNumber, enable: bool):
        """
        Set whether the specified card slot is enabled
        :param slot_index: Card slot number
        :param enable: Whether to enable
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(0x01 if enable else 0x00)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ENABLE, 0X00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_em410x_sim_id(self, id_bytes: bytearray):
        """
        Set the card number simulated by EM410x
        :param id_bytes: byte of the card number
        :return:
        """
        if len(id_bytes) != 5:
            raise ValueError("The id bytes length must equal 5")
        return self.device.send_cmd_sync(DATA_CMD_SET_EM410X_EMU_ID, 0x00, id_bytes)

    def get_em410x_sim_id(self):
        """
            Get the simulated EM410x card id
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_EM410X_EMU_ID, 0x00)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_mf1_detection_enable(self, enable: bool):
        """
        Set whether to enable the detection of the current card slot
        :param enable: Whether to enable
        :return:
        """
        data = bytearray()
        data.append(0x01 if enable else 0x00)
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_DETECTION_ENABLE, 0x00, data)

    def get_mf1_detection_count(self):
        """
        Get the statistics of the current detection records
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_DETECTION_COUNT, 0x00)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_mf1_detection_log(self, index: int):
        """
        Get detection logs from the specified index position
        :param index: start index
        :return:
        """
        data = bytearray()
        data.extend(index.to_bytes(4, "big", signed=False))
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_DETECTION_RESULT, 0x00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_mf1_block_data(self, block_start: int, block_data: bytearray):
        """
        Set the block data of the analog card of MF1
        :param block_start:  Start setting the location of block data, including this location
        :param block_data:  The byte buffer of the block data to be set
        can contain multiple block data, automatically from block_start  increment
        :return:
        """
        data = bytearray()
        data.append(block_start & 0xFF)
        data.extend(block_data)
        return self.device.send_cmd_sync(DATA_CMD_LOAD_MF1_EMU_BLOCK_DATA, 0x00, data)

    def get_mf1_block_data(self, block_start: int, block_count: int):
        """
            Gets data for selected block range
        """
        data = struct.pack('<BH', block_start, block_count)
        return self.device.send_cmd_sync(DATA_CMD_READ_MF1_EMU_BLOCK_DATA, 0x00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_mf1_anti_collision_res(self, sak: bytearray, atqa: bytearray, uid: bytearray):
        """
        Set the anti-collision resource information of the MF1 analog card
        :param sak:  sak bytes
        :param atqa:  atqa array
        :param uid:  card number array
        :return:
        """
        data = bytearray()
        data.extend(sak)
        data.extend(atqa)
        data.extend(uid)
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_ANTI_COLLISION_RES, 0X00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType, name: bytes):
        """
        Set the anti-collision resource information of the MF1 analog card
        :param slot:  Card slot number
        :param sense_type:  field type
        :param name:  Card slot nickname
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = bytearray()
        data.extend([SlotNumber.to_fw(slot), sense_type])
        data.extend(name)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_NICK, 0x00, data)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType):
        """
        Set the anti-collision resource information of the MF1 analog card
        :param slot:  Card slot number
        :param sense_type:  field type
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = bytearray()
        data.extend([SlotNumber.to_fw(slot), sense_type])
        return self.device.send_cmd_sync(DATA_CMD_GET_SLOT_TAG_NICK, 0x00, data)

    def get_mf1_emulator_settings(self):
        """
            Get array of Mifare Classic emulators settings:
            [0] - mf1_is_detection_enable (mfkey32)
            [1] - mf1_is_gen1a_magic_mode
            [2] - mf1_is_gen2_magic_mode
            [3] - mf1_is_use_mf1_coll_res (use UID/BCC/SAK/ATQA from 0 block)
            [4] - mf1_get_write_mode
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_EMULATOR_CONFIG, 0x00)

    def set_mf1_gen1a_mode(self, enabled: bool):
        """
        Set gen1a magic mode
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_GEN1A_MODE, 0x00, bytearray([1 if enabled else 0]))

    def set_mf1_gen2_mode(self, enabled: bool):
        """
        Set gen2 magic mode
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_GEN2_MODE, 0x00, bytearray([1 if enabled else 0]))

    def set_mf1_block_anti_coll_mode(self, enabled: bool):
        """
        Set 0 block anti-collision data
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_USE_FIRST_BLOCK_COLL, 0x00, bytearray([1 if enabled else 0]))

    def set_mf1_write_mode(self, mode: int):
        """
        Set write mode
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_WRITE_MODE, 0x00, bytearray([mode]))

    def update_slot_data_config(self):
        """
        Update the configuration and data of the card slot to flash.
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SLOT_DATA_CONFIG_SAVE, 0x00)

    def enter_dfu_mode(self):
        """
        Reboot into DFU mode (bootloader)
        :return:
        """
        return self.device.send_cmd_auto(DATA_CMD_ENTER_BOOTLOADER, 0x00, close=True)

    def get_settings_animation(self):
        """
        Get animation mode value
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_ANIMATION_MODE, 0x00)

    def get_enabled_slots(self):
        """
        Get enabled slots
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_ENABLED_SLOTS, 0x00)

    def set_settings_animation(self, value: int):
        """
        Set animation mode value
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_ANIMATION_MODE, 0x00, bytearray([value]))

    def reset_settings(self):
        """
        Reset settings stored in flash memory
        """
        return self.device.send_cmd_sync(DATA_CMD_RESET_SETTINGS, 0x00)

    def store_settings(self):
        """
        Store settings to flash memory
        """
        return self.device.send_cmd_sync(DATA_CMD_SAVE_SETTINGS, 0x00)

    def factory_reset(self):
        """
        Reset to factory settings
        """
        ret = self.device.send_cmd_sync(DATA_CMD_WIPE_FDS, 0x00)
        self.device.close()
        return ret

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def battery_information(self):
        """
        Get battery info
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_BATTERY_INFO, 0x00)

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_button_press_fun(self, button: ButtonType):
        """
        Get config of button press function
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_BUTTON_PRESS_CONFIG, 0x00, bytearray([button]))

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_button_press_fun(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of button press function
        """
        return self.device.send_cmd_sync(
            DATA_CMD_SET_BUTTON_PRESS_CONFIG,
            0x00,
            bytearray([button, function])
        )

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def get_long_button_press_fun(self, button: ButtonType):
        """
        Get config of button press function
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG, 0x00, bytearray([button]))

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_long_button_press_fun(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of button press function
        """
        return self.device.send_cmd_sync(
            DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG,
            0x00,
            bytearray([button, function])
        )

    @expect_response(chameleon_status.Device.STATUS_DEVICE_SUCCESS)
    def set_ble_connect_key(self, key: str):
        """
        Set config of ble connect key
        """
        data_bytes = key.encode(encoding='ascii')

        # check key length
        if len(data_bytes) != 6:
            raise ValueError("The ble connect key length must be 6")

        return self.device.send_cmd_sync(
            DATA_CMD_SET_BLE_CONNECT_KEY_CONFIG,
            0x00,
            data_bytes
        )

    def get_ble_connect_key(self):
        """
        Get config of ble connect key
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_BLE_CONNECT_KEY_CONFIG, 0x00, None)

    def delete_ble_all_bonds(self):
        """
        From peer manager delete all bonds.
        """
        return self.device.send_cmd_sync(DATA_CMD_DELETE_ALL_BLE_BONDS, 0x00, None)

    def get_device_capabilities(self):
        """
        Get (and set) commands that client understands
        """

        commands = []

        try:
            ret = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_CAPABILITIES, 0x00)

            for i in range(0, len(ret.data), 2):
                if i + 1 < len(ret.data):
                    commands.append((ret.data[i + 1] << 8) | ret.data[i])

            self.device.commands = commands
        except:
            print("Chameleon doesn't understand get capabilities command. Please update firmware")

        return commands

if __name__ == '__main__':
    # connect to chameleon
    dev = chameleon_com.ChameleonCom()
    dev.open("com19")
    cml = ChameleonCMD(dev)
    ver = cml.get_firmware_version()
    print(f"Firmware number of application: {ver}")
    chip = cml.get_device_chip_id()
    print(f"Device chip id: {chip}")

    # disconnect
    dev.close()

    # never exit
    while True:
        pass
