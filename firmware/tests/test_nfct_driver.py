#!/usr/bin/env python3
"""Test the actual NFCT driver lifecycle on nRF52832, nRF52833 and nRF52840.

Only peripheral operations and timer initialization are mocked. Production
control/workaround declarations and init/uninit/field/activation bodies are copied
verbatim into a temporary host translation unit. No other test suite is needed.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CASES = (
    'active_field_reenable', 'filter_active_reenable',
    'pending_validation_reenable', 'future_field_lost',
    'preserve_driver_resources', 'uninit_clears_workaround_flags',
    'duplicate_init_preserves_live_state',
)
DEVICES = ('NRF52832_XXAA', 'NRF52833_XXAA', 'NRF52840_XXAA')


def extract_function(text, signature):
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


def extract_typedef(text, name):
    end = text.index('} ' + name + ';') + len('} ' + name + ';')
    begin = text.rfind('typedef struct', 0, end)
    if begin < 0:
        raise ValueError('Missing typedef: ' + name)
    return text[begin:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--driver-source', type=Path,
                        help='Alternative nrfx_nfct.c for baseline/mutation checks')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    driver = args.driver_source or tests.parent / 'nrf52_sdk/modules/nrfx/drivers/src/nrfx_nfct.c'
    text = driver.read_text()
    start = text.index('static nrfx_nfct_timer_workaround_t m_timer_workaround')
    end = text.index('};', start) + 2
    state = '\n\n'.join((
        extract_typedef(text, 'nrfx_nfct_timer_workaround_t'),
        text[start:end],
        extract_typedef(text, 'nrfx_nfct_control_block_t'),
        'static nrfx_nfct_control_block_t m_nfct_cb;',
    ))
    resettable_validation = 'is_field_validation_pending' in extract_typedef(text, 'nrfx_nfct_timer_workaround_t')
    functions = []
    if 'static void nrfx_nfct_field_state_reset(' in text:
        functions.append(extract_function(text, 'static void nrfx_nfct_field_state_reset('))
    for signature in (
        'static void nrfx_nfct_hw_init_setup(',
        'static void nrfx_nfct_frame_delay_max_set(',
        'static void nrfx_nfct_field_event_handler(',
    ):
        functions.append(extract_function(text, signature))
    functions.append('#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)\n' +
                     extract_function(text, 'static void nrfx_nfct_activate_check(') + '\n' +
                     extract_function(text, 'static void timer_nfc_close_handle(') + '\n#endif')
    for signature in (
        'nrfx_err_t nrfx_nfct_init(', 'void nrfx_nfct_uninit(',
        'void nrfx_nfct_enable(', 'void nrfx_nfct_disable(',
        'void nrfx_nfct_state_force(',
    ):
        functions.append(extract_function(text, signature))
    failed = 0
    with tempfile.TemporaryDirectory(prefix='chameleon-nfct-driver-') as directory:
        temporary = Path(directory)
        (temporary / 'nfct_driver_state.inc').write_text(state)
        (temporary / 'nfct_driver_functions.inc').write_text('\n\n'.join(functions))
        for device in DEVICES:
            binary = temporary / ('test_' + device)
            command = shlex.split(args.cc) + [
                '-std=c11', '-Wall', '-Werror', '-O1', '-g',
                '-DUSE_TIMER_WORKAROUND', '-D' + device,
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-I' + str(temporary),
                str(tests / 'test_nfct_driver.c'), '-o', str(binary),
            ]
            if resettable_validation:
                command.insert(len(shlex.split(args.cc)), '-DTEST_RESETTABLE_VALIDATION_STATE')
            subprocess.run(command, check=True)
            for case in CASES:
                result = subprocess.run([str(binary), case], capture_output=True,
                                        text=True, timeout=10)
                print(('FAIL ' if result.returncode else 'PASS ') + device + ' ' + case, flush=True)
                if result.returncode:
                    failed += 1
                    print((result.stdout + result.stderr)[:1000], flush=True)
    total = len(CASES) * len(DEVICES)
    print(f'{total - failed}/{total} cases passed', flush=True)
    return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
