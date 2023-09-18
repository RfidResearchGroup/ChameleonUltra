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

    def __getitem__(self, item):
        for field in self.__dict__:
            val = self.__dict__[field]
            if isinstance(val, int):
                if val == item:
                    return field
        return False


class Device(metaclass=MetaDevice):
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
    STATUS_PAR_ERR = 0x60
    # The mode of the current device is wrong, and the corresponding API cannot be called
    STATUS_DEVICE_MODE_ERROR = 0x66
    STATUS_INVALID_CMD = 0x67
    STATUS_DEVICE_SUCCESS = 0x68
    STATUS_NOT_IMPLEMENTED = 0x69
    STATUS_FLASH_WRITE_FAIL = 0x70
    STATUS_FLASH_READ_FAIL = 0x71


message = {
    Device.HF_TAG_OK: "HF tag operation succeeded",
    Device.HF_TAG_NO: "HF tag no found or lost",
    Device.HF_ERR_STAT: "HF tag status error",
    Device.HF_ERR_CRC: "HF tag data crc error",
    Device.HF_COLLISION: "HF tag collision",
    Device.HF_ERR_BCC: "HF tag uid bcc error",
    Device.MF_ERR_AUTH: "HF tag auth fail",
    Device.HF_ERR_PARITY: "HF tag data parity error",
    Device.HF_ERR_ATS: "HF tag was supposed to send ATS but didn't",

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
