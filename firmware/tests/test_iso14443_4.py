#!/usr/bin/env python3
"""Compile and test the firmware's actual ISO14443-4 receive callback on the host."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

CASES = (
    'receive_bits_and_crc', 'invalid_crc', 'partial_byte', 'cid_response',
    'long_apdu', 'deselect', 'wtx_ready_response', 'chained_apdu',
    'chain_overflow', 'pps_cid', 'pps_unsupported_rate',
    'truncated_optional_fields', 'static_response_retransmission',
    'deselect_other_session', 'chained_retransmission', 'maximum_apdu',
    'native_activation', 'seos_activation_wrapper', 'cid_address_validation',
    'unsupported_cid_and_nad', 'pps_activation_and_cid', 'pps_after_i_block',
    'pps_invalid_closes_negotiation', 'wtx_cid_exchange',
    'wtx_invalid_acknowledgments', 'wtx_unsolicited', 'wtx_repeat_until_ready',
    'wtx_duplicate_request', 'stateful_chained_reader', 'rblock_recovery',
    'outgoing_fragmentation', 'empty_i_block', 'malformed_control_frames',
    'outgoing_fragmentation_even', 'outgoing_fragmentation_maximum',
    'initial_presence_with_cid',
    'fsc_boundaries',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parent / 'application/src'
    with tempfile.TemporaryDirectory(prefix='chameleon-iso4-tests-') as directory:
        seos = (source / 'rfid/nfctag/hf/nfc_seos.c').read_text()
        begin = seos.index('static void nfc_tag_seos_rats_handler(')
        end = seos.index('\n}', begin) + len('\n}')
        (Path(directory) / 'seos_rats_handler.inc').write_text(seos[begin:end])
        binary = Path(directory) / 'test_iso14443_4'
        command = shlex.split(args.cc) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fshort-enums',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-fno-sanitize-recover=all',
            '-I' + str(tests / 'stubs'),
            '-I' + directory,
            '-I' + str(source / 'rfid/nfctag'),
            '-I' + str(source / 'rfid'),
            str(tests / 'test_iso14443_4.c'), str(source / 'rfid/crc_utils.c'),
            '-o', str(binary),
        ]
        subprocess.run(command, check=True)
        failed = 0
        for case in CASES:
            result = subprocess.run([str(binary), case], capture_output=True,
                                    text=True, timeout=10)
            print(('FAIL' if result.returncode else 'PASS') + ' ' + case, flush=True)
            if result.returncode:
                failed += 1
                print((result.stdout + result.stderr)[:1600], flush=True)
        print(f'{len(CASES) - failed}/{len(CASES)} cases passed')
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
