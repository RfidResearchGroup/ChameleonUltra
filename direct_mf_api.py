#!/usr/bin/env python3
"""
Direct API hf Mifare Classic & lf EM 410x reader (no CLI).
Uses chameleon_com + chameleon_cmd to scan, search keys, and read blocks.
"""

import argparse
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import serial.tools.list_ports

chameleon_com = None
chameleon_cmd = None
hardnested_utils = None
MfcKeyType = None
Status = None
MifareClassicDarksideStatus = None

DEFAULT_KEYS = [
    "FFFFFFFFFFFF",
    "A0A1A2A3A4A5",
    "D3F7D3F7D3F7",
    "000000000000",
    "AABBCCDDEEFF",
    "B0B1B2B3B4B5",
    "4D3A99C351DD",
    "1A982C7E459A",
]

BIN_DIR = Path(__file__).parent / "software" / "bin"
KEY_RE = re.compile(r"([a-fA-F0-9]{12})")


def _hex_to_bytes(hex_key: str) -> bytes:
    return bytes.fromhex(hex_key)


def _load_chameleon_modules() -> None:
    global chameleon_com
    global chameleon_cmd
    global hardnested_utils
    global MfcKeyType
    global Status
    global MifareClassicDarksideStatus

    script_dir = Path(__file__).parent / "software" / "script"
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))

    import chameleon_com as _chameleon_com
    import chameleon_cmd as _chameleon_cmd
    import hardnested_utils as _hardnested_utils
    from chameleon_enum import MfcKeyType as _MfcKeyType
    from chameleon_enum import Status as _Status
    from chameleon_enum import (
        MifareClassicDarksideStatus as _MifareClassicDarksideStatus,
    )

    chameleon_com = _chameleon_com
    chameleon_cmd = _chameleon_cmd
    hardnested_utils = _hardnested_utils
    MfcKeyType = _MfcKeyType
    Status = _Status
    MifareClassicDarksideStatus = _MifareClassicDarksideStatus


def _parse_hex_bytes(value: str, expected_len: int) -> bytes:
    raw = bytes.fromhex(value)
    if len(raw) != expected_len:
        raise ValueError(f"Expected {expected_len} bytes, got {len(raw)}")
    return raw


def find_port(preferred: str | None) -> str | None:
    if preferred:
        return preferred
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None
    return ports[0].device


def open_device(port: str) -> chameleon_com.ChameleonCom:
    device = chameleon_com.ChameleonCom()
    device.open(port)
    return device


def scan_hf14a(cmd: chameleon_cmd.ChameleonCMD):
    resp = cmd.hf14a_scan()
    if resp.status != Status.HF_TAG_OK:
        return None
    if not resp.parsed:
        return None
    return resp.parsed[0]


def detect_mf1(cmd: chameleon_cmd.ChameleonCMD) -> bool:
    return bool(cmd.mf1_detect_support())


def check_default_keys(cmd: chameleon_cmd.ChameleonCMD, block: int) -> dict:
    found = {}
    keys_bytes = [_hex_to_bytes(k) for k in DEFAULT_KEYS]

    for key_type in (MfcKeyType.A, MfcKeyType.B):
        resp = cmd.mf1_check_keys_on_block(block, int(key_type), keys_bytes)
        if resp.status == Status.HF_TAG_OK and resp.parsed:
            found[str(key_type.name)] = resp.parsed.hex().upper()
    return found


def read_blocks(
    cmd: chameleon_cmd.ChameleonCMD, key_hex: str, key_type: MfcKeyType, blocks: int
) -> list:
    key_bytes = _hex_to_bytes(key_hex)
    dump = []
    for block in range(blocks):
        resp = cmd.mf1_read_one_block(block, key_type, key_bytes)
        if resp.status != Status.HF_TAG_OK:
            break
        dump.append({"block": block, "data": resp.parsed.hex().upper()})
    return dump


