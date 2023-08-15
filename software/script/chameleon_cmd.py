import enum

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

DATA_CMD_LOAD_MF1_BLOCK_DATA = 4000
DATA_CMD_SET_MF1_ANTI_COLLISION_RES = 4001

DATA_CMD_SET_MF1_DETECTION_ENABLE = 4004
DATA_CMD_GET_MF1_DETECTION_COUNT = 4005
DATA_CMD_GET_MF1_DETECTION_RESULT = 4006

DATA_CMD_READ_MF1_BLOCK_DATA = 4008

DATA_CMD_SET_EM410X_EMU_ID = 5000
DATA_CMD_GET_EM410X_EMU_ID = 5001


@enum.unique
class TagSenseType(enum.IntEnum):
    # 无场感应
    TAG_SENSE_NO = 0,
    # 低频125khz场感应
    TAG_SENSE_LF = 1,
    # 高频13.56mhz场感应
    TAG_SENSE_HF = 2,


@enum.unique
class TagSpecificType(enum.IntEnum):
    # 特定的且必须存在的标志不存在的类型
    TAG_TYPE_UNKNOWN = 0
    # 125khz（ID卡）系列
    TAG_TYPE_EM410X = 1
    # Mifare系列
    TAG_TYPE_MIFARE_Mini = 2
    TAG_TYPE_MIFARE_1024 = 3
    TAG_TYPE_MIFARE_2048 = 4
    TAG_TYPE_MIFARE_4096 = 5
    # NTAG系列
    TAG_TYPE_NTAG_213 = 6
    TAG_TYPE_NTAG_215 = 7
    TAG_TYPE_NTAG_216 = 8

    @staticmethod
    def list(exclude_unknown=True):
        enum_list = list(map(int, TagSpecificType))
        if exclude_unknown:
            enum_list.remove(TagSpecificType.TAG_TYPE_UNKNOWN)
        return enum_list


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
        resp = self.device.send_cmd_sync(DATA_CMD_GET_APP_VERSION, 0x00, None)
        return int.from_bytes(resp.data, 'little')
    
    def get_device_chip_id(self) -> str:
        """
            Get device chip id
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_CHIP_ID, 0x00, None)
        return resp.data.hex()
    
    def get_device_address(self) -> str:
        """
            Get device address
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_ADDRESS, 0x00, None)
        return resp.data[::-1].hex()
    

    def is_reader_device_mode(self) -> bool:
        """
            Get device mode, reader or tag
        :return: True is reader mode, else tag mode
        """
        resp = self.device.send_cmd_sync(DATA_CMD_GET_DEVICE_MODE, 0x00, None)
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
        return self.device.send_cmd_sync(DATA_CMD_SCAN_14A_TAG, 0x00, None)

    def detect_mf1_support(self):
        """
            检测是否是mifare classic标签
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_SUPPORT_DETECT, 0x00, None)

    def detect_mf1_nt_level(self):
        """
            检测mifare classic的nt漏洞的等级
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_MF1_NT_LEVEL_DETECT, 0x00, None)

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
        return self.device.send_cmd_sync(DATA_CMD_SCAN_EM410X_TAG, 0x00, None)

    def write_em_410x_to_t55xx(self, id_bytes: bytearray):
        """
            写入EM410X卡号到T55XX中
        :param id_bytes: ID卡号
        :return:
        """
        new_key = [0x20, 0x20, 0x66, 0x66]
        old_keys = [
            [0x51, 0x24, 0x36, 0x48],
            [0x19, 0x92, 0x04, 0x27],
        ]
        if len(id_bytes) != 5:
            raise ValueError("The id bytes length must equal 5")
        data = bytearray()
        data.extend(id_bytes)
        data.extend(new_key)
        for key in old_keys:
            data.extend(key)
        return self.device.send_cmd_sync(DATA_CMD_WRITE_EM410X_TO_T5577, 0x00, data)

    def set_slot_activated(self, slot_index):
        """
            设置当前激活使用的卡槽
        :param slot_index: 卡槽索引，从 1 - 8（不是从0下标开始）
        :return:
        """
        if slot_index < 1 or slot_index > 8:
            raise ValueError("The slot index range error(1-8)")
        data = bytearray()
        data.append(slot_index - 1)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ACTIVATED, 0x00, data)

    def set_slot_tag_type(self, slot_index: int, tag_type: TagSpecificType):
        """
            设置当前卡槽的模拟卡的标签类型
            注意：此操作并不会更改flash中的数据，flash中的数据的变动仅在下次保存时更新
        :param slot_index: 卡槽号码
        :param tag_type: 标签类型
        :return:
        """
        if slot_index < 1 or slot_index > 8:
            raise ValueError("The slot index range error(1-8)")
        data = bytearray()
        data.append(slot_index - 1)
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_TYPE, 0x00, data)

    def set_slot_data_default(self, slot_index: int, tag_type: TagSpecificType):
        """
            设置指定卡槽的模拟卡的数据为缺省数据
            注意：此API会将flash中的数据一并进行设置
        :param slot_index: 卡槽号码
        :param tag_type: 要设置的缺省标签类型
        :return:
        """
        if slot_index < 1 or slot_index > 8:
            raise ValueError("The slot index range error(1-8)")
        data = bytearray()
        data.append(slot_index - 1)
        data.append(tag_type)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_DATA_DEFAULT, 0x00, data)

    def set_slot_enable(self, slot_index: int, enable: bool):
        """
            设置指定的卡槽是否使能
        :param slot_index: 卡槽号码
        :param enable: 是否使能
        :return:
        """
        if slot_index < 1 or slot_index > 8:
            raise ValueError("The slot index range error(1-8)")
        data = bytearray()
        data.append(slot_index - 1)
        data.append(0x01 if enable else 0x00)
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_ENABLE, 0X00, data)

    def set_em140x_sim_id(self, id_bytes: bytearray):
        """
            设置EM410x模拟的卡号
        :param id_bytes: 卡号的字节
        :return:
        """
        if len(id_bytes) != 5:
            raise ValueError("The id bytes length must equal 5")
        return self.device.send_cmd_sync(DATA_CMD_SET_EM410X_EMU_ID, 0x00, id_bytes)

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
        return self.device.send_cmd_sync(DATA_CMD_GET_MF1_DETECTION_COUNT, 0x00, None)

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
        return self.device.send_cmd_sync(DATA_CMD_LOAD_MF1_BLOCK_DATA, 0x00, data)

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
    
    def set_slot_tag_nick_name(self, slot: int, sense_type: int, name: str):
        """
            设置MF1的模拟卡的防冲撞资源信息
        :param slot: 卡槽号码
        :param sense_type: 场类型
        :param name: 卡槽昵称
        :return:
        """
        data = bytearray()
        data.extend([slot, sense_type])
        data.extend(name.encode(encoding="gbk"))
        return self.device.send_cmd_sync(DATA_CMD_SET_SLOT_TAG_NICK, 0x00, data)
    
    def get_slot_tag_nick_name(self, slot: int, sense_type: int):
        """
            设置MF1的模拟卡的防冲撞资源信息
        :param slot: 卡槽号码
        :param sense_type: 场类型
        :param name: 卡槽昵称
        :return:
        """
        data = bytearray()
        data.extend([slot, sense_type])
        return self.device.send_cmd_sync(DATA_CMD_GET_SLOT_TAG_NICK, 0x00, data)
    
    def update_slot_data_config(self):
        """
            更新卡槽的配置和数据到flash中。
        :return:
        """
        return self.device.send_cmd_sync(DATA_CMD_SLOT_DATA_CONFIG_SAVE, 0x00, None)

    def enter_dfu_mode(self):
        """
            重启进入DFU模式(bootloader)
        :return:
        """
        return self.device.send_cmd_auto(DATA_CMD_ENTER_BOOTLOADER, 0x00, close=True)


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
        ret = super(PositiveChameleonCMD, self).acquire_nested(
            block_known, type_known, key_known, block_target, type_target)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def acquire_darkside(self, block_target, type_target, first_recover: int or bool, sync_max):
        ret = super(PositiveChameleonCMD, self).acquire_darkside(block_target, type_target, first_recover, sync_max)
        self.check_status(ret.status, chameleon_status.Device.HF_TAG_OK)
        return ret

    def auth_mf1_key(self, block, type_value, key):
        ret = super(PositiveChameleonCMD, self).auth_mf1_key(block, type_value, key)
        self.check_status(ret.status, [
            chameleon_status.Device.HF_TAG_OK,
            chameleon_status.Device.MF_ERRAUTH,
        ])
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

    def set_slot_tag_type(self, slot_index: int, tag_type: TagSpecificType):
        ret = super(PositiveChameleonCMD, self).set_slot_tag_type(slot_index, tag_type)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_data_default(self, slot_index: int, tag_type: TagSpecificType):
        ret = super(PositiveChameleonCMD, self).set_slot_data_default(slot_index, tag_type)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_slot_enable(self, slot_index: int, enable: bool):
        ret = super(PositiveChameleonCMD, self).set_slot_enable(slot_index, enable)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret

    def set_em140x_sim_id(self, id_bytes: bytearray):
        ret = super(PositiveChameleonCMD, self).set_em140x_sim_id(id_bytes)
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

    def set_mf1_anti_collision_res(self, sak: int, atqa: bytearray, uid: bytearray):
        ret = super(PositiveChameleonCMD, self).set_mf1_anti_collision_res(sak, atqa, uid)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret
    
    def set_slot_tag_nick_name(self, slot: int, sense_type: int, name: str):
        ret = super(PositiveChameleonCMD, self).set_slot_tag_nick_name(slot, sense_type, name)
        self.check_status(ret.status, chameleon_status.Device.STATUS_DEVICE_SUCCESS)
        return ret
    
    def get_slot_tag_nick_name(self, slot: int, sense_type: int):
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
    id = cml.get_device_chip_id()
    print(f"Device chip id: {id}")


    # disconnect
    dev.close()
    
    # nerver exit
    while True: pass
