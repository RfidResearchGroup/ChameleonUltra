"""Synthetic, device-free profile and wire contract tests."""

import struct
import sys
from pathlib import Path
import unittest
from typing import Any, cast

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from chameleon_com import ChameleonCom

import sl1_profile as profile
from chameleon_cmd import ChameleonCMD
from chameleon_enum import Command, MfcKeyType, Status
from chameleon_com import Response


def fixture(kind: str = "mini") -> dict[str, Any]:
    count, sectors, _ = profile.COUNTS[kind]
    blocks = [
        {"data": "00" * 16, "status": "imported", "key": None, "errors": []}
        for _ in range(count)
    ]
    keys = []
    for sector in range(sectors):
        blocks[profile.sector_range(sector).stop - 1]["data"] = (
            "??" * 6 + "FF078069" + "??" * 6
        )
        keys.append(
            {
                name: {"value": "FFFFFFFFFFFF", "status": "provided", "evidence": None}
                for name in ("a", "b")
            }
        )
    return {
        "format": "chameleon-sl1-profile",
        "version": 1,
        "name": "Synthetic",
        "hf14a": {"uid": "01020304", "atqa": "0400", "sak": "08", "ats": "0278"},
        "classic": {
            "type": kind,
            "provenance": "synthetic",
            "blocks": blocks,
            "keys": keys,
        },
        "cardAuth": {
            "enabled": True,
            "scenario": "sl1-cardauth-9004",
            "keyNumber": 36868,
            "key": "00" * 16,
            "verification": {"status": "provided", "evidence": None},
        },
    }


class Fake:
    def __init__(self):
        self.calls: list[tuple[Any, ...]] = []
        self.fail: int | None = None
        self.capabilities = profile.COMMANDS

    def set_device_reader_mode(self, reader_mode: bool = True) -> None:
        self.calls.append(("mode", reader_mode))

    def sl1_profile_scan(self) -> dict[str, Any]:
        self.calls.append(("scan",))
        return fixture()["hf14a"]

    def sl1_profile_check_keys(
        self, block: int, key_type: int, keys: list[bytes]
    ) -> bytes | None:
        self.calls.append(("check", block, key_type, tuple(keys)))
        return keys[0] if key_type == 0x61 else None

    def get_device_capabilities(self) -> list[int]:
        self.calls.append(("capabilities",))
        return list(self.capabilities)

    def sl1_profile_begin(self, slot: int, payload: bytes) -> None:
        self.calls.append(("begin", slot, payload))

    def sl1_profile_write(self, slot: int, first: int, data: bytes) -> None:
        self.calls.append(("write", slot, first, data))
        if self.fail == first:
            raise ConnectionError("Disconnected")

    def sl1_profile_save(self, slot: int) -> None:
        self.calls.append(("save", slot))

    def sl1_profile_read_block(self, block: int, type_value: Any, key: bytes) -> bytes:
        self.calls.append(("read", block, type_value))
        if type_value == MfcKeyType.A:
            raise profile.ReadError(6, "Authentication failed")
        return (
            bytes.fromhex("000000000000FF078069000000000000")
            if block in [profile.sector_range(s).stop - 1 for s in range(40)]
            else bytes(16)
        )