def _run_tool(tool_name: str, params: list[str], timeout: int = 60) -> str:
    tool_path = BIN_DIR / (f"{tool_name}.exe" if sys.platform == "win32" else tool_name)
    if not tool_path.exists():
        raise FileNotFoundError(f"{tool_name} not found in {BIN_DIR}")
    result = subprocess.run(
        [str(tool_path), *params],
        cwd=str(BIN_DIR),
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout + result.stderr


def _run_tool_in_temp(tool_name: str, params: list[str], timeout: int = 60) -> str:
    tool_path = BIN_DIR / (f"{tool_name}.exe" if sys.platform == "win32" else tool_name)
    if not tool_path.exists():
        raise FileNotFoundError(f"{tool_name} not found in {BIN_DIR}")
    result = subprocess.run(
        [str(tool_path), *params],
        cwd=tempfile.gettempdir(),
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout + result.stderr


def _extract_keys(output: str) -> list[str]:
    return [match.group(1).upper() for match in KEY_RE.finditer(output)]


def nested_attack(
    cmd: chameleon_cmd.ChameleonCMD,
    block_known: int,
    type_known: MfcKeyType,
    key_known_hex: str,
    block_target: int,
    type_target: MfcKeyType,
) -> str | None:
    nt_level = cmd.mf1_detect_prng()
    key_known = _hex_to_bytes(key_known_hex)

    if nt_level == 2:
        print("Tag uses hard nested; not supported in this script.")
        return None

    if nt_level == 0:
        nt_uid_obj = cmd.mf1_static_nested_acquire(
            block_known, type_known, key_known, block_target, type_target
        )
        params = [str(nt_uid_obj["uid"]), str(int(type_target))]
        for item in nt_uid_obj["nts"]:
            params.extend([str(item["nt"]), str(item["nt_enc"])])
        tool = "staticnested"
    else:
        dist_obj = cmd.mf1_detect_nt_dist(block_known, type_known, key_known)
        nt_obj = cmd.mf1_nested_acquire(
            block_known, type_known, key_known, block_target, type_target
        )
        params = [str(dist_obj["uid"]), str(dist_obj["dist"])]
        for item in nt_obj:
            params.extend([str(item["nt"]), str(item["nt_enc"]), str(item["par"])])
        tool = "nested"

    output = _run_tool(tool, params, timeout=120)
    for key in _extract_keys(output):
        if cmd.mf1_auth_one_key_block(block_target, type_target, _hex_to_bytes(key)):
            return key
    return None


def darkside_attack(
    cmd: chameleon_cmd.ChameleonCMD,
    block_target: int,
    type_target: MfcKeyType,
    max_retries: int = 30,
) -> str | None:
    darkside_list = []
    first_recover = True
    retry = 0

    while retry < max_retries:
        resp = cmd.mf1_darkside_acquire(block_target, type_target, first_recover, 30)
        first_recover = False
        if resp[0] != MifareClassicDarksideStatus.OK:
            print(f"Darkside error: {MifareClassicDarksideStatus(resp[0])}")
            return None
        obj = resp[1]
        if obj["par"] != 0:
            darkside_list.clear()
        darkside_list.append(obj)

        params = [str(obj["uid"])]
        for item in darkside_list:
            params.extend(
                [
                    str(item["nt1"]),
                    str(item["ks1"]),
                    str(item["par"]),
                    str(item["nr"]),
                    str(item["ar"]),
                ]
            )

        output = _run_tool("darkside", params, timeout=60)
        if "key not found" in output.lower():
            retry += 1
            continue
        for key in _extract_keys(output):
            if cmd.mf1_auth_one_key_block(
                block_target, type_target, _hex_to_bytes(key)
            ):
                return key
        retry += 1

    return None


def hardnested_attack(
    cmd: chameleon_cmd.ChameleonCMD,
    uid_bytes: bytes,
    block_known: int,
    type_known: MfcKeyType,
    key_known_hex: str,
    block_target: int,
    type_target: MfcKeyType,
    slow_mode: bool,
    max_runs: int,
    keep_nonce_file: bool,
    msb_check: bool,
) -> str | None:
    key_known = _hex_to_bytes(key_known_hex)

    if len(uid_bytes) == 4:
        uid_for_file = uid_bytes[0:4]
    elif len(uid_bytes) == 7:
        uid_for_file = uid_bytes[3:7]
    elif len(uid_bytes) == 10:
        uid_for_file = uid_bytes[6:10]
    else:
        print("Unexpected UID length for hardnested.")
        return None

    raw_nonces = bytearray()
    hardnested_utils.reset()
    seen_msbs = [False] * 256
    unique_msb_count = 0
    msb_parity_sum = 0

    for _ in range(max_runs):
        run_bytes = cmd.mf1_hard_nested_acquire(
            slow_mode,
            block_known,
            type_known,
            key_known,
            block_target,
            type_target,
        )
        if run_bytes:
            raw_nonces.extend(run_bytes)

            if msb_check:
                for i in range(0, len(run_bytes), 9):
                    try:
                        _, nt_enc, par = struct.unpack_from("!IIB", run_bytes, i)
                    except struct.error:
                        continue
                    msb = (nt_enc >> 24) & 0xFF
                    if not seen_msbs[msb]:
                        seen_msbs[msb] = True
                        unique_msb_count += 1
                        parity_bit = hardnested_utils.evenparity32(
                            (nt_enc & 0xFF000000) | (par & 0x08)
                        )
                        msb_parity_sum += parity_bit
                if unique_msb_count == 256:
                    if msb_parity_sum in hardnested_utils.hardnested_sums:
                        break

    if not raw_nonces:
        print("No nonces collected for hardnested.")
        return None

    if msb_check and unique_msb_count == 256:
        if msb_parity_sum not in hardnested_utils.hardnested_sums:
            print("Hardnested MSB parity sum invalid.")
            return None

    header = bytearray(uid_for_file)
    header.extend(struct.pack("!BB", block_target, type_target.value & 0x01))
    header.extend(raw_nonces)

    nonce_file = None
    try:
        temp_file = tempfile.NamedTemporaryFile(
            suffix=".bin",
            prefix="hardnested_nonces_",
            delete=False,
            mode="wb",
            dir=tempfile.gettempdir(),
        )
        temp_file.write(header)
        temp_file.flush()
        nonce_file = temp_file.name
        temp_file.close()

        output = _run_tool_in_temp("hardnested", [nonce_file], timeout=600)
        for line in output.splitlines():
            if line.strip().lower().startswith("key found:"):
                keys = _extract_keys(line)
                for key in keys:
                    if cmd.mf1_auth_one_key_block(
                        block_target, type_target, _hex_to_bytes(key)
                    ):
                        return key
    finally:
        if nonce_file and not keep_nonce_file:
            try:
                Path(nonce_file).unlink(missing_ok=True)
            except Exception:
                pass

    return None


def senested_attack(
    cmd: chameleon_cmd.ChameleonCMD,
    backdoor_key_hex: str,
    sector_count: int,
    starting_sector: int,
) -> dict | None:
    acquire = cmd.mf1_static_encrypted_nested_acquire(
        _hex_to_bytes(backdoor_key_hex), sector_count, starting_sector
    )
    if not acquire:
        return None

    uid_hex = format(acquire["uid"], "x")
    key_map = {"A": {}, "B": {}}

    for sector in range(starting_sector, sector_count):
        sector_name = str(sector).zfill(2)
        a_data = acquire["nts"]["a"][sector]
        b_data = acquire["nts"]["b"][sector]

        a_nt = format(a_data["nt"], "x").zfill(8)
        a_nt_enc = format(a_data["nt_enc"], "x").zfill(8)
        b_nt = format(b_data["nt"], "x").zfill(8)
        b_nt_enc = format(b_data["nt_enc"], "x").zfill(8)

        _run_tool_in_temp(
            "staticnested_1nt",
            [uid_hex, sector_name, a_nt, a_nt_enc, str(a_data["parity"]).zfill(4)],
            timeout=120,
        )
        _run_tool_in_temp(
            "staticnested_1nt",
            [uid_hex, sector_name, b_nt, b_nt_enc, str(b_data["parity"]).zfill(4)],
            timeout=120,
        )

        a_dic = f"keys_{uid_hex}_{sector_name}_{a_nt}.dic"
        b_dic = f"keys_{uid_hex}_{sector_name}_{b_nt}.dic"
        _run_tool_in_temp("staticnested_2x1nt_rf08s", [a_dic, b_dic], timeout=120)

        filtered_b = Path(tempfile.gettempdir()) / b_dic.replace(
            ".dic", "_filtered.dic"
        )
        if not filtered_b.exists():
            continue

        keys_bytes = [
            bytes.fromhex(line.strip())
            for line in filtered_b.read_text().splitlines()
            if line.strip()
        ]

        key_b = None
        for i in range(0, len(keys_bytes), 64):
            data = cmd.mf1_check_keys_on_block(
                sector * 4 + 3, 0x61, keys_bytes[i : i + 64]
            )
            if data:
                key_b = data.hex().zfill(12).upper()
                key_map["B"][sector] = key_b
                break

        if not key_b:
            continue

        a_keys_output = _run_tool_in_temp(
            "staticnested_2x1nt_rf08s_1key",
            [b_nt, key_b, a_dic],
            timeout=120,
        )
        a_keys_bytes = [
            bytes.fromhex(line.strip())
            for line in a_keys_output.splitlines()
            if re.fullmatch(r"[a-fA-F0-9]{12}", line.strip())
        ]
        if not a_keys_bytes:
            continue

        data = cmd.mf1_check_keys_on_block(sector * 4 + 3, 0x60, a_keys_bytes)
        if data:
            key_a = data.hex().zfill(12).upper()
            key_map["A"][sector] = key_a

    if not key_map["A"] and not key_map["B"]:
        return None

    return key_map


def read_blocks_by_sector(
    cmd: chameleon_cmd.ChameleonCMD, key_map: dict, blocks: int
) -> list:
    dump = []
    for block in range(blocks):
        sector = block // 4
        key_hex = key_map["A"].get(sector) or key_map["B"].get(sector)
        if not key_hex:
            continue
        key_type = MfcKeyType.A if sector in key_map["A"] else MfcKeyType.B
        resp = cmd.mf1_read_one_block(block, key_type, _hex_to_bytes(key_hex))
        if resp.status != Status.HF_TAG_OK:
            continue
        dump.append({"block": block, "data": resp.parsed.hex().upper()})
    return dump


def lf_em410x_scan(cmd: chameleon_cmd.ChameleonCMD) -> dict | None:
    resp = cmd.em410x_scan()
    if resp.status != Status.LF_TAG_OK:
        return None
    tag_type, uid_bytes = resp.parsed
    return {"type": int(tag_type), "uid": uid_bytes.hex().upper()}


def lf_em410x_write(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 5)
    resp = cmd.em410x_write_to_t55xx(id_bytes)
    return resp.status == Status.LF_TAG_OK


def lf_em410x_get_emu(cmd: chameleon_cmd.ChameleonCMD) -> dict:
    resp = cmd.em410x_get_emu_id()
    return {"uid": resp.parsed.hex().upper()}


def lf_em410x_set_emu(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 5)
    resp = cmd.em410x_set_emu_id(id_bytes)
    return resp.status == Status.SUCCESS


def lf_hidprox_scan(cmd: chameleon_cmd.ChameleonCMD, fmt: int) -> dict | None:
    resp = cmd.hidprox_scan(fmt)
    if resp.status != Status.LF_TAG_OK:
        return None
    length, fac, card, parity, bitcount, raw = resp.parsed
    return {
        "length": int(length),
        "facility_code": int(fac),
        "card_number": int(card),
        "parity": int(parity),
        "bit_count": int(bitcount),
        "raw": int(raw),
    }


def lf_hidprox_write(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 13)
    resp = cmd.hidprox_write_to_t55xx(id_bytes)
    return resp.status == Status.LF_TAG_OK


def lf_hidprox_get_emu(cmd: chameleon_cmd.ChameleonCMD) -> dict:
    resp = cmd.hidprox_get_emu_id()
    length, fac, card, parity, bitcount, raw = resp.parsed
    return {
        "length": int(length),
        "facility_code": int(fac),
        "card_number": int(card),
        "parity": int(parity),
        "bit_count": int(bitcount),
        "raw": int(raw),
    }


def lf_hidprox_set_emu(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 13)
    resp = cmd.hidprox_set_emu_id(id_bytes)
    return resp.status == Status.SUCCESS


def lf_viking_scan(cmd: chameleon_cmd.ChameleonCMD) -> dict | None:
    resp = cmd.viking_scan()
    if resp.status != Status.LF_TAG_OK:
        return None
    return {"uid": resp.parsed.hex().upper()}


def lf_viking_write(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 4)
    resp = cmd.viking_write_to_t55xx(id_bytes)
    return resp.status == Status.LF_TAG_OK


def lf_viking_get_emu(cmd: chameleon_cmd.ChameleonCMD) -> dict:
    resp = cmd.viking_get_emu_id()
    return {"uid": resp.parsed.hex().upper()}


def lf_viking_set_emu(cmd: chameleon_cmd.ChameleonCMD, id_hex: str) -> bool:
    id_bytes = _parse_hex_bytes(id_hex, 4)
    resp = cmd.viking_set_emu_id(id_bytes)
    return resp.status == Status.SUCCESS


def main() -> int:
    _load_chameleon_modules()
    parser = argparse.ArgumentParser(description="Direct API Mifare Classic reader")
    parser.add_argument("--port", help="Serial port (COMx)")
    parser.add_argument(
        "--blocks", type=int, default=64, help="Blocks to read (default: 64)"
    )
    parser.add_argument("--out", default="mf_dump.json", help="Output JSON file")
    parser.add_argument(
        "--attack",
        choices=["none", "nested", "darkside", "hardnested", "senested", "auto"],
        default="auto",
        help="Attack mode if no default key found",
    )
    parser.add_argument("--known-block", type=int, default=0, help="Known key block")
    parser.add_argument(
        "--known-key", type=str, help="Known key (12 hex) for nested attack"
    )
    parser.add_argument(
        "--known-type", choices=["A", "B"], default="A", help="Known key type"
    )
    parser.add_argument(
        "--target-block", type=int, default=4, help="Target block for attack"
    )
    parser.add_argument(
        "--target-type", choices=["A", "B"], default="A", help="Target key type"
    )
    parser.add_argument(
        "--hardnested-slow", action="store_true", help="Use slow mode for hardnested"
    )
    parser.add_argument(
        "--hardnested-max-runs",
        type=int,
        default=200,
        help="Hardnested nonce acquisition runs",
    )
    parser.add_argument(
        "--hardnested-keep-nonce-file",
        action="store_true",
        help="Keep hardnested nonce file",
    )
    parser.add_argument(
        "--hardnested-disable-msb-check",
        action="store_true",
        help="Disable hardnested MSB parity validation",
    )
    parser.add_argument(
        "--senested-key",
        default="A396EFA4E24F",
        help="Static encrypted backdoor key",
    )
    parser.add_argument("--senested-sectors", type=int, default=16, help="Sector count")
    parser.add_argument(
        "--senested-starting-sector",
        type=int,
        default=0,
        help="Starting sector",
    )
    parser.add_argument(
        "--lf-mode",
        choices=[
            "em410x-scan",
            "em410x-write",
            "em410x-get-emu",
            "em410x-set-emu",
            "hidprox-scan",
            "hidprox-write",
            "hidprox-get-emu",
            "hidprox-set-emu",
            "viking-scan",
            "viking-write",
            "viking-get-emu",
            "viking-set-emu",
        ],
        help="Run LF command and exit",
    )
    parser.add_argument(
        "--lf-id",
        help="LF ID as hex (EM410x=5 bytes, HID Prox=13 bytes, Viking=4 bytes)",
    )
    parser.add_argument(
        "--hidprox-format",
        type=int,
        default=0,
        help="HID Prox format (for scan)",
    )
    args = parser.parse_args()

    port = find_port(args.port)
    if not port:
        print("No serial ports found.")
        return 1

    device = None
    try:
        device = open_device(port)
        cmd = chameleon_cmd.ChameleonCMD(device)

        if args.lf_mode:
            lf_result = {"port": port, "lf_mode": args.lf_mode}

            if args.lf_mode == "em410x-scan":
                tag = lf_em410x_scan(cmd)
                if not tag:
                    print("No LF EM410x tag detected.")
                    return 1
                lf_result["em410x"] = tag
            elif args.lf_mode == "em410x-write":
                if not args.lf_id:
                    print("--lf-id required for em410x-write")
                    return 1
                lf_result["em410x_write"] = lf_em410x_write(cmd, args.lf_id)
            elif args.lf_mode == "em410x-get-emu":
                lf_result["em410x_emu"] = lf_em410x_get_emu(cmd)
            elif args.lf_mode == "em410x-set-emu":
                if not args.lf_id:
                    print("--lf-id required for em410x-set-emu")
                    return 1
                lf_result["em410x_emu_set"] = lf_em410x_set_emu(cmd, args.lf_id)
            elif args.lf_mode == "hidprox-scan":
                tag = lf_hidprox_scan(cmd, args.hidprox_format)
                if not tag:
                    print("No HID Prox tag detected.")
                    return 1
                lf_result["hidprox"] = tag
            elif args.lf_mode == "hidprox-write":
                if not args.lf_id:
                    print("--lf-id required for hidprox-write")
                    return 1
                lf_result["hidprox_write"] = lf_hidprox_write(cmd, args.lf_id)
            elif args.lf_mode == "hidprox-get-emu":
                lf_result["hidprox_emu"] = lf_hidprox_get_emu(cmd)
            elif args.lf_mode == "hidprox-set-emu":
                if not args.lf_id:
                    print("--lf-id required for hidprox-set-emu")
                    return 1
                lf_result["hidprox_emu_set"] = lf_hidprox_set_emu(cmd, args.lf_id)
            elif args.lf_mode == "viking-scan":
                tag = lf_viking_scan(cmd)
                if not tag:
                    print("No Viking tag detected.")
                    return 1
                lf_result["viking"] = tag
            elif args.lf_mode == "viking-write":
                if not args.lf_id:
                    print("--lf-id required for viking-write")
                    return 1
                lf_result["viking_write"] = lf_viking_write(cmd, args.lf_id)
            elif args.lf_mode == "viking-get-emu":
                lf_result["viking_emu"] = lf_viking_get_emu(cmd)
            elif args.lf_mode == "viking-set-emu":
                if not args.lf_id:
                    print("--lf-id required for viking-set-emu")
                    return 1
                lf_result["viking_emu_set"] = lf_viking_set_emu(cmd, args.lf_id)

            with open(args.out, "w", encoding="utf-8") as f:
                json.dump(lf_result, f, indent=2)
            print(f"Saved LF result to {args.out}")
            return 0

        tag = scan_hf14a(cmd)
        if not tag:
            print("No HF 14A tag detected.")
            return 1

        if not detect_mf1(cmd):
            print("Detected tag is not Mifare Classic.")
            return 1

        found_keys = check_default_keys(cmd, block=0)
        sector_keys = None
        if not found_keys:
            print("No default keys found on block 0.")
            if args.attack == "none":
                return 1

            target_type = MfcKeyType.A if args.target_type == "A" else MfcKeyType.B
            if args.attack in ("nested", "auto"):
                if not args.known_key:
                    print("Nested attack needs --known-key.")
                else:
                    known_type = (
                        MfcKeyType.A if args.known_type == "A" else MfcKeyType.B
                    )
                    key = nested_attack(
                        cmd,
                        args.known_block,
                        known_type,
                        args.known_key,
                        args.target_block,
                        target_type,
                    )
                    if key:
                        found_keys[target_type.name] = key

            if not found_keys and args.attack in ("darkside", "auto"):
                key = darkside_attack(cmd, args.target_block, target_type)
                if key:
                    found_keys[target_type.name] = key

            if not found_keys and args.attack in ("hardnested", "auto"):
                if not args.known_key:
                    print("Hardnested needs --known-key.")
                else:
                    known_type = (
                        MfcKeyType.A if args.known_type == "A" else MfcKeyType.B
                    )
                    key = hardnested_attack(
                        cmd,
                        tag["uid"],
                        args.known_block,
                        known_type,
                        args.known_key,
                        args.target_block,
                        target_type,
                        args.hardnested_slow,
                        args.hardnested_max_runs,
                        args.hardnested_keep_nonce_file,
                        not args.hardnested_disable_msb_check,
                    )
                    if key:
                        found_keys[target_type.name] = key

            if (
                not found_keys
                and args.attack in ("senested", "auto")
                and args.senested_key
            ):
                sector_keys = senested_attack(
                    cmd,
                    args.senested_key,
                    args.senested_sectors,
                    args.senested_starting_sector,
                )

            if not found_keys and not sector_keys:
                print("No keys recovered with attack mode.")
                return 1

        if sector_keys:
            dump = read_blocks_by_sector(cmd, sector_keys, args.blocks)
        else:
            key_type = MfcKeyType.A if "A" in found_keys else MfcKeyType.B
            key_hex = found_keys["A"] if key_type == MfcKeyType.A else found_keys["B"]
            dump = read_blocks(cmd, key_hex, key_type, args.blocks)

        result = {
            "port": port,
            "tag": {
                "uid": tag["uid"].hex().upper(),
                "atqa": tag["atqa"].hex().upper(),
                "sak": tag["sak"].hex().upper(),
                "ats": tag["ats"].hex().upper(),
            },
            "keys": found_keys,
            "sector_keys": sector_keys,
            "dump": dump,
        }

        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)

        print(f"Saved dump to {args.out}")
        return 0

    except Exception as exc:
        print(f"Error: {exc}")
        return 1
    finally:
        if device is not None:
            device.close()


if __name__ == "__main__":
    raise SystemExit(main())
