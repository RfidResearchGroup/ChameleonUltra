import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from chameleon_enum import Command
from ntag_profile import NtagProfileManager
from ntag_profile_data import (
    NtagConfig,
    NtagProfileError,
    PAGE_COUNTS,
    READ_PROFILE,
    WRITE_PROFILE,
    SlotSnapshot,
    SlotState,
    validate_profile,
)
from ntag_profile_io import load_snapshot, save_snapshot
from tests.ntag_profile_fixtures import FakeNtagDevice, profile

MUTATIONS = {
    Command.SET_SLOT_DATA_DEFAULT,
    Command.SET_SLOT_TAG_TYPE,
    Command.SET_SLOT_ENABLE,
    Command.DELETE_SLOT_SENSE_TYPE,
    Command.HF14A_SET_ANTI_COLL_DATA,
    Command.SLOT_DATA_CONFIG_SAVE,
    WRITE_PROFILE,
}


class NtagProfileTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.device = FakeNtagDevice()

    def manager(self):
        return NtagProfileManager(self.device)

    def test_all_nine_types_import_reload_and_restore_empty(self):
        for kind in PAGE_COUNTS:
            with self.subTest(kind=kind):
                before = copy.deepcopy(self.device.types[:-1])
                desired = profile(kind, seed=kind % 256)
                manager = self.manager()
                plan = manager.plan_import(8, desired)
                path = self.folder / f"before-{kind}.json"
                manager.apply(plan, path)
                self.assertEqual(self.device.flash[7], desired)
                self.assertEqual(self.device.types[:-1], before)
                self.assertEqual((self.device.active, self.device.mode), (0, 0))
                recovery = self.manager()
                recovery.apply(
                    recovery.plan_restore(load_snapshot(path)),
                    self.folder / f"recovery-{kind}.json",
                )
                self.assertEqual(self.device.types[7], [0, 0])
                self.assertNotIn(7, self.device.flash)
        self.assertTrue(all(slot == 0 for slot in self.device.global_saves))

    def test_complete_private_pages_and_reserved_config_roundtrip(self):
        self.device.populate(7, 1102)
        target = self.device.flash[7]
        snapshot = self.manager().backup(8, self.folder / "slot.json")
        self.assertEqual(snapshot.state.profile, target)
        assert snapshot.state.profile is not None
        self.assertEqual(len(snapshot.state.profile.image), 968)
        self.assertEqual(snapshot.state.profile.config.raw, b"\xf3\xab\xcd")
        self.assertEqual(load_snapshot(self.folder / "slot.json"), snapshot)

    def test_preview_has_no_profile_setters_or_save(self):
        self.device.populate(7)
        self.manager().plan_import(8, profile(seed=47))
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_original_legacy_prng_is_never_normalized(self):
        manager = self.manager()
        manager.apply(manager.plan_import(8, profile()), self.folder / "before.json")
        self.assertEqual(self.device.classic_prng, 3)
        self.assertFalse(
            any(
                cmd in (Command.MF1_SET_PRNG_TYPE, Command.MF1_GET_PRNG_TYPE)
                for cmd, _ in self.device.calls
            )
        )

    def test_current_slot_and_dual_frequency_targets_are_rejected(self):
        for slot, lf, enabled in ((1, 0, False), (8, 100, False), (8, 0, True)):
            self.device.types[7][1] = lf
            self.device.enabled[7][1] = enabled
            with self.assertRaises(NtagProfileError):
                self.manager().plan_import(slot, profile())
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_original_classic_shadow_is_not_discarded(self):
        self.device.classic_flags = b"\x00\x00\x00\x00\x03"
        with self.assertRaisesRegex(NtagProfileError, "Shadow"):
            self.manager().plan_import(8, profile())
        self.assertEqual(self.device.active, 0)

    def test_target_shadow_is_rejected(self):
        self.device.populate(7)
        self.device.flash[7] = replace(
            self.device.flash[7], config=NtagConfig(b"\x06\x00\x00")
        )
        with self.assertRaisesRegex(NtagProfileError, "Shadow"):
            self.manager().plan_import(8, profile())
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_authentication_logs_are_preserved(self):
        self.device.ntag_log = 3
        with self.assertRaisesRegex(NtagProfileError, "logs"):
            self.manager().plan_import(8, profile())
        self.assertEqual(self.device.ntag_log, 3)

    def test_physical_pages_only_are_not_a_complete_profile(self):
        desired = profile()
        with self.assertRaisesRegex(NtagProfileError, "private"):
            validate_profile(replace(desired, image=desired.image[: 45 * 4]))

    def test_corrupt_descriptor_is_not_accepted(self):
        self.device.populate(7)
        self.device.corrupt_descriptor = True
        with self.assertRaisesRegex(NtagProfileError, "descriptor"):
            self.manager().plan_import(8, profile())
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_missing_capability_does_not_probe_or_write(self):
        self.device.commands.remove(READ_PROFILE)
        with self.assertRaisesRegex(NtagProfileError, "lacks"):
            self.manager().plan_import(8, profile())
        self.assertEqual(self.device.calls, [])

    def test_stale_inventory_is_not_applied(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.enabled[1][0] = True
        with self.assertRaisesRegex(NtagProfileError, "stale"):
            manager.apply(plan, self.folder / "before.json")
        self.assertFalse((self.folder / "before.json").exists())

    def test_target_change_after_preview_is_not_applied(self):
        self.device.populate(7)
        manager = self.manager()
        plan = manager.plan_import(8, profile(seed=49))
        self.device.flash[7] = profile(seed=72)
        with self.assertRaisesRegex(NtagProfileError, "preview"):
            manager.apply(plan, self.folder / "before.json")
        self.assertFalse((self.folder / "before.json").exists())

    def test_backup_storage_failure_precedes_first_destructive_command(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        with patch("ntag_profile_io.os.fsync", side_effect=OSError("storage failure")):
            with self.assertRaises(NtagProfileError):
                manager.apply(plan, self.folder / "before.json")
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_existing_backup_is_not_overwritten(self):
        path = self.folder / "exists"
        path.write_bytes(b"keep")
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        with self.assertRaises(NtagProfileError):
            manager.apply(plan, path)
        self.assertEqual(path.read_bytes(), b"keep")

    def test_partial_write_failure_leaves_verified_recovery_file(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.fail_command = WRITE_PROFILE
        path = self.folder / "before.json"
        with self.assertRaises(NtagProfileError):
            manager.apply(plan, path)
        self.assertTrue(path.exists())
        self.assertEqual(load_snapshot(path).state, SlotState(0, False))
        self.assertTrue(manager.progress.possibly_changed)
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_cancellation_survives_secondary_context_failure(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.fail_command = WRITE_PROFILE
        self.device.failure = KeyboardInterrupt()
        self.device.fail_origin_selection = True
        with self.assertRaises(KeyboardInterrupt) as raised:
            manager.apply(plan, self.folder / "before.json")
        self.assertIs(raised.exception, self.device.failure)
        self.assertEqual(self.device.mode, 1)
        self.assertTrue((self.folder / "before.json").exists())

    def test_backup_readback_failure_precedes_mutation(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        different = SlotSnapshot("f" * 64, 8, SlotState(0, False))
        with patch("ntag_profile_io.load_snapshot", return_value=different):
            with self.assertRaises(NtagProfileError):
                manager.apply(plan, self.folder / "before.json")
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_corrupt_private_page_persistence_is_detected(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.corrupt_persistence = True
        with self.assertRaisesRegex(NtagProfileError, "differs"):
            manager.apply(plan, self.folder / "before.json")
        self.assertFalse(manager.progress.data_verified)

    def test_wrong_device_restore_is_rejected(self):
        snap = SlotSnapshot("f" * 64, 8, SlotState(0, False))
        with self.assertRaisesRegex(NtagProfileError, "another device"):
            self.manager().plan_restore(snap)

    def test_disabled_restore_reselects_after_auto_selection(self):
        self.device.populate(7, enabled=False)
        path = self.folder / "original.json"
        original = self.manager().backup(8, path)
        manager = self.manager()
        manager.apply(
            manager.plan_import(8, profile(seed=67)), self.folder / "before.json"
        )
        manager = self.manager()
        manager.apply(manager.plan_restore(original), self.folder / "recovery.json")
        self.assertFalse(self.device.enabled[7][0])
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_no_difference_does_not_write_or_create_backup(self):
        self.device.populate(7)
        manager = self.manager()
        plan = manager.plan_import(8, self.device.flash[7])
        manager.apply(plan, self.folder / "unused.json")
        self.assertFalse((self.folder / "unused.json").exists())
        self.assertFalse(any(cmd in MUTATIONS for cmd, _ in self.device.calls))

    def test_context_switch_during_setter_stops_transaction(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.switch_after_write = True
        with self.assertRaisesRegex(NtagProfileError, "changed"):
            manager.apply(plan, self.folder / "before.json")
        self.assertEqual((self.device.active, self.device.mode), (0, 0))

    def test_origin_restore_failure_keeps_reader_mode(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        self.device.fail_origin_selection = True
        with self.assertRaises(NtagProfileError):
            manager.apply(plan, self.folder / "before.json")
        self.assertEqual(self.device.mode, 1)

    def test_single_use_manager_cannot_retry_partial_apply(self):
        manager = self.manager()
        plan = manager.plan_import(8, profile())
        manager.apply(plan, self.folder / "before.json")
        with self.assertRaisesRegex(NtagProfileError, "new manager"):
            manager.apply(plan, self.folder / "retry.json")

    def test_snapshot_corruption_duplicate_and_oversize_are_rejected(self):
        snap = SlotSnapshot("1" * 64, 8, SlotState(1100, True, profile()))
        path = self.folder / "snapshot.json"
        save_snapshot(snap, path)
        value = json.loads(path.read_text())
        value["profile"]["config"] = "020000"
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(NtagProfileError, "checksum"):
            load_snapshot(path)
        path.write_text('{"schema":"x","schema":"y"}')
        with self.assertRaisesRegex(NtagProfileError, "duplicate"):
            load_snapshot(path)
        path.write_bytes(b" " * 32769)
        with self.assertRaisesRegex(NtagProfileError, "large"):
            load_snapshot(path)
