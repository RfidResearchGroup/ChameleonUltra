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
    HF_TAG_OK = 0x00     # IC card operation is successful
    HF_TAG_NO = 0x01     # IC card not found
    HF_ERRSTAT = 0x02    # Abnormal IC card communication
    HF_ERRCRC = 0x03     # IC card communication verification abnormal
    HF_COLLISION = 0x04  # IC card conflict
    HF_ERRBCC = 0x05     # IC card BCC error
    MF_ERRAUTH = 0x06    # MF card verification failed
    HF_ERRPARITY = 0x07  # IC card parity error

    # Darkside, the random number cannot be fixed, this situation may appear on the UID card
    DARKSIDE_CANT_FIXED_NT = 0x20
    # Darkside, the direct verification is successful, maybe the key is empty
    DARKSIDE_LUCK_AUTH_OK = 0x21
    # Darkside, the card doesn't respond to nack, probably a card that fixes the nack logic bug
    DARKSIDE_NACK_NO_SEND = 0x22
    # Darkside, there is a card switching during the running of darkside,
    # maybe there is a signal problem, or the two cards really switched quickly
    DARKSIDE_TAG_CHANGED = 0x23
    # Nested, it is detected that the random number of the card response is fixed
    NESTED_TAG_IS_STATIC = 0x24
    # Nested, detected nonce for card response is unpredictable
    NESTED_TAG_IS_HARD = 0x25

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