class ProfileTests(unittest.TestCase):
    def test_roundtrip_and_geometry(self):
        for kind, (count, sectors, _) in profile.COUNTS.items():
            p = fixture(kind)
            self.assertEqual(profile.loads(profile.dumps(p)), p)
            self.assertEqual(len(profile.image(p)), count * 16)
            self.assertEqual(profile.sector_range(sectors - 1).stop, count)

    def test_reject_unknown_fields_versions_and_duplicate(self):
        for mutate in (
            lambda p: p.update(version=True),
            lambda p: p.update(extra=1),
            lambda p: p["classic"]["blocks"][0].update(extra=1),
        ):
            p = fixture()
            mutate(p)
            with self.assertRaises(ValueError):
                profile.validate(p)
        with self.assertRaises(ValueError):
            profile.loads('{"format":1,"format":2}')

    def test_partial_legacy_conflict_and_access(self):
        for mutate in (
            lambda p: p["classic"].update(provenance="legacy-unverified"),
            lambda p: p["classic"]["blocks"][0].update(data="??" * 16, status="unread"),
            lambda p: p["classic"]["blocks"][3].update(data="00" * 16),
            lambda p: p["classic"]["blocks"][3].update(
                data="??" * 6 + "00000000" + "??" * 6
            ),
        ):
            p = fixture()
            mutate(p)
            with self.assertRaises(ValueError):
                profile.image(p)

    def test_acquisition_a_failure_b_success_and_masking(self):
        p = profile.acquire(Fake(), fixture())
        b = p["classic"]["blocks"][0]
        self.assertEqual(b["data"], "00" * 16)
        self.assertEqual((b["key"], b["errors"][0]["status"]), ("B", 6))
        self.assertEqual(p["classic"]["blocks"][3]["data"][:12], "??" * 6)
        self.assertEqual(p["classic"]["keys"][0]["a"]["status"], "provided")
        self.assertEqual(p["classic"]["keys"][0]["b"]["status"], "verified")
        self.assertEqual(p["classic"]["blocks"][3]["data"][20:], "??" * 6)

    def test_readable_trailer_b_replaces_stale_supplied_key(self):
        observed_b = bytes.fromhex("123456789ABC")

        class Reader(Fake):
            def sl1_profile_read_block(
                self, block: int, type_value: Any, key: bytes
            ) -> bytes:
                self.calls.append(("read", block, type_value))
                if type_value != MfcKeyType.A:
                    raise profile.ReadError(6, "Synthetic stale Key B")
                return (
                    bytes.fromhex("000000000000FF078069") + observed_b
                    if block % 4 == 3
                    else bytes(16)
                )

        seed = fixture()
        result = profile.read_profile(Reader(), seed=seed)
        self.assertEqual(
            result["classic"]["keys"][0]["b"]["value"], observed_b.hex().upper()
        )
        self.assertEqual(result["classic"]["keys"][0]["b"]["status"], "verified")
        self.assertEqual(
            result["classic"]["keys"][0]["b"]["evidence"], "Readable trailer Key B"
        )
        self.assertEqual(profile.image(result)[3 * 16 + 10 : 4 * 16], observed_b)
        self.assertEqual(seed["classic"]["keys"][0]["b"]["value"], "FFFFFFFFFFFF")

    def test_missing_keys_and_boundaries(self):
        for kind, last in (("2k", 127), ("4k", 255)):
            p = fixture(kind)
            p["classic"]["keys"][0] = {
                name: {"value": None, "status": "unknown", "evidence": None}
                for name in ("a", "b")
            }
            fake = Fake()
            result = profile.acquire(fake, p)
            self.assertEqual(result["classic"]["blocks"][0]["status"], "unread")
            self.assertEqual(result["classic"]["blocks"][0]["data"], "??" * 16)
            self.assertIn(("read", last, MfcKeyType.B), fake.calls)

    def test_upload_and_stage_only(self):
        for kind in profile.COUNTS:
            fake = Fake()
            profile.upload(fake, 7, fixture(kind), stage_only=True)
            writes = [call for call in fake.calls if call[0] == "write"]
            self.assertEqual(
                sum(len(call[3]) for call in writes), profile.COUNTS[kind][0] * 16
            )
            self.assertTrue(all(len(call[3]) <= 128 for call in writes))
            self.assertFalse(any(call[0] == "save" for call in fake.calls))
        fake = Fake()
        profile.upload(fake, 0, fixture())
        self.assertEqual(fake.calls[-1], ("save", 0))

    def test_validation_before_mutation_and_disconnect(self):
        fake = Fake()
        p = fixture()
        p["classic"]["provenance"] = "legacy-unverified"
        with self.assertRaises(ValueError):
            profile.upload(fake, 0, p)
        self.assertEqual(fake.calls, [])
        fake.capabilities = set()
        with self.assertRaises(ValueError):
            profile.upload(fake, 0, fixture())
        self.assertEqual(fake.calls, [("capabilities",)])
        fake = Fake()
        fake.fail = 8
        with self.assertRaises(profile.UploadError) as error:
            profile.upload(fake, 0, fixture())
        self.assertEqual(error.exception.phase, "write block 8")
        self.assertFalse(any(call[0] == "save" for call in fake.calls))

    def test_wire(self):
        class Device:
            def __init__(self):
                self.calls: list[tuple[Any, ...]] = []

            def send_cmd_sync(self, command, data):
                self.calls.append((command, data))
                return Response(cmd=command, status=Status.SUCCESS, data=b"")

        device = Device()
        cmd = ChameleonCMD(cast(ChameleonCom, device))
        payload = profile.begin_payload(fixture())
        cmd.sl1_profile_begin(7, payload)
        self.assertEqual(
            device.calls[-1],
            (
                Command.SL1_PROFILE_BEGIN,
                b"\x01\x07"
                + struct.pack(
                    "!HB7s2sBBBBH16s",
                    1000,
                    4,
                    b"\x01\x02\x03\x04\0\0\0",
                    b"\x04\0",
                    8,
                    2,
                    1,
                    1,
                    0x9004,
                    bytes(16),
                )
                + b"\x02\x78",
            ),
        )
        self.assertEqual(len(device.calls[-1][1]), 38)
        cmd.sl1_profile_write(7, 128, bytes(128))
        self.assertEqual(device.calls[-1][1], b"\x01\x07\x00\x80" + bytes(128))
        for method, command in (
            (cmd.sl1_profile_save, 4048),
            (cmd.sl1_profile_cancel, 4049),
            (cmd.sl1_profile_clear, 4050),
        ):
            method(2)
            self.assertEqual(device.calls[-1], (command, b"\x01\x02"))
        cmd.sl1_profile_set_enabled(2, False)
        self.assertEqual(device.calls[-1], (4051, b"\x01\x02\x00"))

    def test_status_and_failure_status(self):
        class Device:
            def __init__(self):
                self.status = Status.SUCCESS
                self.data = struct.pack(
                    "!BBBHBHHHBB", 1, 0, 15, 1001, 1, 0x9004, 64, 64, 0, 7
                )

            def send_cmd_sync(self, command, data):
                return Response(cmd=command, status=self.status, data=self.data)

        device = Device()
        cmd = ChameleonCMD(cast(ChameleonCom, device))
        result = cmd.sl1_profile_status(0)
        self.assertEqual((result["lastStage"], result["expectedBlocks"]), (7, 64))
        self.assertNotIn("key", result)
        device.data = b""
        with self.assertRaises(ValueError):
            cmd.sl1_profile_status(0)
        device.status = Status.PAR_ERR
        with self.assertRaises(profile.ProfileCommandError) as error:
            cmd.sl1_profile_save(0)
        self.assertEqual(error.exception.status, int(Status.PAR_ERR))

    def test_classic_only_and_seven_byte_uid(self):
        p = fixture()
        p["cardAuth"] = None
        p["hf14a"].update(uid="01020304050607", ats=None)
        payload = profile.begin_payload(p)
        self.assertEqual(len(payload), 34)
        self.assertEqual(payload[3:10], bytes.fromhex("01020304050607"))
        self.assertEqual(payload[14:], bytes(20))

    def test_both_reads_fail_preserves_errors(self):
        class Failed(Fake):
            def sl1_profile_read_block(
                self, block: int, type_value: Any, key: bytes
            ) -> bytes:
                raise profile.ReadError(6, "Read failure")

        result = profile.acquire(Failed(), fixture())
        block = result["classic"]["blocks"][0]
        self.assertEqual(block["data"], "??" * 16)
        self.assertEqual(block["status"], "error")
        self.assertEqual([error["key"] for error in block["errors"]], ["A", "B"])

    def test_first_read_dictionary_and_seed_identity(self):
        fake = Fake()
        keys = profile.dictionary_keys(
            "# known keys\nffffffffffff\n000000000000\nffffffffffff\n"
        )
        self.assertEqual(keys, [bytes.fromhex("FFFFFFFFFFFF"), bytes(6)])
        result = profile.read_profile(fake, kind="4k", candidates=keys)
        self.assertEqual(len(result["classic"]["blocks"]), 256)
        self.assertIsNone(result["cardAuth"])
        self.assertEqual(result["classic"]["keys"][39]["b"]["status"], "verified")
        self.assertIn(("check", 240, 0x61, tuple(keys)), fake.calls)
        seed = fixture()
        seed["hf14a"]["uid"] = "05060708"
        with self.assertRaisesRegex(ValueError, "identity changed"):
            profile.read_profile(Fake(), seed=seed)
        self.assertEqual(seed["cardAuth"]["verification"]["status"], "provided")

    def test_seed_key_falls_back_to_supplied_dictionary(self):
        replacement = bytes.fromhex("A1A2A3A4A5A6")
        known_b = bytes.fromhex("FFFFFFFFFFFF")

        class Reader(Fake):
            def sl1_profile_check_keys(
                self, block: int, key_type: int, keys: list[bytes]
            ) -> bytes | None:
                self.calls.append(("check", block, key_type, tuple(keys)))
                valid = replacement if key_type == 0x60 else known_b
                return valid if valid in keys else None

            def sl1_profile_read_block(
                self, block: int, type_value: Any, key: bytes
            ) -> bytes:
                self.calls.append(("read", block, type_value, key))
                valid = replacement if type_value == MfcKeyType.A else known_b
                if key != valid:
                    raise profile.ReadError(6, "Synthetic seed authentication failure")
                return (
                    bytes.fromhex("000000000000FF078069FFFFFFFFFFFF")
                    if block % 4 == 3
                    else bytes(16)
                )

        seed = fixture()
        for sector in seed["classic"]["keys"]:
            sector["a"]["value"] = "111111111111"
        reader = Reader()
        result = profile.read_profile(reader, seed=seed, candidates=[replacement])
        self.assertEqual(
            result["classic"]["keys"][0]["a"]["value"], replacement.hex().upper()
        )
        self.assertEqual(result["classic"]["keys"][0]["a"]["status"], "verified")
        self.assertEqual(result["classic"]["blocks"][0]["key"], "A")
        self.assertEqual(seed["classic"]["keys"][0]["a"]["value"], "111111111111")
        self.assertIn(
            ("check", 0, 0x60, (bytes.fromhex("111111111111"),)), reader.calls
        )
        self.assertIn(("check", 0, 0x60, (replacement,)), reader.calls)
        self.assertNotIn(("check", 0, 0x61, (replacement,)), reader.calls)

    def test_ats_requires_tl_and_t0_before_upload(self):
        for enabled in (None, False, True):
            p = fixture()
            if enabled is None:
                p["cardAuth"] = None
            else:
                p["cardAuth"]["enabled"] = enabled
            p["hf14a"]["ats"] = "01"
            device = Fake()
            with self.subTest(enabled=enabled):
                with self.assertRaisesRegex(ValueError, "ATS"):
                    profile.upload(device, 3, p)
                self.assertEqual(device.calls, [])
        for ats in (None, "", "0278", "FE" + "00" * 253):
            p = fixture()
            p["cardAuth"] = None
            p["hf14a"]["ats"] = ats
            self.assertEqual(profile.validate(p)["hf14a"]["ats"], ats)

    def test_card_change_rejects_mixed_acquisition(self):
        class Swapped(Fake):
            def __init__(self):
                super().__init__()
                self.scans = 0

            def sl1_profile_scan(self) -> dict[str, Any]:
                self.scans += 1
                identity = fixture()["hf14a"]
                if self.scans == 3:
                    identity["uid"] = "05060708"
                return identity

        with self.assertRaisesRegex(ValueError, "identity changed"):
            profile.acquire(Swapped(), fixture())

    def test_mode_ordering_and_explicit_save(self):
        fake = Fake()
        profile.upload(fake, 0, fixture())
        self.assertEqual(
            [call[0] for call in fake.calls[:3]], ["capabilities", "mode", "begin"]
        )
        self.assertEqual(fake.calls[1], ("mode", False))
        staged = Fake()
        profile.upload(staged, 0, fixture(), stage_only=True)
        self.assertFalse(any(call[0] == "mode" for call in staged.calls))
        staged.calls.clear()
        profile.save_staged(staged, 0)
        self.assertEqual(
            staged.calls, [("capabilities",), ("mode", False), ("save", 0)]
        )
        old = Fake()
        old.capabilities = set()
        with self.assertRaises(ValueError):
            profile.save_staged(old, 0)
        self.assertEqual(old.calls, [("capabilities",)])

    def test_read_and_save_cli_arguments(self):
        from chameleon_cli_unit import HFMFProfileRead, HFMFProfileSlot

        read = (
            HFMFProfileRead.__new__(HFMFProfileRead)
            .args_parser()
            .parse_args(
                ["--type", "2k", "--keys-file", "known.dic", "--output", "profile.json"]
            )
        )
        self.assertEqual((read.type, read.keys_file), ("2k", "known.dic"))
        save = (
            HFMFProfileSlot.__new__(HFMFProfileSlot)
            .args_parser()
            .parse_args(["save", "--slot", "8"])
        )
        self.assertEqual((save.action, save.slot), ("save", 8))

    def test_cli_persistent_controls_prepare_tag_mode(self):
        import argparse
        from chameleon_cli_unit import ArgsParserError, HFMFProfileSlot

        class Controls(Fake):
            def sl1_profile_clear(self, slot: int) -> None:
                self.calls.append(("clear", slot))

            def sl1_profile_set_enabled(self, slot: int, enabled: bool) -> None:
                self.calls.append(("enabled", slot, enabled))

        for action, expected in (
            ("clear", ("clear", 2)),
            ("enable", ("enabled", 2, True)),
            ("disable", ("enabled", 2, False)),
        ):
            with self.subTest(action=action):
                fake = Controls()
                unit = HFMFProfileSlot()
                unit._device_cmd = cast(ChameleonCMD, fake)
                args = argparse.Namespace(action=action, slot=3)
                unit.on_exec(args)
                self.assertEqual(
                    fake.calls, [("capabilities",), ("mode", False), expected]
                )

                old = Controls()
                old.capabilities = set()
                unit._device_cmd = cast(ChameleonCMD, old)
                with self.assertRaises(ArgsParserError):
                    unit.on_exec(args)
                self.assertEqual(old.calls, [("capabilities",)])

    def test_known_key_check_bridge_wire(self):
        class Device:
            def __init__(self):
                self.calls: list[tuple[Any, ...]] = []
                self.found = True

            def send_cmd_sync(self, command, data, timeout=10):
                self.calls.append((command, data))
                return Response(
                    cmd=command,
                    status=Status.HF_TAG_OK,
                    data=(b"\x01" + bytes(6)) if self.found else b"\x00" + bytes(6),
                )

        device = Device()
        cmd = ChameleonCMD(cast(ChameleonCom, device))
        self.assertEqual(cmd.sl1_profile_check_keys(240, 0x61, [bytes(6)]), bytes(6))
        self.assertEqual(
            device.calls[-1],
            (Command.MF1_CHECK_KEYS_ON_BLOCK, b"\xf0\x61\x01" + bytes(6)),
        )
        device.found = False
        self.assertIsNone(cmd.sl1_profile_check_keys(240, 0x61, [bytes(6)]))

    def test_dictionary_batches_remain_bounded(self):
        class Candidates(Fake):
            def sl1_profile_check_keys(
                self, block: int, key_type: int, keys: list[bytes]
            ) -> bytes | None:
                self.calls.append(("check", len(keys)))
                return None

        fake = Candidates()
        profile.read_profile(
            fake, kind="mini", candidates=[i.to_bytes(6, "big") for i in range(200)]
        )
        lengths = [call[1] for call in fake.calls if call[0] == "check"]
        self.assertEqual(lengths[:3], [83, 83, 34])
        self.assertEqual(len(lengths), 5 * 2 * 3)


if __name__ == "__main__":
    from unittest.main import main

    main()
