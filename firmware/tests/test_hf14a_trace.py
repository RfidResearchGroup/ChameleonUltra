#!/usr/bin/env python3
"""Check the actual firmware capture record writer and its compatibility."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    source = (tests.parent / 'application/src/app_cmd.c').read_text()
    start = source.index('#define HF_SNIFF_BUF_SIZE')
    end = source.index('static data_frame_tx_t *cmd_processor_hf14a_sniff', start)
    with tempfile.TemporaryDirectory(prefix='chameleon-trace-tests-') as directory:
        root = Path(directory)
        (root / 'trace_subject.c').write_text(source[start:end])
        binary = root / 'trace-tests'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Werror', '-O1', '-g',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer', '-I' + str(root),
            str(tests / 'test_hf14a_trace.c'), '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS legacy/metadata capture records, bounds and inactive callbacks')


if __name__ == '__main__':
    main()
