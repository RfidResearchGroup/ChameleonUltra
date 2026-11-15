#!/usr/bin/env python3
"""Host regression test for the production MIFARE Classic access decoder.

Run from any directory with Python 3.9+ and GCC or Clang:
    python3 firmware/tests/test_mf1_access.py --cc gcc --sanitize

Only the decoder and its permission tables are extracted from nfc_mf1.c;
no SDK, device, card image, UID or key is needed. This tests access decoding
and permission-table selection, not the RF state machine or authentication.
The expected group layout comes from NXP MF1S70YYX_V1, section 8.7.1:
https://www.nxp.com/docs/en/data-sheet/MF1S70YYX_V1.pdf
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


HARNESS = r"""
static void set_access_bits(unsigned packed) {
    unsigned c1 = 0, c2 = 0, c3 = 0;
    for (unsigned group = 0; group < 4; group++) {
        /* The production decoder returns C1 | (C2 << 1) | (C3 << 2). */
        unsigned code = (packed >> (3 * group)) & 7;
        c1 |= (code & 1) << group;
        c2 |= ((code >> 1) & 1) << group;
        c3 |= ((code >> 2) & 1) << group;
    }
    trailer.acs[0] = ((~c2 & 15) << 4) | (~c1 & 15);
    trailer.acs[1] = (c1 << 4) | (~c3 & 15);
    trailer.acs[2] = (c3 << 4) | c2;
}

int main(void) {
    /* Explicit datasheet layout, independent of the decoder's branches. */
    static const unsigned groups[16] = {
        0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3
    };
    unsigned mappings = 0, mismatches = 0, invalid = 0, permissions = 0;
    for (unsigned packed = 0; packed < 4096; packed++) {
        set_access_bits(packed);
        for (unsigned block = 0; block < 256; block++) {
            unsigned group = block < 128 ? block % 4 : groups[block % 16];
            unsigned expected = (packed >> (3 * group)) & 7;
            unsigned actual = GetAccessCondition((uint8_t)block);
            if (actual != expected) {
                if (mismatches < 4) {
                    fprintf(stderr, "ACL mismatch: packed=%u block=%u "
                            "expected=%u actual=%u\n", packed, block, expected, actual);
                }
                mismatches++;
            }
            mappings++;
            if (actual > 7) {
                fprintf(stderr, "Decoder returned an invalid table index\n");
                return 1;
            }
            /* Check both key columns for every block, including trailers.
             * These are policy lookups, not a claim that Key B can always
             * authenticate: that also depends on the trailer policy.
             */
            for (unsigned key = 0; key < 2; key++) {
                const uint8_t (*table)[2] = group == 3
                    ? abTrailerAccessConditions : abDataAccessConditions;
                if (table[actual][key] != table[expected][key]) {
                    permissions++;
                }
            }
        }
        /* Flip each of the 24 stored bits individually, including inverse
         * copies, to check that malformed access bytes still fail closed.
         */
        for (unsigned bit = 0; bit < 24; bit++) {
            trailer.acs[bit / 8] ^= 1u << (bit % 8);
            for (unsigned block = 0; block < 256; block++) {
                if (GetAccessCondition((uint8_t)block) != NO_ACCESS) {
                    fprintf(stderr, "Malformed ACL accepted: packed=%u bit=%u block=%u\n",
                            packed, bit, block);
                    return 1;
                }
                invalid++;
            }
            trailer.acs[bit / 8] ^= 1u << (bit % 8);
        }
    }
    printf("%u valid block/ACL combinations; %u decoding mismatches; "
           "%u permission-table mismatches; %u malformed-ACL rejections\n",
           mappings, mismatches, permissions, invalid);
    if (mismatches || permissions) {
        return 1;
    }
    /* Synthetic example: data groups 000, trailer group 010 (C1 C2 C3).
     * Reading block 129 with Key A is allowed; writing is also allowed.
     * The old mapping incorrectly denied this write by selecting 010.
     */
    set_access_bits(2u << 9);
    unsigned access = abDataAccessConditions[GetAccessCondition(129)][KEY_A];
    if ((access & (ACC_BLOCK_READ | ACC_BLOCK_WRITE)) !=
            (ACC_BLOCK_READ | ACC_BLOCK_WRITE)) {
        fprintf(stderr, "Synthetic data-block read/write regression\n");
        return 1;
    }
    if (GetAccessCondition(143) != 2 ||
            (abTrailerAccessConditions[2][KEY_A] & ACC_TRAILER_WRITE_ACC)) {
        fprintf(stderr, "Synthetic trailer policy changed\n");
        return 1;
    }
    puts("Synthetic read/write and trailer-policy checks passed");
    return 0;
}
"""


def between(source: str, start: str, end: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f"Cannot uniquely locate production code: {start!r}, {end!r}")
    first = source.index(start)
    return source[first:source.index(end, first)]


def make_test(source: str) -> str:
    tables = between(source, "#define ACC_TRAILER_READ_KEYA", "// Save the current MF1 standard status")
    decoder = between(source, "uint8_t GetAccessCondition(", "bool CheckValueIntegrity(")
    definitions = []
    for name in ("BYTE_SWAP", "NO_ACCESS"):
        matches = re.findall(rf"^#define {name}\b[^\n]*", source, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f"Cannot uniquely locate production macro {name}")
        definitions.append(matches[0])
    prelude = """#include <stdint.h>
#include <stdio.h>
typedef struct { uint8_t acs[4]; } trailer_t;
static trailer_t trailer;
static trailer_t *m_tag_trailer_info = &trailer;
"""
    return prelude + tables + "\n".join(definitions) + "\n" + decoder + HARNESS


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true", help="Enable address/undefined-behavior sanitizers")
    parser.add_argument(
        "--source", type=Path,
        default=Path(__file__).resolve().parents[1] / "application/src/rfid/nfctag/hf/nfc_mf1.c",
        help="Production source to test (also accepts an unpatched source for a negative control)",
    )
    args = parser.parse_args()
    test_source = make_test(args.source.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="mf1-access-test-") as temporary:
        directory = Path(temporary)
        source_path = directory / "test.c"
        binary_path = directory / ("test.exe" if os.name == "nt" else "test")
        source_path.write_text(test_source, encoding="utf-8")
        command = shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
        subprocess.run(command + [str(source_path), "-o", str(binary_path)], check=True)
        subprocess.run([str(binary_path)], check=True)


if __name__ == "__main__":
    main()
