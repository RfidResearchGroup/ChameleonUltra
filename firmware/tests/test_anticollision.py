#!/usr/bin/env python3
"""Exercise the actual ISO14443-A selection handler and TX setup on the host.

This investigation harness compiles the source up to the Nordic event-driver
section, replacing its includes and memory-mapped peripheral with a small shim.
The handler, parity stripping, CRC and transmit functions are not reimplemented.
RF timing and the Nordic peripheral itself are outside this host test's scope.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CASES = (
    'full_uid_4', 'full_uid_7', 'full_uid_10',
    'prefix_one_byte', 'prefix_four_bytes_cl1', 'prefix_four_bytes_cl2',
    'prefix_four_bytes_cl3', 'split_bit_cl1', 'split_bit_cl2', 'split_bit_cl3',
    'all_prefix_splits', 'mismatched_byte_prefix', 'mismatched_zero_split',
    'truncated_prefix', 'trailing_prefix_data', 'invalid_nvb',
    'invalid_nvb_matching_length', 'invalid_bcc_prefix_matching_length',
    'select_bad_crc', 'select_bad_bcc', 'select_wrong_uid',
    'select_invalid_length', 'prefix_mismatch_retry', 'select_bad_crc_retry',
    'selection_recovery', 'selection_timing', 'selection_timing_restored',
    'passive_prefix', 'collision_walk', 'large_active_frame',
    'rats_activation', 'rats_invalid_cid', 'rats_invalid_crc',
    'rats_invalid_length', 'rats_without_ats', 'rats_missing_resources',
    'ats_rate_sanitized', 'ats_fsc_sanitized', 'ats_invalid_structure', 'ats_fsd_limit',
    'ats_valid_boundaries',
    'missing_resources', 'unsupported_uid_size',
    'unwrap_tight_buffers', 'unwrap_raw_boundaries', 'maximum_active_frame',
    'rx_limit_rejection',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parent / 'application/src'
    subject = args.source or source / 'rfid/nfctag/hf/nfc_14a.c'
    text = subject.read_text()
    boundary = '// Copy from nrf_nfct.c and modified'
    if text.count(boundary) != 1:
        raise ValueError('Cannot locate the Nordic event-driver boundary')
    prefix = text.split(boundary)[0]
    prefix = re.sub(r'^\s*#include[^\n]*$', '', prefix, flags=re.MULTILINE)
    with tempfile.TemporaryDirectory(prefix='chameleon-anticollision-') as directory:
        temporary = Path(directory)
        (temporary / 'subject.c').write_text(prefix)
        # The fork predates the upstream uint16_t unwrap fix. Match its actual
        # declaration so its large-frame behavior is tested, rather than hidden.
        unwrap_type = re.search(r'(uint(?:8|16)_t) nfc_tag_14a_unwrap_frame', prefix)[1]
        header = (source / 'rfid/nfctag/hf/nfc_14a.h').read_text()
        header = re.sub(r'uint16_t nfc_tag_14a_unwrap_frame',
                        unwrap_type + ' nfc_tag_14a_unwrap_frame', header)
        (temporary / 'nfc_14a.h').write_text(header)
        binary = temporary / 'test_anticollision'
        command = shlex.split(args.cc) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fshort-enums',
            '-Wno-pointer-to-int-cast', '-Wno-unused-variable',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer',
            '-I' + str(temporary), '-I' + str(tests / 'stubs'),
            '-I' + str(source / 'rfid/nfctag/hf'),
            '-I' + str(source / 'rfid/nfctag'),
            '-I' + str(source / 'rfid'),
            '-I' + str(tests.parent / 'nrf52_sdk/modules/nrfx/mdk'),
            str(tests / 'test_anticollision.c'),
            str(source / 'rfid/crc_utils.c'),
            str(source / 'rfid/byte_mirror.c'),
            str(source / 'rfid/parity.c'), '-o', str(binary),
        ]
        subprocess.run(command, check=True)
        failed = 0
        for case in CASES:
            result = subprocess.run([str(binary), case], capture_output=True,
                                    text=True, timeout=10)
            print(('FAIL' if result.returncode else 'PASS') + ' ' + case,
                  flush=True)
            if result.returncode:
                failed += 1
                print((result.stdout + result.stderr)[:800], flush=True)
        print(f'{len(CASES) - failed}/{len(CASES)} cases passed')
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
