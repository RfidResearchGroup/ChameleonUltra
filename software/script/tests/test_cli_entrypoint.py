#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
CLI_MAIN = SCRIPT_DIR / "chameleon_cli_main.py"


def run_cli(*args, stdin=""):
    env = os.environ.copy()
    env["PYTHONPATH"] = str(SCRIPT_DIR)
    return subprocess.run(
        [sys.executable, str(CLI_MAIN), *args],
        input=stdin,
        text=True,
        capture_output=True,
        timeout=5,
        env=env,
    )


def test_help_exits_without_starting_interactive_prompt():
    result = run_cli("--help")

    assert result.returncode == 0
    assert "usage:" in result.stdout
    assert "Chameleon Ultra CLI" in result.stdout
    assert "prompt_toolkit" not in result.stderr


def test_non_interactive_without_command_fails_cleanly():
    result = run_cli()

    assert result.returncode != 0
    assert "interactive terminal" in result.stderr
    assert "prompt_toolkit" not in result.stderr
