#!/usr/bin/env python3
"""Connect actual selection/RATS handling to native ISO14443-4 transport."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def extract_function(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    depth = 1
    end = cursor + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    tests = Path(__file__).resolve().parent
    source = tests.parent / 'application/src'
    subject = (source / 'rfid/nfctag/hf/nfc_14a.c').read_text()
    prefix = subject.split('// Copy from nrf_nfct.c and modified')[0]
    prefix = re.sub(r'^\s*#include[^\n]*$', '', prefix, flags=re.MULTILINE)
    with tempfile.TemporaryDirectory(prefix='chameleon-transport-tests-') as directory:
        temporary = Path(directory)
        (temporary / 'subject.c').write_text(prefix)
        functions = ('void nfc_tag_14a_set_handler(', 'void nfc_tag_14a_set_state(')
        (temporary / 'transport_registration.c').write_text(
            '\n'.join(extract_function(subject, name) for name in functions))
        binary = temporary / 'transport-tests'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fshort-enums',
            '-Wno-pointer-to-int-cast', '-Wno-unused-variable',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer', '-I' + str(temporary),
            '-I' + str(tests / 'stubs'), '-I' + str(source / 'rfid/nfctag/hf'),
            '-I' + str(source / 'rfid/nfctag'), '-I' + str(source / 'rfid'),
            '-I' + str(tests.parent / 'nrf52_sdk/modules/nrfx/mdk'),
            str(tests / 'test_transport_integration.c'),
            str(source / 'rfid/crc_utils.c'), str(source / 'rfid/byte_mirror.c'),
            str(source / 'rfid/parity.c'), '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS selection -> RATS/PPS -> CID/WTX -> chained command/retry')


if __name__ == '__main__':
    main()
