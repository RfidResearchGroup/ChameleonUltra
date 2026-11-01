"""Run host C tests for the production CCID protocol and RF reader bridge."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument(
        "--suite", choices=("all", "reader", "protocol", "transport"), default="all"
    )
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parent / "src"
    stubs = tests / "ccid_stubs"
    with tempfile.TemporaryDirectory(prefix="chameleon-ccid-tests-") as temporary:
        suites = ("reader", "protocol", "transport") if args.suite == "all" else (args.suite,)
        for suite in suites:
            binary = Path(temporary) / (
                f"test_ccid_{suite}.exe" if os.name == "nt" else f"test_ccid_{suite}"
            )
            command = [
                args.cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-DPROJECT_CHAMELEON_ULTRA",
                "-I",
                str(stubs),
                "-I",
                str(source),
            ]
            if suite == "reader":
                command.extend(["-include", str(stubs / "rfid_main.h")])
            if suite == "transport":
                command.extend(["-I", str(tests / "ccid_transport_stubs")])
            command.extend(
                [
                    str(tests / f"test_usb_ccid_{suite}.c"),
                    str(source / f"usb_ccid_{'protocol' if suite == 'transport' else suite}.c"),
                    "-o",
                    str(binary),
                ]
            )
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
