#!/usr/bin/env bash
set -euo pipefail

# Resolve paths relative to this script's location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOFTWARE_DIR="$SCRIPT_DIR/software"
VENV_DIR="$SOFTWARE_DIR/.venv"
CLI_SCRIPT="$SOFTWARE_DIR/script/chameleon_cli_main.py"

# --- Find Python 3.9+ ---
PYTHON=""
for candidate in python3 python; do
    if command -v "$candidate" &>/dev/null; then
        version=$("$candidate" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null || true)
        major="${version%%.*}"
        minor="${version#*.}"
        if [ "${major:-0}" -ge 3 ] && [ "${minor:-0}" -ge 9 ]; then
            PYTHON="$candidate"
            break
        fi
    fi
done

if [ -z "$PYTHON" ]; then
    echo "Error: Python 3.9 or newer is required but was not found."
    echo ""
    echo "Install Python with one of:"
    echo "  macOS:   brew install python"
    echo "  Ubuntu:  sudo apt install python3"
    echo "  Windows: https://www.python.org/downloads/"
    exit 1
fi

# --- Bootstrap venv (only if it doesn't exist) ---
if [ ! -f "$VENV_DIR/bin/python" ] && [ ! -f "$VENV_DIR/Scripts/python.exe" ]; then
    echo "First run — setting up Python environment..."
    "$PYTHON" -m venv "$VENV_DIR"

    # Install dependencies matching software/pyproject.toml
    "$VENV_DIR/bin/pip" install --quiet \
        "colorama==0.4.6" \
        "prompt-toolkit==3.0.39" \
        "pyserial==3.5"
    echo "Setup complete."
    echo ""
fi

# --- Launch CLI ---
export PYTHONPATH="$SOFTWARE_DIR/script"
exec "$VENV_DIR/bin/python" "$CLI_SCRIPT" "$@"
