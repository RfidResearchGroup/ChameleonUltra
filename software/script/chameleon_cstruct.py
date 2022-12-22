"""
    From bytes to c struct parser(Chameleon data response)
"""


def bytes_to_u32(byte_array):
    """
        bytes array to u32 int
    :param byte_array:
    :return:
    """
    return int.from_bytes(byte_array, byteorder='big', signed=False)


def parse_14a_scan_tag_result(data: bytearray):
    """
        From bytes parse tag info
    :param data:
    :return:
    """
    return {
        'uid_size': data[10],
        'uid_hex': data[0:data[10]].hex(),
        'sak_hex': hex(data[12]).lstrip('0x').rjust(2, '0'),
        'atqa_hex': data[13:15].hex().upper(),
    }


def parse_nt_distance_detect_result(data: bytearray):
    """
        From bytes parse nt distance
    :param data: data
    :return:
    """
    return {'uid': bytes_to_u32(data[0:4]), 'dist': bytes_to_u32(data[4:8])}


def parse_nested_nt_acquire_group(data: bytearray):
    """
        From bytes parse nested param
    :param data: data
    :return:
    """
    group = []
    if len(data) % 9 != 0:
        raise ValueError("Nt data length error, except: { nt(4byte), nt_enc(4byte), par(1byte) } * N")
    i = 0
    while i < len(data):
        group.append({
            'nt': bytes_to_u32(data[i: i + 4]),
            'nt_enc': bytes_to_u32(data[i + 4: i + 8]),
            'par': data[i + 8],
        })
        i += 9
    return group


def parse_darkside_acquire_result(data: bytearray):
    """
        From bytes parse darkside param
    :param data: data
    :return:
    """
    return {
        'uid': bytes_to_u32(data[0 : 4]),
        'nt1': bytes_to_u32(data[4 : 8]),
        'par': bytes_to_u32(data[8 : 16]),
        'ks1': bytes_to_u32(data[16: 24]),
        'nr':  bytes_to_u32(data[24: 28]),
        'ar':  bytes_to_u32(data[28: 32]),
    }
