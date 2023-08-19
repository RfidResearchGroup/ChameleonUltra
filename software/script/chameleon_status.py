class MetaDevice(type):
    def __iter__(self):
        for attr in dir(self):
            if not attr.startswith("__"):
                yield attr

    def __contains__(self, item):
        for field in self.__dict__:
            val = self.__dict__[field]
            if isinstance(val, int):
                if val == item:
                    return True
        return False


class Device(metaclass=MetaDevice):
    HF_TAG_OK = 0x00  # IC卡操作成功
    HF_TAG_NO = 0x01  # 没有发现IC卡
    HF_ERRSTAT = 0x02  # IC卡通信异常
    HF_ERRCRC = 0x03  # IC卡通信校验异常
    HF_COLLISION = 0x04  # IC卡冲突
    HF_ERRBCC = 0x05  # IC卡BCC错误
    MF_ERRAUTH = 0x06  # MF卡验证失败
    HF_ERRPARITY = 0x07  # IC卡奇偶校验错误

    DARKSIDE_CANT_FIXED_NT = 0x20  # Darkside，无法固定随机数，这个情况可能出现在UID卡上
    DARKSIDE_LUCK_AUTH_OK = 0x21  # Darkside，直接验证成功了，可能刚好密钥是空的
    DARKSIDE_NACK_NO_SEND = 0x22  # Darkside，卡片不响应nack，可能是一张修复了nack逻辑漏洞的卡片
    DARKSIDE_TAG_CHANGED = 0x23  # Darkside，在运行darkside的过程中出现了卡片切换，可能信号问题，或者真的是两张卡迅速切换了
    NESTED_TAG_IS_STATIC = 0x24  # Nested，检测到卡片应答的随机数是固定的
    NESTED_TAG_IS_HARD = 0x25  # Nested，检测到卡片应答的随机数是不可预测的

    LF_TAG_OK = 0x40  # 低频卡的一些操作成功！
    EM410X_TAG_NO_FOUND = 0x41  # 无法搜索到有效的EM410X标签

    STATUS_PAR_ERR = 0x60  # BLE指令传递的参数错误，或者是调用某些函数传递的参数错误
    STATUS_DEVICE_MODE_ERROR = 0x66  # 当前设备所处的模式错误，无法调用对应的API
    STATUS_INVALID_CMD = 0x67  # 无效的指令
    STATUS_DEVICE_SUCCESS = 0x68  # 设备相关操作成功执行
    STATUS_NOT_IMPLEMENTED = 0x69  # 调用了某些未实现的操作，属于开发者遗漏的错误
    STATUS_FLASH_WRITE_FAIL = 0x70  # flash写入失败
    STATUS_FLASH_READ_FAIL = 0x71  # flash读取失败


message = {
    Device.HF_TAG_OK: "HF tag operation succeeded",
    Device.HF_TAG_NO: "HF tag no found or lost",
    Device.HF_ERRSTAT: "HF tag status error",
    Device.HF_ERRCRC: "HF tag data crc error",
    Device.HF_COLLISION: "HF tag collision",
    Device.HF_ERRBCC: "HF tag uid bcc error",
    Device.MF_ERRAUTH: "HF tag auth fail",
    Device.HF_ERRPARITY: "HF tag data parity error",

    Device.DARKSIDE_CANT_FIXED_NT: "Darkside Can't select a nt(PRNG is unpredictable)",
    Device.DARKSIDE_LUCK_AUTH_OK: "Darkside try to recover a default key",
    Device.DARKSIDE_NACK_NO_SEND: "Darkside can't make tag response nack(enc)",
    Device.DARKSIDE_TAG_CHANGED: "Darkside running, can't change tag",
    Device.NESTED_TAG_IS_STATIC: "StaticNested tag, not weak nested",
    Device.NESTED_TAG_IS_HARD: "HardNested tag, not weak nested",

    Device.LF_TAG_OK: "LF tag operation succeeded",
    Device.EM410X_TAG_NO_FOUND: "EM410x tag no found",

    Device.STATUS_PAR_ERR: "API request fail, param error",
    Device.STATUS_DEVICE_MODE_ERROR: "API request fail, device mode error",
    Device.STATUS_INVALID_CMD: "API request fail, cmd invalid",
    Device.STATUS_DEVICE_SUCCESS: "Device operation succeeded",
    Device.STATUS_NOT_IMPLEMENTED: "Some api not implemented",
    Device.STATUS_FLASH_WRITE_FAIL: "Flash write failed",
    Device.STATUS_FLASH_READ_FAIL: "Flash read failed"
}
