"""Exercise firmware persistence semantics, preflight and private backups."""

import hashlib
import json
import tempfile
import unittest
from unittest.main import TestProgram
from dataclasses import replace
from pathlib import Path
from typing import Any
from unittest.mock import patch

from chameleon_com import Response
from chameleon_enum import Command, Status
from slot_profile import SlotProfileManager
from slot_profile_data import (
    EmulatorConfig,
    SlotProfileError,
    SlotSnapshot,
    SlotState,
)
from slot_profile_io import (
    load_card_profile,
    load_snapshot,
    save_snapshot,
    snapshot_document,
)
from tests.slot_profile_fixtures import DATA_SETTERS, FakeSlots, profile

STORAGE_ERRORS: tuple[type[Exception], ...] = (SlotProfileError, OSError)


def card_document(p=None):
    p = p or profile()
    return {
        "format": "chameleon-gen1a-classic1k-v1",
        "read_profile": "gen1a-compatible-classic1k",
        "block_size": 16,
        "block_count": 64,
        "independent_reads": 2,
        "key_material_verified": False,
        "uid": p.uid.hex(),
        "atqa_wire_order": p.atqa.hex(),
        "sak": p.sak.hex(),
        "sha256": p.image_sha256,
        "blocks": [p.image[i : i + 16].hex() for i in range(0, 1024, 16)],
    }


