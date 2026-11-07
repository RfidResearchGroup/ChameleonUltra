#!/usr/bin/env python3
"""Test the actual Nordic IRQ and application callback's timeout ordering.

The existing anticollision fixture supplies fake MMIO and the real parser/TX
functions. This suite also compiles the unchanged production IRQ and application
event callback bodies. It models peripheral notifications, not elapsed RF time.
An error already pending at IRQ entry cannot reveal whether the current frame
has itself missed its deadline; that needs a timestamp or an RF measurement.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CASES = (
    'reqa_without_error', 'stale_timeout_before_new_req_a',
    'genuine_timeout_after_new_req_a', 'stale_then_genuine_timeout_during_rx',
    'selected_preserves_error_suppression',
    'field_events_still_precede_error_and_rx',
    'other_errors_keep_existing_handling',
    'mixed_error_bits_are_preserved',
)


def extract_function(text, signature):
    """Copy one function verbatim, ignoring braces in comments and strings."""
    begin = text.index(signature)
    opening = text.index('{', begin)
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'
                        r"'(?:\\.|[^'\\])*'|[{}]", re.DOTALL)
    depth = 0
    for token in tokens.finditer(text, opening):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return text[begin:token.end()]
    raise ValueError('Unclosed function: ' + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--driver-source', type=Path,
                        help='Alternative nrfx_nfct.c, for baseline/mutation checks')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    firmware = tests.parent
    source = firmware / 'application/src'
    driver = args.driver_source or firmware / 'nrf52_sdk/modules/nrfx/drivers/src/nrfx_nfct.c'
    application = (source / 'rfid/nfctag/hf/nfc_14a.c').read_text()
    boundary = '// Copy from nrf_nfct.c and modified'
    if application.count(boundary) != 1:
        raise ValueError('Cannot locate the Nordic event-driver boundary')
    prefix = application.split(boundary)[0]
    prefix = re.sub(r'^\s*#include[^\n]*$', '', prefix, flags=re.MULTILINE)

    with tempfile.TemporaryDirectory(prefix='chameleon-nfct-irq-') as directory:
        temporary = Path(directory)
        (temporary / 'subject.c').write_text(prefix)
        (temporary / 'nfc_14a.h').write_text((source / 'rfid/nfctag/hf/nfc_14a.h').read_text())
        (temporary / 'nfct_event_callback.inc').write_text(
            extract_function(application, 'void nfc_tag_14a_event_callback('))
        (temporary / 'nfct_irq_handler.inc').write_text(
            extract_function(driver.read_text(), 'void nrfx_nfct_irq_handler('))
        binary = temporary / 'test_nfct_irq'
        command = shlex.split(args.cc) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fshort-enums',
            '-DNRF52840_XXAA', '-Wno-pointer-to-int-cast', '-Wno-unused-variable',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer',
            '-I' + str(temporary), '-I' + str(tests / 'stubs'),
            '-I' + str(source / 'rfid/nfctag/hf'),
            '-I' + str(source / 'rfid/nfctag'), '-I' + str(source / 'rfid'),
            '-I' + str(firmware / 'nrf52_sdk/modules/nrfx/mdk'),
            str(tests / 'test_nfct_irq.c'),
            str(source / 'rfid/crc_utils.c'), str(source / 'rfid/byte_mirror.c'),
            str(source / 'rfid/parity.c'), '-o', str(binary),
        ]
        subprocess.run(command, check=True)
        failed = 0
        for case in CASES:
            result = subprocess.run([str(binary), case], capture_output=True,
                                    text=True, timeout=10)
            print(('FAIL ' if result.returncode else 'PASS ') + case, flush=True)
            if result.returncode:
                failed += 1
                print((result.stdout + result.stderr)[:1000], flush=True)
        print(f'{len(CASES) - failed}/{len(CASES)} cases passed', flush=True)
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
