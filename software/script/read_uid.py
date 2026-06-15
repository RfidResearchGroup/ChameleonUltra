#!/usr/bin/env python3
"""
read_uid.py — Read a Mifare Classic UID via ChameleonUltra and display it large.

Usage:
    py read_uid.py [--port COM3] [--ble] [--watch]

Requirements:
    py -m pip install pyfiglet
    Place this script in the same folder as chameleon_com.py etc. (e.g. C:\\starkdata)
"""

import argparse
import sys
import time

# Add the script's own directory to path so sibling .py files are importable
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ── pretty output ────────────────────────────────────────────────────────────
try:
    import pyfiglet
    def big(text: str, font: str = "big") -> str:
        return pyfiglet.figlet_format(text, font=font)
except ImportError:
    print("[!] pyfiglet not found – falling back to plain text. "
          "Run: py -m pip install pyfiglet", file=sys.stderr)
    def big(text: str, font: str = "big") -> str:
        return f"\n  {text}\n"

# ── ChameleonUltra imports ────────────────────────────────────────────────────
import chameleon_com
import chameleon_cmd

# ── helpers ──────────────────────────────────────────────────────────────────
ANSI_GREEN  = "\033[92m"
ANSI_YELLOW = "\033[93m"
ANSI_RED    = "\033[91m"
ANSI_RESET  = "\033[0m"
ANSI_CYAN   = "\033[96m"
ANSI_BOLD   = "\033[1m"


def format_uid(uid_bytes: bytes) -> str:
    return " ".join(f"{b:02X}" for b in uid_bytes)


def format_uid_compact(uid_bytes: bytes) -> str:
    return "".join(f"{b:02X}" for b in uid_bytes)


def uid_length_name(n: int) -> str:
    return {4: "Single (4B)", 7: "Double (7B)", 10: "Triple (10B)"}.get(n, f"{n}B")


_last_line_count = 0

def clear_last_output():
    global _last_line_count
    if _last_line_count > 0:
        # Move cursor up and clear each line
        sys.stdout.write(f"\033[{_last_line_count}A")
        sys.stdout.write("\033[J")
        sys.stdout.flush()
    _last_line_count = 0

def print_uid(uid_bytes: bytes) -> None:
    global _last_line_count
    uid_spaced  = format_uid(uid_bytes)
    uid_compact = format_uid_compact(uid_bytes)
    uid_len     = uid_length_name(len(uid_bytes))

    clear_last_output()

    lines = []
    lines.append("")
    lines.append("─" * 40)
    for l in big(uid_compact).splitlines():
        lines.append("\033[92m" + l + "\033[0m")
    lines.append(f"  UID (hex)  :  {uid_spaced}")
    lines.append(f"  Length     :  {uid_len}")
    lines.append("─" * 40)

    output = "\n".join(lines) + "\n"
    sys.stdout.write(output)
    sys.stdout.flush()
    _last_line_count = len(lines)


def open_device(port, use_ble):
    dev = chameleon_com.ChameleonCom()
    if use_ble:
        dev.open_via_ble()
    else:
        target = port or chameleon_com.find_port()
        if target is None:
            print(f"{ANSI_RED}[!] No ChameleonUltra serial port found.{ANSI_RESET}\n"
                  "    Connect the device or pass --port COM3",
                  file=sys.stderr)
            sys.exit(1)
        dev.open(target)
    return dev


def init_reader_mode(cmd):
    """Put the device into reader mode before scanning.

    This is the step the interactive CLI performs on startup but a bare
    script does not. Without it the device stays in whatever mode it booted
    into (tag/emulation), hf14a_scan finds nothing, and the device only
    appears to work after the CLI has been run once to flip the mode.

    Guarded by is_device_reader_mode() so subsequent runs skip the switch
    (and its settle delay) entirely — only the first run after a mode change
    pays the cost.
    """
    try:
        if not cmd.is_device_reader_mode():
            cmd.set_device_reader_mode(True)
            # Give the firmware a moment to switch RC522 into reader mode
            time.sleep(0.1)
    except Exception as e:
        print(f"{ANSI_YELLOW}[!] Could not confirm reader mode: {e}{ANSI_RESET}",
              file=sys.stderr)


def scan_once(cmd):
    try:
        resp = cmd.hf14a_scan()
        if not resp:
            return None
        # hf14a_scan returns a list of dicts with 'uid' key
        entry = resp[0] if isinstance(resp, list) else resp
        uid = entry['uid'] if isinstance(entry, dict) else entry.uid
        return bytes(uid)
    except Exception as e:
        return None


# ── main ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Read Mifare Classic UID from ChameleonUltra and display it large."
    )
    parser.add_argument("--port", "-p", metavar="DEV",
                        help="Serial port (e.g. COM3). Auto-detected if omitted.")
    parser.add_argument("--ble", action="store_true",
                        help="Connect via BLE instead of USB serial.")
    parser.add_argument("--watch", "-w", action="store_true",
                        help="Poll continuously until Ctrl-C.")
    parser.add_argument("--interval", "-i", type=float, default=0.5,
                        help="Poll interval in seconds for --watch mode (default: 0.5).")
    parser.add_argument("--font", default="standard",
                        help="pyfiglet font name (default: 'standard'). "
                             "Try 'digital', 'banner', 'big', 'block', 'doom'.")
    args = parser.parse_args()

    print(f"{ANSI_BOLD}ChameleonUltra UID Reader{ANSI_RESET}")
    print(f"Connecting {'via BLE' if args.ble else 'via USB'} …")

    try:
        dev = open_device(args.port, args.ble)
    except Exception as e:
        print(f"{ANSI_RED}[!] Could not open ChameleonUltra: {e}{ANSI_RESET}", file=sys.stderr)
        sys.exit(1)

    cmd = chameleon_cmd.ChameleonCMD(dev)

    # Ensure the device is in reader mode — this is what the CLI does on startup
    # and what was missing here.
    init_reader_mode(cmd)

    # Validate the requested font once at startup by probing it. If it isn't
    # available, warn and fall back to the default rather than crashing later
    # inside print_uid (where there is no fallback).
    if args.font:
        try:
            pyfiglet.figlet_format("0", font=args.font)  # probe — raises if missing
            _font = args.font
            global big
            big = lambda text, font=_font: pyfiglet.figlet_format(text, font=_font)
        except Exception:
            print(f"{ANSI_YELLOW}[!] Font '{args.font}' not found, using default.{ANSI_RESET}",
                  file=sys.stderr)

    print(f"{ANSI_GREEN}Connected.{ANSI_RESET}  Place a Mifare Classic card on the reader …\n")

    last_uid = None

    try:
        while True:
            uid = scan_once(cmd)

            if uid is None:
                last_uid = None  # reset so re-presenting the same card re-displays it
                if args.watch:
                    print(".", end="", flush=True)
                else:
                    print(f"{ANSI_YELLOW}[!] No card detected. Place a card and try again.{ANSI_RESET}")
            else:
                if uid != last_uid:
                    if args.watch:
                        print()
                    print_uid(uid)
                    last_uid = uid

            if not args.watch:
                break

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print(f"\n{ANSI_YELLOW}Stopped.{ANSI_RESET}")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