class SourceAndBackupTest(unittest.TestCase):
    def test_card_json_and_explicit_bin_produce_the_same_safe_profile(self):
        p = profile()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "image.json"
            source.write_text(json.dumps(card_document(p)), encoding="utf-8")
            from_json = load_card_profile(source)
            binary = root / "image.bin"
            binary.write_bytes(p.image)
            from_bin = load_card_profile(
                binary, atqa="0400", sak="08", expected_sha256=p.image_sha256
            )
            self.assertEqual(from_json, from_bin)
            self.assertEqual(from_json.image, p.image)
            self.assertEqual(from_json.config, EmulatorConfig())
            self.assertFalse(from_json.config.gen1a or from_json.config.gen2)
            self.assertEqual(from_json.config.write_mode, 1)

    def test_bad_json_fields_lengths_uid_bcc_and_digests_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image.json"
            for field, invalid in (
                ("format", "other"),
                ("read_profile", "other"),
                ("block_count", True),
                ("block_count", 63),
                ("block_size", 8),
                ("independent_reads", True),
                ("independent_reads", 1),
                ("key_material_verified", "yes"),
                ("uid", "00000000"),
                ("atqa_wire_order", "04"),
                ("sak", "18"),
                ("sha256", "00" * 32),
                ("blocks", []),
            ):
                document = card_document()
                document[field] = invalid
                source.write_text(json.dumps(document), encoding="utf-8")
                with (
                    self.subTest(field=field, invalid=invalid),
                    self.assertRaises(SlotProfileError),
                ):
                    load_card_profile(source)
            document = card_document()
            image = bytearray(profile().image)
            image[4] ^= 1
            document["blocks"] = [image[i : i + 16].hex() for i in range(0, 1024, 16)]
            document["sha256"] = hashlib.sha256(image).hexdigest()
            source.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(SlotProfileError, "UID"):
                load_card_profile(source)

    def test_duplicate_and_oversized_json_and_identity_overrides_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image.json"
            for content in (b'{"format":1,"format":2}', b"[" + b" " * 65536):
                source.write_bytes(content)
                with self.assertRaises(SlotProfileError):
                    load_card_profile(source)
            source.write_text(json.dumps(card_document()), encoding="utf-8")
            with self.assertRaises(SlotProfileError):
                load_card_profile(source, atqa="0400")

    def test_bin_needs_explicit_metadata_and_matching_expected_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image.bin"
            source.write_bytes(profile().image)
            for args in (
                {},
                {"atqa": "0400"},
                {"atqa": "0400", "sak": "08"},
                {"atqa": "0400", "sak": "08", "expected_sha256": "00" * 32},
            ):
                with self.subTest(args=args), self.assertRaises(SlotProfileError):
                    load_card_profile(source, **args)

    def test_slot_snapshot_roundtrip_covers_memory_metadata_and_all_flags(self):
        p = replace(
            profile(),
            uid=bytes.fromhex("04112233445566"),
            ats=b"\x01\x02",
            config=EmulatorConfig(True, True, True, True, 2, True, 2),
        )
        snapshot = SlotSnapshot("ab" * 32, 8, SlotState(1001, False, p))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "before.json"
            save_snapshot(snapshot, path)
            self.assertEqual(load_snapshot(path), snapshot)
            with self.assertRaises(SlotProfileError):
                save_snapshot(snapshot, path)
            original = path.read_bytes()
            document = json.loads(original)
            for field, replacement in (
                ("slot", 7),
                ("device_sha256", "cd" * 32),
                ("hf_enabled", True),
            ):
                changed = dict(document)
                changed[field] = replacement
                path.write_text(json.dumps(changed), encoding="utf-8")
                with (
                    self.subTest(field=field),
                    self.assertRaisesRegex(SlotProfileError, "SHA-256"),
                ):
                    load_snapshot(path)
            path.write_bytes(original)

    def test_empty_snapshot_roundtrip_and_no_secret_repr(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.json"
            value = SlotSnapshot("ab" * 32, 8, SlotState(0, False))
            save_snapshot(value, path)
            self.assertEqual(load_snapshot(path), value)
        self.assertNotIn(repr(profile().image), repr(profile()))

    def test_invalid_native_config_is_rejected_even_with_recomputed_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.json"
            original = snapshot_document(
                SlotSnapshot("ab" * 32, 8, SlotState(1001, True, profile()))
            )
            for field, bad in (("write_mode", 3), ("prng", 3), ("gen1a", "true")):
                document = json.loads(json.dumps(original))
                document["profile"]["config"][field] = bad
                document.pop("sha256")
                document["sha256"] = hashlib.sha256(
                    json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
                ).hexdigest()
                path.write_text(json.dumps(document), encoding="utf-8")
                with self.subTest(field=field), self.assertRaises(SlotProfileError):
                    load_snapshot(path)


class ManagerTest(unittest.TestCase):
    def test_import_reload_restore_empty_preserves_other_slots_and_original_context(
        self,
    ):
        device = FakeSlots()
        originals = dict(device.flash)
        manager = SlotProfileManager(device)
        plan = manager.plan_import(8, profile(9))
        self.assertEqual(device.setters(), [])
        self.assertEqual((device.active, device.reader), (2, False))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "before.json"
            manager.apply(plan, path)
            self.assertEqual(device.flash[7], profile(9))
            self.assertEqual((device.types[7], device.enabled[7]), (1001, True))
            self.assertEqual(
                (device.active, device.reader, device.saved_active), (2, False, 2)
            )
            self.assertEqual(device.saved_types[7], 1001)
            self.assertTrue(manager.progress.data_verified)
            before = load_snapshot(path)
            restorer = SlotProfileManager(device)
            restore_plan = restorer.plan_restore(before)
            restorer.apply(restore_plan, Path(directory) / "before-restore.json")
            self.assertEqual((device.types[7], device.enabled[7]), (0, False))
            self.assertNotIn(7, device.flash)
            self.assertEqual(device.flash, originals)
            self.assertEqual((device.active, device.reader), (2, False))
            self.assertEqual(device.saves, [2, 2])
        self.assertFalse(
            any(
                int(cmd) < 3000
                and cmd
                not in {
                    Command.GET_DEVICE_MODEL,
                    Command.GET_DEVICE_CHIP_ID,
                    Command.GET_DEVICE_MODE,
                    Command.CHANGE_DEVICE_MODE,
                    Command.GET_ACTIVE_SLOT,
                    Command.SET_ACTIVE_SLOT,
                    Command.GET_SLOT_INFO,
                    Command.GET_ENABLED_SLOTS,
                    Command.SET_SLOT_TAG_TYPE,
                    Command.SET_SLOT_DATA_DEFAULT,
                    Command.SET_SLOT_ENABLE,
                    Command.DELETE_SLOT_SENSE_TYPE,
                    Command.SLOT_DATA_CONFIG_SAVE,
                }
                for cmd, _ in device.calls
            )
        )

    def test_existing_disabled_profile_restores_all_config_and_handles_auto_selection(
        self,
    ):
        device = FakeSlots()
        old = replace(
            profile(7), config=EmulatorConfig(True, True, True, True, 2, True, 2)
        )
        device.types[7] = 1001
        device.enabled[7] = False
        device.flash[7] = old
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "before.json"
            manager = SlotProfileManager(device)
            manager.apply(manager.plan_import(8, profile(9)), path)
            restore = SlotProfileManager(device)
            restore.apply(
                restore.plan_restore(load_snapshot(path)),
                Path(directory) / "second.json",
            )
        self.assertEqual(device.flash[7], old)
        self.assertFalse(device.enabled[7])
        self.assertEqual((device.active, device.reader), (2, False))

    def test_before_image_is_published_and_exact_before_factory_command(self):
        device = FakeSlots()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "before.json"
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile(9))

            def audit(fake, cmd, data):
                if cmd in DATA_SETTERS:
                    self.assertEqual(load_snapshot(path), plan.before)
                    self.assertEqual(
                        fake.active, 7 if cmd != Command.SLOT_DATA_CONFIG_SAVE else 2
                    )
                    self.assertTrue(fake.reader)
                return None

            device.fault = audit
            manager.apply(plan, path)

    def test_active_lf_unsupported_and_bad_slot_targets_do_not_get_selected(self):
        bad_slots: tuple[Any, ...] = (0, 9, True, 1.5, "8", 3)
        for slot in bad_slots:
            device = FakeSlots()
            with self.subTest(slot=slot), self.assertRaises(SlotProfileError):
                SlotProfileManager(device).plan_import(slot, profile())
            self.assertFalse(
                any(cmd == Command.SET_ACTIVE_SLOT for cmd, _ in device.calls)
            )
        for field, value in (("lf_types", 100), ("lf_enabled", True), ("types", 1100)):
            device = FakeSlots()
            getattr(device, field)[7] = value
            with self.subTest(field=field), self.assertRaises(SlotProfileError):
                SlotProfileManager(device).plan_import(8, profile())
            self.assertEqual(device.setters(), [])

    def test_shadow_origin_and_nonempty_detection_logs_refuse_before_context_changes(
        self,
    ):
        for shadow, logs in ((True, 0), (False, 1)):
            device = FakeSlots()
            if shadow:
                device.ram = replace(
                    device.ram, config=replace(device.ram.config, write_mode=3)
                )
            device.logs = logs
            with (
                self.subTest(shadow=shadow, logs=logs),
                self.assertRaises(SlotProfileError),
            ):
                SlotProfileManager(device).plan_import(8, profile())
            self.assertFalse(
                any(
                    cmd in {Command.SET_ACTIVE_SLOT, Command.CHANGE_DEVICE_MODE}
                    for cmd, _ in device.calls
                )
            )

    def test_shadow_target_and_capture_transport_errors_restore_context(self):
        device = FakeSlots()
        device.types[7] = 1001
        device.flash[7] = replace(
            profile(), config=replace(profile().config, write_mode=3)
        )
        with self.assertRaises(SlotProfileError):
            SlotProfileManager(device).plan_import(8, profile())
        self.assertEqual((device.active, device.reader), (2, False))
        self.assertEqual(device.setters(), [])

    def test_stale_plan_and_changed_target_refuse_before_data_setters(self):
        for mode in ("inventory", "memory"):
            device = FakeSlots()
            device.types[7] = 1001
            device.flash[7] = profile(7)
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile(9))
            if mode == "inventory":
                device.enabled[0] = False
            else:
                device.flash[7] = profile(8)
            with tempfile.TemporaryDirectory() as directory:
                with self.subTest(mode=mode), self.assertRaises(SlotProfileError):
                    manager.apply(plan, Path(directory) / "before.json")
            self.assertEqual(device.setters(), [])
            self.assertEqual((device.active, device.reader), (2, False))

    def test_wrong_device_and_restore_slot_are_rejected(self):
        device = FakeSlots()
        snapshot = SlotSnapshot("00" * 32, 8, SlotState(0, False))
        with self.assertRaisesRegex(SlotProfileError, "another device"):
            SlotProfileManager(device).plan_restore(snapshot)
        self.assertEqual(device.setters(), [])

    def test_reserved_original_prng_is_preserved_without_normalization(self):
        device = FakeSlots()
        original = replace(device.ram, config=replace(device.ram.config, prng=3))
        device.ram = original
        device.flash[2] = original
        manager = SlotProfileManager(device)
        with tempfile.TemporaryDirectory() as directory:
            manager.apply(
                manager.plan_import(8, profile(9)),
                Path(directory) / "before.json",
            )
        self.assertEqual(device.flash[2], original)
        self.assertEqual(device.ram, original)
        self.assertEqual(device.flash[7].config.prng, 1)
        self.assertEqual((device.active, device.reader), (2, False))

    def test_reserved_target_prng_refuses_capture_before_destructive_changes(self):
        device = FakeSlots()
        device.types[7] = 1001
        device.flash[7] = replace(profile(7), config=replace(profile(7).config, prng=3))
        with self.assertRaisesRegex(SlotProfileError, "reserved PRNG"):
            SlotProfileManager(device).plan_import(8, profile(9))
        self.assertEqual(device.setters(), [])
        self.assertEqual((device.active, device.reader), (2, False))

    def test_lost_ack_stops_and_retains_backup_progress_without_auto_rollback(self):
        for command in (
            Command.SET_SLOT_DATA_DEFAULT,
            Command.MF1_WRITE_EMU_BLOCK_DATA,
            Command.HF14A_SET_ANTI_COLL_DATA,
            Command.MF1_SET_GEN1A_MODE,
        ):
            device = FakeSlots()
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile(9))
            fired = []

            def fail(fake, cmd, data):
                if cmd == command and not fired:
                    fired.append(cmd)
                    return TimeoutError("lost ACK")
                return None

            device.fault = fail
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "before.json"
                with self.subTest(command=command), self.assertRaises(SlotProfileError):
                    manager.apply(plan, path)
                self.assertEqual(load_snapshot(path), plan.before)
                self.assertEqual(manager.progress.backup_path, path.absolute())
            self.assertTrue(manager.progress.possibly_changed)
            self.assertFalse(manager.progress.data_verified)
            self.assertEqual((device.active, device.reader), (2, False))
            with self.assertRaises(SlotProfileError):
                manager.apply(plan, Path("not-created.json"))

    def test_drop_flash_persistence_cannot_pass_reload_verification(self):
        device = FakeSlots()
        manager = SlotProfileManager(device)
        plan = manager.plan_import(8, profile(9))
        device.drop_persist = True
        with (
            tempfile.TemporaryDirectory() as directory,
            self.assertRaisesRegex(SlotProfileError, "reloaded"),
        ):
            manager.apply(plan, Path(directory) / "before.json")
        self.assertFalse(manager.progress.data_verified)
        self.assertEqual((device.active, device.reader), (2, False))

    def test_prng_setter_echo_is_checked_instead_of_ignored(self):
        for data in (b"", b"\x02"):
            device = FakeSlots()
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile(9))
            device.fault = lambda fake, cmd, payload: (
                Response(cmd, Status.SUCCESS, data)
                if cmd == Command.MF1_SET_PRNG_TYPE
                else None
            )
            with tempfile.TemporaryDirectory() as directory:
                with self.subTest(data=data), self.assertRaises(SlotProfileError):
                    manager.apply(plan, Path(directory) / "before.json")
            self.assertTrue(manager.progress.possibly_changed)
            self.assertFalse(manager.progress.data_verified)
            self.assertEqual((device.active, device.reader), (2, False))

    def test_cancel_retains_progress_and_context(self):
        device = FakeSlots()
        manager = SlotProfileManager(device)
        plan = manager.plan_import(8, profile(9))
        device.fault = (
            lambda fake, cmd, data: KeyboardInterrupt()
            if cmd == Command.MF1_WRITE_EMU_BLOCK_DATA
            else None
        )
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(KeyboardInterrupt) as caught:
                manager.apply(plan, Path(directory) / "before.json")
        self.assertEqual(getattr(caught.exception, "slot_progress"), manager.progress)
        self.assertTrue(manager.progress.possibly_changed)
        self.assertEqual((device.active, device.reader), (2, False))

    def test_original_reader_mode_is_preserved_and_unchanged_import_has_no_setters(
        self,
    ):
        device = FakeSlots()
        device.reader = True
        device.types[7] = 1001
        device.enabled[7] = True
        device.flash[7] = profile(9)
        manager = SlotProfileManager(device)
        plan = manager.plan_import(8, profile(9))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unused.json"
            manager.apply(plan, path)
            self.assertFalse(path.exists())
        self.assertEqual(device.setters(), [])
        self.assertEqual((device.active, device.reader), (2, True))

    def test_failed_slot_cleanup_leaves_reader_and_does_not_expose_target(self):
        device = FakeSlots()
        manager = SlotProfileManager(device)
        armed = []

        def fail(fake, cmd, data):
            if cmd == Command.MF1_READ_EMU_BLOCK_DATA:
                armed.append(True)
                return TimeoutError("read failed")
            if cmd == Command.SET_ACTIVE_SLOT and data == b"\x02" and armed:
                return TimeoutError("cannot restore active")
            return None

        device.types[7] = 1001
        device.flash[7] = profile(7)
        device.fault = fail
        with self.assertRaisesRegex(
            SlotProfileError, "restoring host context"
        ) as caught:
            manager.plan_import(8, profile(9))
        self.assertIn("read failed", str(caught.exception))
        self.assertTrue(device.reader)
        self.assertEqual(device.active, 7)

    def test_bad_status_malformed_capture_and_missing_capability_refuse(self):
        for response in (
            Response(0, Status.PAR_ERR),
            Response(0, Status.SUCCESS, b"x"),
        ):
            device = FakeSlots()
            device.fault = (
                lambda fake, cmd, data: response
                if cmd == Command.GET_SLOT_INFO
                else None
            )
            with (
                self.subTest(response=response.status),
                self.assertRaises(SlotProfileError),
            ):
                SlotProfileManager(device).plan_import(8, profile())
            self.assertEqual(device.setters(), [])
        device = FakeSlots()
        device.commands = []
        with self.assertRaises(SlotProfileError):
            SlotProfileManager(device).plan_import(8, profile())
        self.assertEqual(device.calls, [])

    def test_backup_failures_prevent_all_data_mutations(self):
        for fault in ("disk", "corrupt"):
            device = FakeSlots()
            manager = SlotProfileManager(device)
            plan = manager.plan_import(8, profile())
            replacement = (
                patch("slot_profile.save_snapshot", side_effect=OSError("disk full"))
                if fault == "disk"
                else patch(
                    "slot_profile.load_snapshot",
                    return_value=replace(plan.before, slot=7),
                )
            )
            with tempfile.TemporaryDirectory() as directory, replacement:
                with (
                    self.subTest(fault=fault),
                    self.assertRaises(STORAGE_ERRORS),
                ):
                    manager.apply(plan, Path(directory) / "before.json")
            self.assertEqual(device.setters(), [])
            self.assertFalse(manager.progress.possibly_changed)
            self.assertEqual((device.active, device.reader), (2, False))


if __name__ == "__main__":
    TestProgram()
