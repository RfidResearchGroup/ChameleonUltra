#!/usr/bin/env python3
"""Test actual NFCT lifecycle against the SDK clock queue and reference counter.

Only peripheral/SoftDevice boundaries are mocked. The NFCT field handlers,
enable/disable path, LF clock clients and SDK request/release/notification bodies
are copied verbatim into a temporary host translation unit. These tests verify
ownership and event order, not crystal startup duration or RF calibration.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CASES = (
    'clock_init_on_enable', 'delayed_hfxo_start', 'synchronous_hfxo_start',
    'duplicate_field_events', 'duplicate_clock_notifications',
    'field_loss_before_start', 'field_loss_then_redetect',
    'disable_before_start', 'disable_then_reenable', 'stale_start_without_owner',
    'stale_start_during_restart', 'stale_start_after_slot_switch',
    'coexist_usb_lf_nfct', 'late_nfct_callback_with_usb',
    'queued_usb_handler_preserved', 'clock_handler_ignores_other_events',
    'reset_on_field_loss', 'repeated_enable_disable',
)


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--clock-source', type=Path,
                        help='Alternative nrf_drv_clock.c for mutation checks')
    parser.add_argument('--application-source', type=Path,
                        help='Alternative nfc_14a.c for mutation checks')
    parser.add_argument('--lf-source', type=Path,
                        help='Alternative lf_tag_em.c for ownership checks')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    firmware = tests.parent
    source = firmware / 'application/src'
    application_file = args.application_source or source / 'rfid/nfctag/hf/nfc_14a.c'
    clock_file = args.clock_source or firmware / 'nrf52_sdk/integration/nrfx/legacy/nrf_drv_clock.c'
    application = application_file.read_text()
    clock = clock_file.read_text()
    lf_file = args.lf_source or source / 'rfid/nfctag/lf/lf_tag_em.c'
    lf = lf_file.read_text()
    boundary = '// Copy from nrf_nfct.c and modified'
    if application.count(boundary) != 1:
        raise ValueError('Cannot locate the Nordic event-driver boundary')
    lifecycle_start = application.index('static enum {', application.index(boundary))
    lifecycle_end = application.index('void nfc_tag_14a_event_callback(', lifecycle_start)
    lifecycle = application[lifecycle_start:lifecycle_end]
    clock_functions = (
        'bool nrf_drv_clock_init_check(',
        'static void hfclk_start(', 'static void hfclk_stop(',
        'static void item_enqueue(',
        'static nrf_drv_clock_handler_item_t * item_dequeue(',
        'void nrf_drv_clock_hfclk_request(',
        'void nrf_drv_clock_hfclk_release(',
        'bool nrf_drv_clock_hfclk_is_running(',
        '__STATIC_INLINE void clock_clk_started_notify(',
        'static void soc_evt_handler(',
    )
    with tempfile.TemporaryDirectory(prefix='chameleon-nfct-clock-') as directory:
        temporary = Path(directory)
        (temporary / 'clock_driver.inc').write_text('\n\n'.join(
            extract_function(clock, signature) for signature in clock_functions))
        (temporary / 'nfct_clock_lifecycle.inc').write_text(lifecycle)
        (temporary / 'nfct_event_callback.inc').write_text(
            extract_function(application, 'void nfc_tag_14a_event_callback('))
        (temporary / 'nfct_sense_switch.inc').write_text(
            extract_function(application, 'void nfc_tag_14a_sense_switch('))
        (temporary / 'lf_clock_client.inc').write_text('\n\n'.join(
            extract_function(lf, signature) for signature in (
                'static void lf_sense_enable(', 'static void lf_sense_disable(')))
        binary = temporary / 'test_nfct_clock'
        command = shlex.split(args.cc) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fshort-enums',
            '-DNRF52840_XXAA', '-DSOFTDEVICE_PRESENT',
            '-Wno-pointer-to-int-cast', '-Wno-unused-variable',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer',
            '-I' + str(temporary),
            '-I' + str(firmware / 'nrf52_sdk/modules/nrfx/mdk'),
            str(tests / 'test_nfct_clock.c'), '-o', str(binary),
        ]
        subprocess.run(command, check=True)
        failed = 0
        for case in CASES:
            result = subprocess.run([str(binary), case], capture_output=True,
                                    text=True, timeout=10)
            print(('FAIL ' if result.returncode else 'PASS ') + case, flush=True)
            if result.returncode:
                failed += 1
                print((result.stdout + result.stderr)[:1200], flush=True)
        print(f'{len(CASES) - failed}/{len(CASES)} cases passed', flush=True)
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
