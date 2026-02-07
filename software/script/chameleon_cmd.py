import struct
import ctypes
from typing import Union

import chameleon_com
from chameleon_utils import expect_response, reconstruct_full_nt, parity_to_str
from chameleon_enum import Command, SlotNumber, Status, TagSenseType, TagSpecificType
from chameleon_enum import ButtonPressFunction, ButtonType, MifareClassicDarksideStatus
from chameleon_enum import MfcKeyType, MfcValueBlockOperator

CURRENT_VERSION_SETTINGS = 5

new_key = b'\x20\x20\x66\x66'
old_keys = [b'\x51\x24\x36\x48', b'\x19\x92\x04\x27']

class ChameleonCMD:
    """
        Chameleon cmd function
    """

    def __init__(self, chameleon: chameleon_com.ChameleonCom):
        """
        :param chameleon: chameleon instance, @see chameleon_device.Chameleon
        """
        self.device = chameleon

    @expect_response(Status.SUCCESS)
    def get_app_version(self):
        """
            Get firmware version number(application)
        """
        resp = self.device.send_cmd_sync(Command.GET_APP_VERSION)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('!BB', resp.data)
        # older protocol, must upgrade!
        if resp.status == 0 and resp.data == b'\x00\x01':
            print("Chameleon does not understand new protocol. Please update firmware")
            return chameleon_com.Response(cmd=Command.GET_APP_VERSION,
                                          status=Status.NOT_IMPLEMENTED)
        return resp

    @expect_response(Status.SUCCESS)
    def get_device_chip_id(self):
        """
            Get device chip id
        """
        resp = self.device.send_cmd_sync(Command.GET_DEVICE_CHIP_ID)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data.hex()
        return resp

    @expect_response(Status.SUCCESS)
    def get_device_address(self):
        """
            Get device address
        """
        resp = self.device.send_cmd_sync(Command.GET_DEVICE_ADDRESS)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data.hex()
        return resp

    @expect_response(Status.SUCCESS)
    def get_git_version(self):
        resp = self.device.send_cmd_sync(Command.GET_GIT_VERSION)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data.decode('utf-8')
        return resp

    @expect_response(Status.SUCCESS)
    def get_device_mode(self):
        resp = self.device.send_cmd_sync(Command.GET_DEVICE_MODE)
        if resp.status == Status.SUCCESS:
            resp.parsed, = struct.unpack('!?', resp.data)
        return resp

    def is_device_reader_mode(self) -> bool:
        """
            Get device mode, reader or tag.

        :return: True is reader mode, else tag mode
        """
        return self.get_device_mode()

    # Note: Will return NOT_IMPLEMENTED if one tries to set reader mode on Lite
    @expect_response(Status.SUCCESS)
    def change_device_mode(self, mode):
        data = struct.pack('!B', mode)
        return self.device.send_cmd_sync(Command.CHANGE_DEVICE_MODE, data)

    def set_device_reader_mode(self, reader_mode: bool = True):
        """
            Change device mode, reader or tag.

        :param reader_mode: True if reader mode, False if tag mode.
        :return:
        """
        self.change_device_mode(reader_mode)

    @expect_response(Status.HF_TAG_OK)
    def hf14a_scan(self):
        """
        14a tags in the scanning field.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.HF14A_SCAN)
        if resp.status == Status.HF_TAG_OK:
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
            resp.parsed = data
        return resp

    def mf1_detect_support(self):
        """
        Detect whether it is mifare classic tag.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.MF1_DETECT_SUPPORT)
        return resp.status == Status.HF_TAG_OK

    @expect_response(Status.HF_TAG_OK)
    def mf1_detect_prng(self):
        """
        Detect mifare Class of classic nt vulnerabilities.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.MF1_DETECT_PRNG)
        if resp.status == Status.HF_TAG_OK:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_detect_nt_dist(self, block_known, type_known, key_known):
        """
        Detect the random number distance of the card.

        :return:
        """
        data = struct.pack('!BB6s', type_known, block_known, key_known)
        resp = self.device.send_cmd_sync(Command.MF1_DETECT_NT_DIST, data)
        if resp.status == Status.HF_TAG_OK:
            uid, dist = struct.unpack('!II', resp.data)
            resp.parsed = {'uid': uid, 'dist': dist}
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_nested_acquire(self, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the key NT parameters needed for Nested decryption
        :return:
        """
        data = struct.pack('!BB6sBB', type_known, block_known, key_known, type_target, block_target)
        resp = self.device.send_cmd_sync(Command.MF1_NESTED_ACQUIRE, data)
        if resp.status == Status.HF_TAG_OK:
            resp.parsed = [{'nt': nt, 'nt_enc': nt_enc, 'par': par}
                           for nt, nt_enc, par in struct.iter_unpack('!IIB', resp.data)]
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_darkside_acquire(self, block_target, type_target, first_recover: Union[int, bool], sync_max):
        """
        Collect the key parameters needed for Darkside decryption.

        :param block_target:
        :param type_target:
        :param first_recover:
        :param sync_max:
        :return:
        """
        data = struct.pack('!BBBB', type_target, block_target, first_recover, sync_max)
        resp = self.device.send_cmd_sync(Command.MF1_DARKSIDE_ACQUIRE, data, timeout=sync_max * 10)
        if resp.status == Status.HF_TAG_OK:
            if resp.data[0] == MifareClassicDarksideStatus.OK:
                darkside_status, uid, nt1, par, ks1, nr, ar = struct.unpack('!BIIQQII', resp.data)
                resp.parsed = (darkside_status, {'uid': uid, 'nt1': nt1, 'par': par, 'ks1': ks1, 'nr': nr, 'ar': ar})
            else:
                resp.parsed = (resp.data[0],)
        return resp

    @expect_response([Status.HF_TAG_OK, Status.MF_ERR_AUTH])
    def mf1_auth_one_key_block(self, block, type_value: MfcKeyType, key):
        """
        Verify the mf1 key, only verify the specified type of key for a single sector.

        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = struct.pack('!BB6s', type_value, block, key)
        resp = self.device.send_cmd_sync(Command.MF1_AUTH_ONE_KEY_BLOCK, data)
        resp.parsed = resp.status == Status.HF_TAG_OK
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_read_one_block(self, block, type_value: MfcKeyType, key):
        """
        Read one mf1 block.

        :param block:
        :param type_value:
        :param key:
        :return:
        """
        data = struct.pack('!BB6s', type_value, block, key)
        resp = self.device.send_cmd_sync(Command.MF1_READ_ONE_BLOCK, data)
        resp.parsed = resp.data
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_write_one_block(self, block, type_value: MfcKeyType, key, block_data):
        """
        Write mf1 single block.

        :param block:
        :param type_value:
        :param key:
        :param block_data:
        :return:
        """
        data = struct.pack('!BB6s16s', type_value, block, key, block_data)
        resp = self.device.send_cmd_sync(Command.MF1_WRITE_ONE_BLOCK, data)
        resp.parsed = resp.status == Status.HF_TAG_OK
        return resp

    @expect_response(Status.HF_TAG_OK)
    def hf14a_raw(self, options, resp_timeout_ms=100, data=[], bitlen=None):
        """
        Send raw cmd to 14a tag.

        :param options:
        :param resp_timeout_ms:
        :param data:
        :param bit_owned_by_the_last_byte:
        :return:
        """

        class CStruct(ctypes.BigEndianStructure):
            _fields_ = [
                ("activate_rf_field", ctypes.c_uint8, 1),
                ("wait_response", ctypes.c_uint8, 1),
                ("append_crc", ctypes.c_uint8, 1),
                ("auto_select", ctypes.c_uint8, 1),
                ("keep_rf_field", ctypes.c_uint8, 1),
                ("check_response_crc", ctypes.c_uint8, 1),
                ("reserved", ctypes.c_uint8, 2),
            ]

        cs = CStruct()
        cs.activate_rf_field = options['activate_rf_field']
        cs.wait_response = options['wait_response']
        cs.append_crc = options['append_crc']
        cs.auto_select = options['auto_select']
        cs.keep_rf_field = options['keep_rf_field']
        cs.check_response_crc = options['check_response_crc']

        if bitlen is None:
            bitlen = len(data) * 8  # bits = bytes * 8(bit)
        else:
            if len(data) == 0:
                raise ValueError(f'bitlen={bitlen} but missing data')
            if not ((len(data) - 1) * 8 < bitlen <= len(data) * 8):
                raise ValueError(f'bitlen={bitlen} incompatible with provided data ({len(data)} bytes), '
                                 f'must be between {((len(data) - 1) * 8)+1} and {len(data) * 8} included')

        data = bytes(cs)+struct.pack(f'!HH{len(data)}s', resp_timeout_ms, bitlen, bytearray(data))
        resp = self.device.send_cmd_sync(Command.HF14A_RAW, data, timeout=(resp_timeout_ms // 1000) + 1)
        resp.parsed = resp.data
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_manipulate_value_block(self, src_block, src_type: MfcKeyType, src_key, operator: MfcValueBlockOperator, operand, dst_block, dst_type: MfcKeyType, dst_key):
        """
        1. Increment: increments value from source block and write to dest block
        2. Decrement: decrements value from source block and write to dest block
        3. Restore: copy value from source block and write to dest block


        :param src_block:
        :param src_type:
        :param src_key:
        :param operator:
        :param operand:
        :param dst_block:
        :param dst_type:
        :param dst_key:
        :return:
        """
        data = struct.pack('!BB6sBiBB6s', src_type, src_block, src_key, operator, operand, dst_type, dst_block, dst_key)
        resp = self.device.send_cmd_sync(Command.MF1_MANIPULATE_VALUE_BLOCK, data)
        resp.parsed = resp.status == Status.HF_TAG_OK
        return resp

    @expect_response([Status.HF_TAG_OK, Status.HF_TAG_NO])
    def mf1_check_keys_of_sectors(self, mask: bytes, keys: list[bytes]):
        """
        Check keys of sectors.
        :return:
        """
        if len(mask) != 10:
            raise ValueError("len(mask) should be 10")
        if len(keys) < 1 or len(keys) > 83:
            raise ValueError("Invalid len(keys)")
        data = struct.pack(f'!10s{6*len(keys)}s', mask, b''.join(keys))

        bitsCnt = 80  # maximum sectorKey_to_be_checked
        for b in mask:
            while b > 0:
                [bitsCnt, b] = [bitsCnt - (b & 0b1), b >> 1]
        if bitsCnt < 1:
            # All sectorKey is masked
            return chameleon_com.Response(
                cmd=Command.MF1_CHECK_KEYS_OF_SECTORS,
                status=Status.HF_TAG_OK,
                parsed={'status': Status.HF_TAG_OK},
            )
        # base timeout: 1s
        # auth: len(keys) * sectorKey_to_be_checked * 0.1s
        # read keyB from trailer block: 0.1s
        timeout = 1 + (bitsCnt + 1) * len(keys) * 0.1

        resp = self.device.send_cmd_sync(Command.MF1_CHECK_KEYS_OF_SECTORS, data, timeout=timeout)
        resp.parsed = {'status': resp.status}
        if len(resp.data) == 490:
            found = ''.join([format(i, '08b') for i in resp.data[0:10]])
            # print(f'{found = }')
            resp.parsed.update({
                'found': resp.data[0:10],
                'sectorKeys': {k: resp.data[6 * k + 10:6 * k + 16] for k, v in enumerate(found) if v == '1'}
            })
        return resp

    @expect_response([Status.HF_TAG_OK, Status.HF_TAG_NO, Status.MF_ERR_AUTH])
    def mf1_check_keys_on_block(self, block: int, key_type: int, keys: list[bytes]):
        if key_type not in [0x60, 0x61]:
            raise ValueError("Wrong key type")
        if len(keys) < 1 or len(keys) > 83:
            raise ValueError("Invalid len(keys)")
        data = struct.pack(f'!BBB{6*len(keys)}s', block, key_type, len(keys), b''.join(keys))

        resp = self.device.send_cmd_sync(Command.MF1_CHECK_KEYS_ON_BLOCK, data, timeout=10)

        if resp.status == Status.HF_TAG_OK and len(resp.data) == 7:
            found, key = struct.unpack('!B6s', resp.data)
            if found:
                resp.parsed = key

        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_static_nested_acquire(self, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the key NT parameters needed for StaticNested decryption
        :return:
        """
        data = struct.pack('!BB6sBB', type_known, block_known, key_known, type_target, block_target)
        resp = self.device.send_cmd_sync(Command.MF1_STATIC_NESTED_ACQUIRE, data)
        if resp.status == Status.HF_TAG_OK:
            resp.parsed = {
                'uid': struct.unpack('!I', resp.data[0:4])[0],
                'nts': [
                    {
                        'nt': nt,
                        'nt_enc': nt_enc
                    } for nt, nt_enc in struct.iter_unpack('!II', resp.data[4:])
                ]
            }
        return resp

    @expect_response(Status.HF_TAG_OK)
    def mf1_hard_nested_acquire(self, slow, block_known, type_known, key_known, block_target, type_target):
        """
        Collect the NT_ENC list for HardNested decryption
        :return:
        """
        data = struct.pack('!BBB6sBB', slow, type_known, block_known, key_known, type_target, block_target)
        resp = self.device.send_cmd_sync(Command.MF1_HARDNESTED_ACQUIRE, data, timeout=30)
        if resp.status == Status.HF_TAG_OK:
            resp.parsed = resp.data  # we can return the raw nonces bytes
        return resp

    @expect_response([Status.HF_TAG_OK, Status.HF_TAG_NO])
    def mf1_static_encrypted_nested_acquire(self, backdoor_key, sector_count, starting_sector):
        data = struct.pack('!6sBB', backdoor_key, sector_count, starting_sector)
        resp = self.device.send_cmd_sync(Command.MF1_ENC_NESTED_ACQUIRE, data, timeout=30)
        if resp.status == Status.HF_TAG_OK:
            resp.parsed = {
                'uid': struct.unpack('!I', resp.data[0:4])[0],
                'nts': {
                    'a': [],
                    'b': []
                }
            }

            i = 4

            while i < len(resp.data):
                resp.parsed['nts']['a'].append(
                    {
                        'nt': reconstruct_full_nt(resp.data, i),
                        'nt_enc': int.from_bytes(resp.data[i + 3: i + 7], byteorder='big'),
                        'parity': parity_to_str(resp.data[i + 2])
                    }
                )

                resp.parsed['nts']['b'].append(
                    {
                        'nt': reconstruct_full_nt(resp.data, i + 7),
                        'nt_enc': int.from_bytes(resp.data[i + 10: i + 14], byteorder='big'),
                        'parity': parity_to_str(resp.data[i + 9])
                    }
                )

                i += 14
        return resp

    @expect_response(Status.SUCCESS)
    def hf14a_get_config(self):
        """
        Get hf 14a config

        :return:
        """
        resp = self.device.send_cmd_sync(Command.HF14A_GET_CONFIG)
        if resp.status == Status.SUCCESS:
            bcc, cl2, cl3, rats = struct.unpack('!bbbb', resp.data)
            resp.parsed = {'bcc': bcc,
                           'cl2': cl2,
                           'cl3': cl3,
                           'rats': rats}
        return resp

    @expect_response(Status.SUCCESS)
    def hf14a_set_config(self, data):
        """
        Set hf 14a config

        :return:
        """
        data = struct.pack('!bbbb', data['bcc'], data['cl2'], data['cl3'], data['rats'])
        return self.device.send_cmd_sync(Command.HF14A_SET_CONFIG, data)

    @expect_response(Status.LF_TAG_OK)
    def em410x_scan(self):
        """
        Read the card number of EM410X.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.EM410X_SCAN)
        if resp.status == Status.LF_TAG_OK:
            tag_type = struct.unpack('!H', resp.data[:2])[0]
            if tag_type == TagSpecificType.EM410X_ELECTRA:
                fmt = '!H13s'
            else:
                fmt = '!H5s'
            resp.parsed = struct.unpack(fmt, resp.data[:struct.calcsize(fmt)]) # tag type + uid
        return resp

    @expect_response(Status.LF_TAG_OK)
    def em410x_write_to_t55xx(self, id_bytes: bytes):
        """
        Write EM410X card number into T55XX.

        :param id_bytes: ID card number
        :return:
        """
        if len(id_bytes) == 5:
            data = struct.pack(f'!5s4s{4*len(old_keys)}s', id_bytes, new_key, b''.join(old_keys))
            return self.device.send_cmd_sync(Command.EM410X_WRITE_TO_T55XX, data)
        if len(id_bytes) == 13:
            data = struct.pack(f'!13s4s{4*len(old_keys)}s', id_bytes, new_key, b''.join(old_keys))
            return self.device.send_cmd_sync(Command.EM410X_ELECTRA_WRITE_TO_T55XX, data)
        raise ValueError("The id bytes length must equal 5 (EM410X) or 13 (Electra)")

    @expect_response(Status.LF_TAG_OK)
    def hidprox_scan(self, format: int):
        """
        Read the length, facility code and card number of HID Prox.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.HIDPROX_SCAN, struct.pack('!B', format))
        if resp.status == Status.LF_TAG_OK:
            resp.parsed = struct.unpack('>BIBIBH', resp.data[:13])
        return resp

    @expect_response(Status.LF_TAG_OK)
    def hidprox_write_to_t55xx(self, id_bytes: bytes):
        """
        Write HID Prox card number into T55XX.

        :param id_bytes: ID card number
        :return:
        """
        if len(id_bytes) != 13:
            raise ValueError("The id bytes length must equal 13")
        data = struct.pack(f'!13s4s{4*len(old_keys)}s', id_bytes, new_key, b''.join(old_keys))
        return self.device.send_cmd_sync(Command.HIDPROX_WRITE_TO_T55XX, data)

    @expect_response(Status.LF_TAG_OK)
    def viking_scan(self):
        """
        Read the card number of Viking.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.VIKING_SCAN)
        if resp.status == Status.LF_TAG_OK:
            resp.parsed = resp.data # uid
        return resp

    @expect_response(Status.LF_TAG_OK)
    def viking_write_to_t55xx(self, id_bytes: bytes):
        """
        Write Viking card number into T55XX.

        :param id_bytes: ID card number
        :return:
        """
        if len(id_bytes) != 4:
            raise ValueError("The id bytes length must equal 4")
        data = struct.pack(f'!4s4s{4*len(old_keys)}s', id_bytes, new_key, b''.join(old_keys))
        return self.device.send_cmd_sync(Command.VIKING_WRITE_TO_T55XX, data)

    @expect_response(Status.LF_TAG_OK)
    def adc_generic_read(self):
        """
        Read the ADC when the field is on.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.ADC_GENERIC_READ, None)
        if resp.status == Status.LF_TAG_OK:
            resp.parsed = resp.data
        return resp

    @expect_response(Status.SUCCESS)
    def get_slot_info(self):
        """
        Get slots info.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.GET_SLOT_INFO)
        if resp.status == Status.SUCCESS:
            resp.parsed = [{'hf': hf, 'lf': lf}
                           for hf, lf in struct.iter_unpack('!HH', resp.data)]
        return resp

    @expect_response(Status.SUCCESS)
    def get_active_slot(self):
        """
        Get selected slot.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.GET_ACTIVE_SLOT)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def set_active_slot(self, slot_index: SlotNumber):
        """
        Set the card slot currently active for use.

        :param slot_index: Card slot index
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!B', SlotNumber.to_fw(slot_index))
        return self.device.send_cmd_sync(Command.SET_ACTIVE_SLOT, data)

    @expect_response(Status.SUCCESS)
    def set_slot_tag_type(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
        Set the label type of the emulated card of the current card slot
        Note: This operation will not change the data in the flash,
        and the change of the data in the flash will only be updated at the next save.

        :param slot_index:  Card slot number
        :param tag_type:  label type
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BH', SlotNumber.to_fw(slot_index), tag_type)
        return self.device.send_cmd_sync(Command.SET_SLOT_TAG_TYPE, data)

    @expect_response(Status.SUCCESS)
    def delete_slot_sense_type(self, slot_index: SlotNumber, sense_type: TagSenseType):
        """
        Delete a sense type for a specific slot.

        :param slot_index: Slot index
        :param sense_type: Sense type to disable
        :return:
        """
        data = struct.pack('!BB', SlotNumber.to_fw(slot_index), sense_type)
        return self.device.send_cmd_sync(Command.DELETE_SLOT_SENSE_TYPE, data)

    @expect_response(Status.SUCCESS)
    def set_slot_data_default(self, slot_index: SlotNumber, tag_type: TagSpecificType):
        """
        Set the data of the emulated card in the specified card slot as the default data
        Note: This API will set the data in the flash together.

        :param slot_index: Card slot number
        :param tag_type:  The default label type to set
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BH', SlotNumber.to_fw(slot_index), tag_type)
        return self.device.send_cmd_sync(Command.SET_SLOT_DATA_DEFAULT, data)

    @expect_response(Status.SUCCESS)
    def set_slot_enable(self, slot_index: SlotNumber, sense_type: TagSenseType, enabled: bool):
        """
        Set whether the specified card slot is enabled.

        :param slot_index: Card slot number
        :param enable: Whether to enable
        :return:
        """
        # SlotNumber() will raise error for us if slot_index not in slot range
        data = struct.pack('!BBB', SlotNumber.to_fw(slot_index), sense_type, enabled)
        return self.device.send_cmd_sync(Command.SET_SLOT_ENABLE, data)

    def _get_active_lf_tag_type(self) -> TagSpecificType:
        slotinfo = self.get_slot_info()
        active_slot = SlotNumber.from_fw(self.get_active_slot())
        lf_tag_value = slotinfo[active_slot - 1]['lf']
        return TagSpecificType(lf_tag_value)

    @expect_response(Status.SUCCESS)
    def em410x_set_emu_id(self, id: bytes):
        """
        Set the card number emulated by EM410x.

        :param id_bytes: byte of the card number
        :return:
        """
        lf_tag_type = self._get_active_lf_tag_type()
        if lf_tag_type == TagSpecificType.EM410X_ELECTRA:
            expected_len = 13
        elif lf_tag_type == TagSpecificType.EM410X:
            expected_len = 5
        else:
            raise ValueError(f"Active LF slot type {lf_tag_type} is not EM410X")

        if len(id) != expected_len:
            raise ValueError(f"The id bytes length must equal {expected_len}")

        data = struct.pack(f'!{expected_len}s', id)
        return self.device.send_cmd_sync(Command.EM410X_SET_EMU_ID, data)

    @expect_response(Status.SUCCESS)
    def em410x_get_emu_id(self):
        """
        Get the emulated EM410x card id
        """
        resp = self.device.send_cmd_sync(Command.EM410X_GET_EMU_ID)
        if resp.status == Status.SUCCESS:
            data = resp.data
            id_bytes = data
            tag_type = None

            if len(data) >= 2:
                try:
                    candidate = TagSpecificType(int.from_bytes(data[:2], byteorder='big'))
                except ValueError:
                    candidate = None

                if candidate in (TagSpecificType.EM410X, TagSpecificType.EM410X_ELECTRA):
                    expected_len = 13 if candidate == TagSpecificType.EM410X_ELECTRA else 5
                    if len(data) == expected_len + 2:
                        tag_type = candidate
                        id_bytes = data[2:2 + expected_len]

            if tag_type is None:
                lf_tag_type = self._get_active_lf_tag_type()
                if lf_tag_type == TagSpecificType.EM410X_ELECTRA:
                    expected_len = 13
                elif lf_tag_type == TagSpecificType.EM410X:
                    expected_len = 5
                else:
                    expected_len = len(data)
                id_bytes = data[:expected_len]

            resp.parsed = id_bytes
        return resp

    @expect_response(Status.SUCCESS)
    def hidprox_set_emu_id(self, id: bytes):
        """
        Set the card number emulated by HID Prox.

        :param id_bytes: byte of the card number
        :return:
        """
        if len(id) != 13:
            raise ValueError("The id bytes length must equal 13")
        return self.device.send_cmd_sync(Command.HIDPROX_SET_EMU_ID, id)

    @expect_response(Status.SUCCESS)
    def hidprox_get_emu_id(self):
        """
        Get the emulated HID Prox card id
        """
        resp = self.device.send_cmd_sync(Command.HIDPROX_GET_EMU_ID)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('>BIBIBH', resp.data[:13])
        return resp

    @expect_response(Status.SUCCESS)
    def viking_set_emu_id(self, id: bytes):
        """
        Set the card number emulated by Viking.

        :param id_bytes: byte of the card number
        :return:
        """
        if len(id) != 4:
            raise ValueError("The id bytes length must equal 4")
        data = struct.pack('4s', id)
        return self.device.send_cmd_sync(Command.VIKING_SET_EMU_ID, data)

    @expect_response(Status.SUCCESS)
    def viking_get_emu_id(self):
        """
        Get the emulated Viking card id
        """
        resp = self.device.send_cmd_sync(Command.VIKING_GET_EMU_ID)
        resp.parsed = resp.data
        return resp

    @expect_response(Status.SUCCESS)
    def mf1_set_detection_enable(self, enabled: bool):
        """
        Set whether to enable the detection of the current card slot.

        :param enable: Whether to enable
        :return:
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF1_SET_DETECTION_ENABLE, data)

    @expect_response(Status.SUCCESS)
    def mf1_get_detection_count(self):
        """
        Get the statistics of the current detection records.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.MF1_GET_DETECTION_COUNT)
        if resp.status == Status.SUCCESS:
            resp.parsed, = struct.unpack('!I', resp.data)
        return resp

    @expect_response(Status.SUCCESS)
    def mf1_get_detection_log(self, index: int):
        """
        Get detection logs from the specified index position.

        :param index: start index
        :return:
        """
        data = struct.pack('!I', index)
        resp = self.device.send_cmd_sync(Command.MF1_GET_DETECTION_LOG, data)
        if resp.status == Status.SUCCESS:
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
            resp.parsed = result_list
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_detection_enable(self):
        """
        Get whether NTAG password detection is enabled.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_DETECTION_ENABLE)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('!B', resp.data)[0] == 1
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_set_detection_enable(self, enabled: bool):
        """
        Set whether to enable NTAG password detection.

        :param enable: Whether to enable
        :return:
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF0_NTAG_SET_DETECTION_ENABLE, data)

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_detection_count(self):
        """
        Get the statistics of the current NTAG password detection records.

        :return:
        """
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_DETECTION_COUNT)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('!I', resp.data)[0]
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_detection_log(self, index: int):
        """
        Get NTAG password detection logs from the specified index position.

        :param index: start index
        :return:
        """
        data = struct.pack('!I', index)
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_DETECTION_LOG, data)
        if resp.status == Status.SUCCESS:
            # convert - each log entry is just a 4-byte password
            result_list = []
            pos = 0
            while pos < len(resp.data):
                password = resp.data[pos:pos+4]
                result_list.append({
                    'password': password.hex()
                })
                pos += 4
            resp.parsed = result_list
        return resp

    @expect_response(Status.SUCCESS)
    def mf1_write_emu_block_data(self, block_start: int, block_data: bytes):
        """
        Set the block data of the analog card of MF1.

        :param block_start:  Start setting the location of block data, including this location
        :param block_data:  The byte buffer of the block data to be set can contain multiple block data,
                            automatically from block_start  increment
        :return:
        """
        data = struct.pack(f'!B{len(block_data)}s', block_start, block_data)
        return self.device.send_cmd_sync(Command.MF1_WRITE_EMU_BLOCK_DATA, data)

    @expect_response(Status.SUCCESS)
    def mf1_read_emu_block_data(self, block_start: int, block_count: int):
        """
            Gets data for selected block range
        """
        data = struct.pack('!BB', block_start, block_count)
        resp = self.device.send_cmd_sync(Command.MF1_READ_EMU_BLOCK_DATA, data)
        resp.parsed = resp.data
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_get_emu_pages_count(self):
        """
            Gets the number of pages available in the current MF0 / NTAG slot
        """
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_PAGE_COUNT)
        resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_read_emu_page_data(self, page_start: int, page_count: int):
        """
            Gets data for selected block range
        """
        data = struct.pack('!BB', page_start, page_count)
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_READ_EMU_PAGE_DATA, data)
        resp.parsed = resp.data
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_write_emu_page_data(self, page_start: int, data: bytes):
        """
            Gets data for selected block range
        """
        count = len(data) >> 2

        assert (len(data) % 4) == 0
        assert (page_start >= 0) and (count + page_start) <= 256

        data = struct.pack('!BB', page_start, count) + data
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_WRITE_EMU_PAGE_DATA, data)
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_read_emu_counter_data(self, index: int) -> tuple[int, bool]:
        """
            Gets data for selected counter
        """
        data = struct.pack('!B', index)
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_COUNTER_DATA, data)
        if resp.status == Status.SUCCESS:
            resp.parsed = (((resp.data[2] << 16) | (resp.data[1] << 8) | resp.data[0]), resp.data[3] == 0xBD)
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_write_emu_counter_data(self, index: int, value: int, reset_tearing: bool):
        """
            Sets data for selected counter
        """
        data = struct.pack('!BBBB', index | (int(reset_tearing) << 7),
                           value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF)
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_SET_COUNTER_DATA, data)
        return resp

    @expect_response(Status.SUCCESS)
    def mfu_reset_auth_cnt(self):
        """
            Resets authentication counter
        """
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_RESET_AUTH_CNT, bytes())
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def hf14a_set_anti_coll_data(self, uid: bytes, atqa: bytes, sak: bytes, ats: bytes = b''):
        """
        Set anti-collision data of current HF slot (UID/SAK/ATQA/ATS).

        :param uid:  uid bytes
        :param atqa: atqa bytes
        :param sak:  sak bytes
        :param ats:  ats bytes (optional)
        :return:
        """
        data = struct.pack(f'!B{len(uid)}s2s1sB{len(ats)}s', len(uid), uid, atqa, sak, len(ats), ats)
        return self.device.send_cmd_sync(Command.HF14A_SET_ANTI_COLL_DATA, data)

    @expect_response(Status.SUCCESS)
    def set_slot_tag_nick(self, slot: SlotNumber, sense_type: TagSenseType, name: str):
        """
        Set the nick name of the slot.

        :param slot:  Card slot number
        :param sense_type:  field type
        :param name:  Card slot nickname
        :return:
        """
        encoded_name = name.encode(encoding="utf8")
        if len(encoded_name) > 32:
            raise ValueError("Your tag nick name too long.")
        # SlotNumber() will raise error for us if slot not in slot range
        data = struct.pack(f'!BB{len(encoded_name)}s', SlotNumber.to_fw(slot), sense_type, encoded_name)
        return self.device.send_cmd_sync(Command.SET_SLOT_TAG_NICK, data)

    @expect_response(Status.SUCCESS)
    def get_slot_tag_nick(self, slot: SlotNumber, sense_type: TagSenseType):
        """
        Get the nick name of the slot.

        :param slot:  Card slot number
        :param sense_type:  field type
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = struct.pack('!BB', SlotNumber.to_fw(slot), sense_type)
        resp = self.device.send_cmd_sync(Command.GET_SLOT_TAG_NICK, data)
        resp.parsed = resp.data.decode(encoding="utf8")
        return resp

    @expect_response(Status.SUCCESS)
    def get_all_slot_nicks(self):
        resp = self.device.send_cmd_sync(Command.GET_ALL_SLOT_NICKS, b'')

        slots = []
        i = 0
        slot_index = 0

        while i < len(resp.data) and slot_index < 8:
            slot_names = {'hf': '', 'lf': ''}

            if i < len(resp.data):
                hf_len = resp.data[i]
                i += 1
                if hf_len > 0 and i + hf_len <= len(resp.data):
                    slot_names['hf'] = resp.data[i:i + hf_len].decode(encoding="utf8", errors="ignore")
                    i += hf_len
                else:
                    i += hf_len

            if i < len(resp.data):
                lf_len = resp.data[i]
                i += 1
                if lf_len > 0 and i + lf_len <= len(resp.data):
                    slot_names['lf'] = resp.data[i:i + lf_len].decode(encoding="utf8", errors="ignore")
                    i += lf_len
                else:
                    i += lf_len

            slots.append(slot_names)
            slot_index += 1

        resp.parsed = slots
        return resp

    @expect_response(Status.SUCCESS)
    def delete_slot_tag_nick(self, slot: SlotNumber, sense_type: TagSenseType):
        """
        Delete the nick name of the slot.

        :param slot:  Card slot number
        :param sense_type:  field type
        :return:
        """
        # SlotNumber() will raise error for us if slot not in slot range
        data = struct.pack('!BB', SlotNumber.to_fw(slot), sense_type)
        return self.device.send_cmd_sync(Command.DELETE_SLOT_TAG_NICK, data)

    @expect_response(Status.SUCCESS)
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
        resp = self.device.send_cmd_sync(Command.MF1_GET_EMULATOR_CONFIG)
        if resp.status == Status.SUCCESS:
            b1, b2, b3, b4, b5 = struct.unpack('!????B', resp.data)
            resp.parsed = {'detection': b1,
                           'gen1a_mode': b2,
                           'gen2_mode': b3,
                           'block_anti_coll_mode': b4,
                           'write_mode': b5}
        return resp

    @expect_response(Status.SUCCESS)
    def mf1_set_gen1a_mode(self, enabled: bool):
        """
        Set gen1a magic mode
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF1_SET_GEN1A_MODE, data)

    @expect_response(Status.SUCCESS)
    def mf1_set_gen2_mode(self, enabled: bool):
        """
        Set gen2 magic mode
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF1_SET_GEN2_MODE, data)

    @expect_response(Status.SUCCESS)
    def mf1_set_block_anti_coll_mode(self, enabled: bool):
        """
        Set 0 block anti-collision data
        """
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF1_SET_BLOCK_ANTI_COLL_MODE, data)

    @expect_response(Status.SUCCESS)
    def mf1_set_write_mode(self, mode: int):
        """
        Set write mode
        """
        data = struct.pack('!B', mode)
        return self.device.send_cmd_sync(Command.MF1_SET_WRITE_MODE, data)

    @expect_response(Status.SUCCESS)
    def slot_data_config_save(self):
        """
        Update the configuration and data of the card slot to flash.
        :return:
        """
        return self.device.send_cmd_sync(Command.SLOT_DATA_CONFIG_SAVE)

    def enter_bootloader(self):
        """
        Reboot into DFU mode (bootloader)
        :return:
        """
        self.device.send_cmd_auto(Command.ENTER_BOOTLOADER, close=True)

    @expect_response(Status.SUCCESS)
    def get_animation_mode(self):
        """
        Get animation mode value
        """
        resp = self.device.send_cmd_sync(Command.GET_ANIMATION_MODE)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def get_enabled_slots(self):
        """
        Get enabled slots
        """
        resp = self.device.send_cmd_sync(Command.GET_ENABLED_SLOTS)
        if resp.status == Status.SUCCESS:
            resp.parsed = [{'hf': hf, 'lf': lf} for hf, lf in struct.iter_unpack('!BB', resp.data)]
        return resp

    @expect_response(Status.SUCCESS)
    def set_animation_mode(self, value: int):
        """
        Set animation mode value
        """
        data = struct.pack('!B', value)
        return self.device.send_cmd_sync(Command.SET_ANIMATION_MODE, data)

    @expect_response(Status.SUCCESS)
    def reset_settings(self):
        """
        Reset settings stored in flash memory
        """
        resp = self.device.send_cmd_sync(Command.RESET_SETTINGS)
        resp.parsed = resp.status == Status.SUCCESS
        return resp

    @expect_response(Status.SUCCESS)
    def save_settings(self):
        """
        Store settings to flash memory
        """
        resp = self.device.send_cmd_sync(Command.SAVE_SETTINGS)
        resp.parsed = resp.status == Status.SUCCESS
        return resp

    @expect_response(Status.SUCCESS)
    def wipe_fds(self):
        """
        Reset to factory settings
        """
        resp = self.device.send_cmd_sync(Command.WIPE_FDS)
        resp.parsed = resp.status == Status.SUCCESS
        self.device.close()
        return resp

    @expect_response(Status.SUCCESS)
    def get_battery_info(self):
        """
        Get battery info
        """
        resp = self.device.send_cmd_sync(Command.GET_BATTERY_INFO)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('!HB', resp.data)
        return resp

    @expect_response(Status.SUCCESS)
    def get_button_press_config(self, button: ButtonType):
        """
        Get config of button press function
        """
        data = struct.pack('!B', button)
        resp = self.device.send_cmd_sync(Command.GET_BUTTON_PRESS_CONFIG, data)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def set_button_press_config(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of button press function
        """
        data = struct.pack('!BB', button, function)
        return self.device.send_cmd_sync(Command.SET_BUTTON_PRESS_CONFIG, data)

    @expect_response(Status.SUCCESS)
    def get_long_button_press_config(self, button: ButtonType):
        """
        Get config of long button press function
        """
        data = struct.pack('!B', button)
        resp = self.device.send_cmd_sync(Command.GET_LONG_BUTTON_PRESS_CONFIG, data)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def set_long_button_press_config(self, button: ButtonType, function: ButtonPressFunction):
        """
        Set config of long button press function
        """
        data = struct.pack('!BB', button, function)
        return self.device.send_cmd_sync(Command.SET_LONG_BUTTON_PRESS_CONFIG, data)

    @expect_response(Status.SUCCESS)
    def set_ble_connect_key(self, key: str):
        """
        Set config of ble connect key
        """
        data_bytes = key.encode(encoding='ascii')

        # check key length
        if len(data_bytes) != 6:
            raise ValueError("The ble connect key length must be 6")

        data = struct.pack('6s', data_bytes)
        return self.device.send_cmd_sync(Command.SET_BLE_PAIRING_KEY, data)

    @expect_response(Status.SUCCESS)
    def get_ble_pairing_key(self):
        """
        Get config of ble connect key
        """
        resp = self.device.send_cmd_sync(Command.GET_BLE_PAIRING_KEY)
        resp.parsed = resp.data.decode(encoding='ascii')
        return resp

    @expect_response(Status.SUCCESS)
    def delete_all_ble_bonds(self):
        """
        From peer manager delete all bonds.
        """
        return self.device.send_cmd_sync(Command.DELETE_ALL_BLE_BONDS)

    @expect_response(Status.SUCCESS)
    def get_device_capabilities(self):
        """
        Get list of commands that client understands
        """
        try:
            resp = self.device.send_cmd_sync(Command.GET_DEVICE_CAPABILITIES)
        except chameleon_com.CMDInvalidException:
            print("Chameleon does not understand get_device_capabilities command. Please update firmware")
            return chameleon_com.Response(cmd=Command.GET_DEVICE_CAPABILITIES,
                                          status=Status.NOT_IMPLEMENTED)
        else:
            if resp.status == Status.SUCCESS:
                resp.parsed = [x[0] for x in struct.iter_unpack('!H', resp.data)]
            return resp

    @expect_response(Status.SUCCESS)
    def get_device_model(self):
        """
        Get device model
        0 - Chameleon Ultra
        1 - Chameleon Lite
        """

        resp = self.device.send_cmd_sync(Command.GET_DEVICE_MODEL)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
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
        resp = self.device.send_cmd_sync(Command.GET_DEVICE_SETTINGS)
        if resp.status == Status.SUCCESS:
            if resp.data[0] > CURRENT_VERSION_SETTINGS:
                raise ValueError("Settings version in app older than Chameleon. "
                                 "Please upgrade client")
            if resp.data[0] < CURRENT_VERSION_SETTINGS:
                raise ValueError("Settings version in app newer than Chameleon. "
                                 "Please upgrade Chameleon firmware")
            settings_version, animation_mode, btn_press_A, btn_press_B, btn_long_press_A, \
                btn_long_press_B, ble_pairing_enable, ble_pairing_key = \
                struct.unpack('!BBBBBBB6s', resp.data)
            resp.parsed = {'settings_version': settings_version,
                           'animation_mode': animation_mode,
                           'btn_press_A': btn_press_A,
                           'btn_press_B': btn_press_B,
                           'btn_long_press_A': btn_long_press_A,
                           'btn_long_press_B': btn_long_press_B,
                           'ble_pairing_enable': ble_pairing_enable,
                           'ble_pairing_key': ble_pairing_key}
        return resp

    @expect_response(Status.SUCCESS)
    def hf14a_get_anti_coll_data(self):
        """
        Get anti-collision data from current HF slot (UID/SAK/ATQA/ATS)

        :return:
        """
        resp = self.device.send_cmd_sync(Command.HF14A_GET_ANTI_COLL_DATA)
        if resp.status == Status.SUCCESS and len(resp.data) > 0:
            # uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
            offset = 0
            uidlen, = struct.unpack_from('!B', resp.data, offset)
            offset += struct.calcsize('!B')
            uid, atqa, sak, atslen = struct.unpack_from(f'!{uidlen}s2s1sB', resp.data, offset)
            offset += struct.calcsize(f'!{uidlen}s2s1sB')
            ats, = struct.unpack_from(f'!{atslen}s', resp.data, offset)
            offset += struct.calcsize(f'!{atslen}s')
            resp.parsed = {'uid': uid, 'atqa': atqa, 'sak': sak, 'ats': ats}
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_uid_magic_mode(self):
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_UID_MAGIC_MODE)
        if resp.status == Status.SUCCESS:
            resp.parsed, = struct.unpack('!?', resp.data)
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_set_uid_magic_mode(self, enabled: bool):
        return self.device.send_cmd_sync(Command.MF0_NTAG_SET_UID_MAGIC_MODE, struct.pack('?', enabled))

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_version_data(self):
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_VERSION_DATA)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[:8]
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_set_version_data(self, data: bytes):
        assert len(data) == 8
        return self.device.send_cmd_sync(Command.MF0_NTAG_SET_VERSION_DATA, data)

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_signature_data(self):
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_SIGNATURE_DATA)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[:32]
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_set_signature_data(self, data: bytes):
        assert len(data) == 32
        return self.device.send_cmd_sync(Command.MF0_NTAG_SET_SIGNATURE_DATA, data)

    @expect_response(Status.SUCCESS)
    def mf0_ntag_get_write_mode(self):
        """
        Get write mode for MF0/NTAG
        """
        resp = self.device.send_cmd_sync(Command.MF0_NTAG_GET_WRITE_MODE)
        if resp.status == Status.SUCCESS:
            resp.parsed = resp.data[0]
        return resp

    @expect_response(Status.SUCCESS)
    def mf0_ntag_set_write_mode(self, mode: int):
        """
        Set write mode for MF0/NTAG
        """
        data = struct.pack('!B', mode)
        return self.device.send_cmd_sync(Command.MF0_NTAG_SET_WRITE_MODE, data)

    @expect_response(Status.SUCCESS)
    def get_ble_pairing_enable(self):
        """
        Is ble pairing enable?

        :return: True if pairing is enable, False if pairing disabled
        """
        resp = self.device.send_cmd_sync(Command.GET_BLE_PAIRING_ENABLE)
        if resp.status == Status.SUCCESS:
            resp.parsed, = struct.unpack('!?', resp.data)
        return resp

    @expect_response(Status.SUCCESS)
    def set_ble_pairing_enable(self, enabled: bool):
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.SET_BLE_PAIRING_ENABLE, data)
    
    @expect_response(Status.SUCCESS)
    def mf1_get_field_off_do_reset(self):
        resp = self.device.send_cmd_sync(Command.MF1_GET_FIELD_OFF_DO_RESET)
        if resp.status == Status.SUCCESS:
            resp.parsed = struct.unpack('!B', resp.data)[0] == 1
        return resp

    @expect_response(Status.SUCCESS)
    def mf1_set_field_off_do_reset(self, enabled: bool):
        data = struct.pack('!B', enabled)
        return self.device.send_cmd_sync(Command.MF1_SET_FIELD_OFF_DO_RESET, data)


