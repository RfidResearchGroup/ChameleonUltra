"""Independent synthetic reader recurrence and PRNG reference."""
class ReaderCrypto1:
    """Reader-side Crypto1 recurrence; no calls to the C/card implementation.

    Uses the public Crypto1 polynomials/filter, with the stream clocked in reader
    protocol order. The fixtures are synthetic and do not test key recovery.
    """
    def __init__(self, key):
        key = int.from_bytes(key, "big")
        self.odd = sum(((key >> ((2 * bit) ^ 7)) & 1) << bit for bit in range(24))
        self.even = sum(((key >> ((2 * bit + 1) ^ 7)) & 1) << bit for bit in range(24))

    def copy(self):
        result = object.__new__(type(self))
        result.odd, result.even = self.odd, self.even
        return result

    def filter(self):
        index = ((0xf22c0 >> (self.odd & 15)) & 16)
        for shift, table, mask in ((4, 0x6c9c0, 8), (8, 0x3c8b0, 4),
                                   (12, 0x1e458, 2), (16, 0x0d938, 1)):
            index |= (table >> ((self.odd >> shift) & 15)) & mask
        return (0xec57e80a >> index) & 1

    def clock(self, value=0, encrypted=False):
        stream = self.filter()
        feedback = (self.odd & 0x29ce5c) ^ (self.even & 0x870804)
        feedback ^= bool(value) ^ (stream if encrypted else 0)
        self.odd, self.even = ((self.even << 1) | (feedback.bit_count() & 1)) & 0xffffff, self.odd
        return stream

    def byte(self, value=0, encrypted=False):
        return sum(self.clock((value >> bit) & 1, encrypted) << bit for bit in range(8))

    def word(self, value=0, encrypted=False):
        stream = bytes(self.byte(byte, encrypted) for byte in value.to_bytes(4, "big"))
        return int.from_bytes(stream, "big")

    def crypt(self, data):
        return bytes(byte ^ self.byte() for byte in data)

    def decrypt_with_parity(self, wire, parity):
        plain = bytearray()
        for index, byte in enumerate(wire):
            plain.append(byte ^ self.byte())
            expected = self.filter() ^ (1 ^ (plain[-1].bit_count() & 1))
            assert ((parity[index // 8] >> (index % 8)) & 1) == expected
        return bytes(plain)


def successor(nt, clocks):
    value = int.from_bytes(nt.to_bytes(4, "big"), "little")
    for _ in range(clocks):
        feedback = ((value >> 16) ^ (value >> 18) ^ (value >> 19) ^ (value >> 21)) & 1
        value = (value >> 1) | (feedback << 31)
    return int.from_bytes(value.to_bytes(4, "little"), "big")
