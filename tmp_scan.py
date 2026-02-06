import sys, json

sys.path.insert(0, "software/script")
import chameleon_com, chameleon_cmd
from chameleon_enum import Status

PORT = "COM3"
print(f"Connecting to {PORT}...")
com = chameleon_com.ChameleonCom()
try:
    com.open(PORT)
    print("Connected.")
    cmd = chameleon_cmd.ChameleonCMD(com)

    print("\n=== HF 14A scan ===")
    rh = cmd.hf14a_scan()
    print("Status:", rh.status.name)
    print("Data:", getattr(rh, "parsed", rh.data if hasattr(rh, "data") else None))

    print("\n=== LF EM410x scan ===")
    rl = cmd.em410x_scan()
    print("Status:", rl.status.name)
    data = getattr(rl, "parsed", rl.data if hasattr(rl, "data") else None)
    print("Data:", data)

    # If we have an EM410x UID, print it in hex and decimal for convenience.
    if (
        rl.status == Status.LF_TAG_OK
        and isinstance(data, (list, tuple))
        and len(data) == 2
    ):
        tag_type, uid_bytes = data
        # uid_bytes is bytes; convert to hex and decimal
        uid_hex = uid_bytes.hex().upper()
        uid_dec = int.from_bytes(uid_bytes, byteorder="big")
        print(f"EM410x UID hex: {uid_hex}")
        print(f"EM410x UID dec: {uid_dec}")

finally:
    try:
        com.close()
        print("Disconnected.")
    except Exception:
        pass