def test_fn():
    # connect to chameleon
    dev = chameleon_com.ChameleonCom()
    try:
        dev.open('com19')
    except chameleon_com.OpenFailException:
        dev.open('/dev/ttyACM0')

    cml = ChameleonCMD(dev)
    ver = cml.get_app_version()
    print(f"Firmware number of application: {ver[0]}.{ver[1]}")
    chip = cml.get_device_chip_id()
    print(f"Device chip id: {chip}")

    # change to reader mode
    cml.set_device_reader_mode()

    options = {
        'activate_rf_field': 1,
        'wait_response': 1,
        'append_crc': 0,
        'auto_select': 0,
        'keep_rf_field': 1,
        'check_response_crc': 0,
    }

    try:
        # unlock 1
        resp = cml.hf14a_raw(options=options, resp_timeout_ms=1000, data=[0x40], bitlen=7)

        if resp[0] == 0x0a:
            print("Gen1A unlock 1 success")
            # unlock 2
            resp = cml.hf14a_raw(options=options, resp_timeout_ms=1000, data=[0x43])
            if resp[0] == 0x0a:
                print("Gen1A unlock 2 success")
                print("Start dump gen1a memory...")
                # Transfer with crc
                options['append_crc'] = 1
                options['check_response_crc'] = 1
                block = 0
                while block < 64:
                    # Tag read block cmd
                    cmd_read_gen1a_block = [0x30, block]
                    if block == 63:
                        options['keep_rf_field'] = 0
                    resp = cml.hf14a_raw(options=options, resp_timeout_ms=100, data=cmd_read_gen1a_block)

                    print(f"Block {block} : {resp.hex()}")
                    block += 1

            else:
                print("Gen1A unlock 2 fail")
                raise
        else:
            print("Gen1A unlock 1 fail")
            raise
    except Exception:
        options['keep_rf_field'] = 0
        options['wait_response'] = 0
        cml.hf14a_raw(options=options)

    # disconnect
    dev.close()


if __name__ == '__main__':
    test_fn()
