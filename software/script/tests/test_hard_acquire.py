#!/usr/bin/env python3
import sys
sys.path.append('..')

from chameleon_com import ChameleonCom, OpenFailException
from chameleon_cmd import ChameleonCMD
import hardnested_utils


def test_hardnested_acquire():
    nonces_buffer = bytearray()
    acquire_count = 0

    # known key and target block
    key = bytes.fromhex("FFFFFFFFFFFF")  # <-- Your known key
    block_known = 0x00
    type_known = 0x60
    block_target = 0x00
    type_target = 0x60

    # Before acquire start, we need to reset history
    hardnested_utils.reset()

    # The nonces file format required by PM3:
    #   (4byte uid of card) - (block_target 1byte) - (type_target 1byte) - (nonces from device Nbytes)

    # ------------------------     open the device     ------------------------
    try:
        cml = ChameleonCom().open('com19')
    except OpenFailException:
        cml = ChameleonCom().open('/dev/ttyACM0')
    cml_cmd = ChameleonCMD(cml)

    # ------------------------ SET DEVICE MODE ------------------------
    print("Setting device mode to HF Reader...")
    status = cml_cmd.set_device_reader_mode()

    # ------------------------     append tag info     ------------------------

    resp = cml_cmd.hf14a_scan()
    if resp is None or len(resp) == 0:
        print("ISO14443-A Tag no found")
        return

    tag_info = resp[0]
    uidbytes = tag_info['uid']
    uid_len = len(uidbytes)
    if uid_len == 4:
        nonces_buffer.extend(uidbytes[0: 4])
    if uid_len == 7:
        nonces_buffer.extend(uidbytes[3: 7])
    if uid_len == 10:
        nonces_buffer.extend(uidbytes[6: 10])

    nonces_buffer.extend([block_target, type_target & 0x01])

    # ------------------------ append nonces from device ------------------------

    while True:
        # 1, acquire from device
        # slow = 0 to fast acquire...
        acquire_datas = cml_cmd.mf1_hard_nested_acquire(0, block_known, type_known, key, block_target, type_target)
        if acquire_datas is not None:
            acquire_count += 1
            print(f"Acquire success, count: {acquire_count}")
        else:
            raise Exception(f"acquire failed")
        # 2. check data
        data_check_index = 0
        while data_check_index < len(acquire_datas):
            # Memory Layout: nt_enc1(4byte) - nt_enc2(4byte) - par(1byte)...
            # To integer
            nt_enc1 = int.from_bytes(acquire_datas[data_check_index + 0:  data_check_index + 0 + 4])
            nt_enc2 = int.from_bytes(acquire_datas[data_check_index + 4:  data_check_index + 4 + 4])
            par_enc = acquire_datas[data_check_index + 8]
            # check unique and sum
            hardnested_utils.check_nonce_unique_sum(nt_enc1, par_enc >> 4)
            hardnested_utils.check_nonce_unique_sum(nt_enc2, par_enc & 0x0F)
            data_check_index += 9  # The two ciphertext random numbers have a total of 8 bytes, and the parity bits corresponding to the two ciphertext random numbers occupy one byte
        # 3. store data
        nonces_buffer.extend(acquire_datas)
        # 4. After collecting 256 possible different groups, determine whether the collected data is summed correctly. If not, it may not be an EV1 tag.
        if hardnested_utils.hardnested_first_byte_num == 256:
            got_match = False
            for i in range(len(hardnested_utils.hardnested_sums)):
                if hardnested_utils.hardnested_first_byte_sum == hardnested_utils.hardnested_sums[i]:
                    got_match = True  # Sum matches successfully, and we can try to decrypt it next.
                    break
            if got_match:
                print(f"Acquire finish, save to file [nonces.bin], size is {len(nonces_buffer)}bytes")
                break
            else:
                print(
                    f"hardnested_first_byte_num exceeds the limit but got_match is false: {hardnested_utils.hardnested_first_byte_sum}")
        else:
            continue  # Continue acquire

    # ------------------------  write nonces to bin ------------------------
    with open("nonces.bin", mode="wb+") as fd:
        fd.write(nonces_buffer)

    # You can decrypt nonce bin by pm3 client, or any app if support pm3 nonce bin format.
    # TODO If CU bin can decrypt, run cmd on here...


if __name__ == "__main__":
    try:
        test_hardnested_acquire()
    except Exception as e:
        print(f"An error occurred: {e}")
