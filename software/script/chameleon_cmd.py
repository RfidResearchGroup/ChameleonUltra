import enum
import struct

import chameleon_com
import chameleon_status

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
    # 125 kHz（ID）cards
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
    DEINED = 1
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
        elif self == MifareClassicWriteMode.DEINED:
            return "Deined"
        elif self == MifareClassicWriteMode.DECEIVE:
            return "Deceive"
        elif self == MifareClassicWriteMode.SHADOW:
            return "Shadow"
        return "None"


class BaseChameleonCMD:
    """
        Chameleon cmd function
    """

    def __init__(self, chameleon: chameleon_com.ChameleonCom):
        """
        :param chameleon: chameleon instance, @see chameleon_device.Chameleon
        """
        self.device = chameleon

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

    def set_reader_device_mode(self, reader_mode: bool = True):
        """
            Change device mode, reader or tag
        :param reader_mode: True if reader mode, False if tag mode.
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_CHANGE_MODE, 0x00, 0x0001 if reader_mode else 0x0000)

    def scan_tag_14a(self):
        """
            扫描场内的14a标签
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SCAN_14A_TAG, 0x00)

    def detect_mf1_support(self):
        """
            检测是否是mifare classic标签
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_SUPPORT_DETECT, 0x00)

    def detect_mf1_nt_level(self):
        """
            检测mifare classic的nt漏洞的等级
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_NT_LEVEL_DETECT, 0x00)

    def detect_darkside_support(self):
        """
            检测卡片是否易受mifare classic darkside攻击
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_DARKSIDE_DETECT, 0x00, None, timeout=20)

    def detect_nt_distance(self, block_known, type_known, key_known):
        """
            检测卡片的随机数距离
        :return:
        """
        data = bytearray()
        data.append(type_known)
        data.append(block_known)
        data.extend(key_known)
        return self.device.send_cmd_sync(DATA_CMD_MF1_NT_DIST_DETECT, 0x00, data)

    def acquire_nested(self, block_known, type_known, key_known, block_target, type_target):
        """
            采集Nested解密需要的关键NT参数
        :return:
        """
        data = bytearray()
        data.append(type_known)
        data.append(block_known)
        data.extend(key_known)
        data.append(type_target)
        data.append(block_target)
        return self.device.send_cmd_sync(DATA_CMD_MF1_NESTED_ACQUIRE, 0x00, data)

    def acquire_darkside(self, block_target, type_target, first_recover: int or bool, sync_max):
        """
            采集Darkside解密需要的关键参数
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

    def auth_mf1_key(self, block, type_value, key):
        """
            验证mf1秘钥，只验证单个扇区的指定类型的秘钥
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

    def read_mf1_block(self, block, type_value, key):
        """
            读取mf1单块
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

    def write_mf1_block(self, block, type_value, key, block_data):
        """
            写入mf1单块
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

    def read_em_410x(self):
        """
            读取EM410X的卡号
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SCAN_EM410X_TAG, 0x00)

    def write_em_410x_to_t55xx(self, id_bytes: bytearray):
        """
            写入EM410X卡号到T55XX中
        :param id_bytes: ID卡号
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

    def set_slot_tag_type(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
            设置当前卡槽的模拟卡的标签类型
            注意：此操作并不会更改flash中的数据，flash中的数据的变动仅在下次保存时更新
        :param slot_index: 卡槽号码
        :param tag_type: 标签类型
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_TYPE, 0x00, data)

    def set_slot_data_default(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
            设置指定卡槽的模拟卡的数据为缺省数据
            注意：此API会将flash中的数据一并进行设置
        :param slot_index: 卡槽号码
        :param tag_type: 要设置的缺省标签类型
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_DATA_DEFAULT, 0x00, data)

    def set_slot_enable(self, slot_index: SlotNumber, enable: bool):
        """
            设置指定的卡槽是否使能
        :param slot_index: 卡槽号码
        :param enable: 是否使能
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = bytearray()
        data.append(SlotNumber.to_fw(slot_index))
        data.append(0x01 if enable else 0x00)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ENABLE, 0X00, data)

    def set_em410x_sim_id(self, id_bytes: bytearray):
        """
            设置EM410x模拟的卡号
        :param id_bytes: 卡号的字节
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

    def set_mf1_detection_enable(self, enable: bool):
        """
            设置是否使能当前卡槽的侦测
        :param enable: 是否使能
        :return:
        """
        data = bytearray()
        data.append(0x01 if enable else 0x00)
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_DETECTION_ENABLE, 0x00, data)

    def get_mf1_detection_count(self):
        """
            获取当前侦测记录的统计个数
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_DETECTION_COUNT, 0x00)

    def get_mf1_detection_log(self, index: int):
        """
            从指定的index位置开始获取侦测日志
        :param index: 开始索引
        :return:
        """
        data = bytearray()
        data.extend(index.to_bytes(4, "big", signed=False))
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_DETECTION_RESULT, 0x00, data)

    def set_mf1_block_data(self, block_start: int, block_data: bytearray):
        """
            设置MF1的模拟卡的块数据
        :param block_start: 开始设置块数据的位置，包含此位置
        :param block_data: 要设置的块数据的字节缓冲区，可包含多个块数据，自动从 block_start 递增
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

    def set_mf1_anti_collision_res(self, sak: bytearray, atqa: bytearray, uid: bytearray):
        """
            设置MF1的模拟卡的防冲撞资源信息
        :param sak: sak字节
        :param atqa: atqa数组
        :param uid: 卡号数组
        :return:
        """
        data = bytearray()
        data.extend(sak)
        data.extend(atqa)
        data.extend(uid)
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_ANTI_COLLISION_RES, 0X00, data)

    def set_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType, name: bytes):
        """
            设置MF1的模拟卡的防冲撞资源信息
        :param slot: 卡槽号码
        :param sense_type: 场类型
        :param name: 卡槽昵称
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = bytearray()
        data.extend([SlotNumber.to_fw(slot), sense_type])
        data.extend(name)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_NICK, 0x00, data)

    def get_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType):
        """
            设置MF1的模拟卡的防冲撞资源信息
        :param slot: 卡槽号码
        :param sense_type: 场类型
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
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_ANTI_COLLISION_RES, 0x00, bytearray([1 if enabled else 0]))

    def set_mf1_write_mode(self, mode: int):
        """
        Set write mode
        """
        return self.device.send_cmd_sync(DATA_CMD_SET_MF1_WRITE_MODE, 0x00, bytearray([mode]))

    def update_slot_data_config(self):
        """
            更新卡槽的配置和数据到flash中。
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SLOT_DATA_CONFIG_SAVE, 0x00)

    def enter_dfu_mode(self):
        """
            重启进入DFU模式(bootloader)
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
        Get animation mode value
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
        return self.device.send_cmd_sync(DATA_CMD_WIPE_FDS, 0x00)


class NegativeResponseError(Exception):
    """
        Not positive response
    """


class PositiveChameleonCMD(BaseChameleonCMD):
    """
        子类重写基础指令交互实现类，针对每个指令进行单独封装结果处理
        如果结果是成功状态，那么就返回对应的数据，否则直接抛出异常
    """

    @staticmethod
    def check_status(status_ret, status_except):
        """
            检查状态码，如果在接受为成功的
        :param status_ret: 执行指令之后返回的状态码
        :param status_except: 可以认为是执行成功的状态码
        :return:
        """
        if isinstance(status_except, int):
            status_except = [status_except]
        if status_ret not in status_except:
            if status_ret in chameleon_status.Device and status_ret in chameleon_status.message:
                raise NegativeResponseError(chameleon_status.message[status_ret])
            else:
                raise NegativeResponseError(f"Not positive response and unknown status {status_ret}")
        return

    def scan_tag_14a(self):
        ret = super(PositiveChameleonCMD, self).scan_tag_14a()
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def detect_nt_distance(self, block_known, type_known, key_known):
        ret = super(PositiveChameleonCMD, self).detect_nt_distance(block_known, type_known, key_known)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def acquire_nested(self, block_known, type_known, key_known, block_target, type_target):
        ret = super(PositiveChameleonCMD, self).acquire_nested(block_known, type_known, key_known, block_target,
                                                               type_target)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def acquire_darkside(self, block_target, type_target, first_recover: int or bool, sync_max):
        ret = super(PositiveChameleonCMD, self).acquire_darkside(block_target, type_target, first_recover, sync_max)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def auth_mf1_key(self, block, type_value, key):
        ret = super(PositiveChameleonCMD, self).auth_mf1_key(block, type_value, key)
        self.check_status(ret.status, [chameleon_status.Device.HF_TAG_OK, chameleon_status.Device.MF_ERRAUTH])
        return ret

    def read_mf1_block(self, block, type_value, key):
        ret = super(PositiveChameleonCMD, self).read_mf1_block(block, type_value, key)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def write_mf1_block(self, block, type_value, key, block_data):
        ret = super(PositiveChameleonCMD, self).write_mf1_block(block, type_value, key, block_data)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def read_em_410x(self):
        ret = super(PositiveChameleonCMD, self).read_em_410x()
        self.check_status(ret.status, chameleon_status.Device.LF_TAG_OK)
        return ret

    def write_em_410x_to_t55xx(self, id_bytes: bytearray):
        ret = super(PositiveChameleonCMD, self).write_em_410x_to_t55xx(id_bytes)
        self.check_status(ret.status, chameleon_status.Device.LF_TAG_OK)
        return ret

    def set_slot_activated(self, slot_index):
        ret = super(PositiveChameleonCMD, self).set_slot_activated(slot_index)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_tag_type(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        ret = super(PositiveChameleonCMD, self).set_slot_tag_type(slot_index, tag_type)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_data_default(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        ret = super(PositiveChameleonCMD, self).set_slot_data_default(slot_index, tag_type)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_enable(self, slot_index: SlotNumber, enable: bool):
        ret = super(PositiveChameleonCMD, self).set_slot_enable(slot_index, enable)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_em410x_sim_id(self, id_bytes: bytearray):
        ret = super(PositiveChameleonCMD, self).set_em410x_sim_id(id_bytes)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_mf1_detection_enable(self, enable: bool):
        ret = super(PositiveChameleonCMD, self).set_mf1_detection_enable(enable)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def get_mf1_detection_log(self, index: int):
        ret = super(PositiveChameleonCMD, self).get_mf1_detection_log(index)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_mf1_block_data(self, block_start: int, data: bytearray):
        ret = super(PositiveChameleonCMD, self).set_mf1_block_data(block_start, data)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_mf1_anti_collision_res(self, sak: bytearray, atqa: bytearray, uid: bytearray):
        ret = super(PositiveChameleonCMD, self).set_mf1_anti_collision_res(sak, atqa, uid)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType, name: bytes):
        ret = super(PositiveChameleonCMD, self).set_slot_tag_nick_name(slot, sense_type, name)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def get_slot_tag_nick_name(self, slot: SlotNumber, sense_type: TagSenseType):
        ret = super(PositiveChameleonCMD, self).get_slot_tag_nick_name(slot, sense_type)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret


if __name__ == '__main__':
    # connect to chameleon
    dev = chameleon_com.ChameleonCom()
    dev.open("com19")
    cml = BaseChameleonCMD(dev)
    ver = cml.get_firmware_version()
    print(f"Firmware number of application: {ver}")
    chip = cml.get_device_chip_id()
    print(f"Device chip id: {chip}")

    # disconnect
    dev.close()

    # never exit
    while True:
        pass
