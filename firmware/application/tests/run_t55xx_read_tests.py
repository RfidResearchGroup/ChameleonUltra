"""Compile and execute the production T55xx acquisition against hardware mocks."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parent / "src" / "rfid" / "reader" / "lf"
    with tempfile.TemporaryDirectory(prefix="chameleon-t55xx-read-") as temporary:
        binary = Path(temporary) / (
            "test_t55xx.exe" if os.name == "nt" else "test_t55xx"
        )
        subprocess.run(
            [
                args.cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(tests / "t55xx_read_stubs"),
                "-I",
                str(source),
                str(tests / "test_t55xx_read_capture.c"),
                str(source / "lf_t55xx_read.c"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
