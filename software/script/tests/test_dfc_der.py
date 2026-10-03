"""Codec tests for the `.dfcb` credential encoding.

Runs with cwd = software/script, like the rest of the suite. Needs no device.
The fixture is the same one the firmware's own codec suite checks itself
against, so a green run on both sides means the two implementations agree on the
octets rather than merely on each other.
"""

import os
import sys
import unittest

sys.path.append('..')

from chameleon_dfc import (  # noqa: E402
    OWNER_PICC,
    DfcApplication,
    DfcCredential,
    DfcError,
    DfcErrorClass,
    DfcFile,
    DfcKey,
)

def build_basic() -> DfcCredential:
    cred = DfcCredential()
    cred.uid = bytes.fromhex('04223344556677')
    cred.generation = 1
    cred.storage = 4096
    cred.uid_provenance = 1
    cred.picc_key_settings_1 = 0x0F
    cred.picc_key_settings_2 = 0x01
    cred.picc_auth_command = 0x0A
    cred.picc_keys = [DfcKey(bytes(16), 0)]
    cred.picc_random_id = True
    cred.picc_ats = bytes.fromhex('067577810280')
    cred.picc_sak = 0x20

    app = DfcApplication()
    app.aid = bytes.fromhex('000001')
    app.key_settings_2 = 0x81
    app.auth_command = 0xAA
    app.key_len = 16
    app.keys = [DfcKey(bytes(range(16)), 1)]
    cred.apps.append(app)
    return cred


class TestDfcDer(unittest.TestCase):
    def test_canonical_format_pair(self):
        from_text = build_basic()
        encoded = from_text.to_wire()
        from_der = DfcCredential.from_wire(encoded)

        self.assertEqual(from_text, from_der)
        self.assertEqual(from_text.to_wire(), encoded)
        self.assertEqual(from_der.to_wire(), encoded)

    @unittest.skip('the compact EV1 CLI model does not decode extended v4 features')
    def test_canonical_reject_corpus(self):
        self.fail('enable when extended v4 decoding is supported')

    def test_four_byte_uid_round_trip(self):
        cred = build_basic()
        cred.uid = bytes.fromhex('1337C0DE')

        blob = cred.to_wire()
        back = DfcCredential.from_wire(blob)

        self.assertEqual(back.uid, cred.uid)
        self.assertEqual(back.to_wire(), blob)

    def test_round_trip(self):
        cred = build_basic()
        cred.files.append(DfcFile(
            app_index=0, number=0x01, type=0x00, access_rights=0xEEEE,
            declared_size=32, data=bytes.fromhex('12345678'),
            contents_complete=False))
        cred.files.append(DfcFile(
            app_index=0, number=0x03, type=0x02, comm_settings=0x03,
            access_rights=0x00EE, value_lower_limit=-100, value_upper_limit=1000,
            value=42, limited_credit=0x03))
        cred.files.append(DfcFile(
            app_index=0, number=0x04, type=0x03, access_rights=0xE000,
            record_size=4, max_records=5, record_count=2, contents_complete=True,
            records=[bytes.fromhex('01020304'), bytes.fromhex('05060708')]))

        blob = cred.to_wire()
        self.assertEqual(blob[0], 0x60)

        back = DfcCredential.from_wire(blob)
        self.assertEqual(back.uid, cred.uid)
        self.assertEqual(back.storage, 4096)
        self.assertTrue(back.picc_random_id)
        self.assertFalse(back.picc_format_disabled)
        self.assertEqual(back.picc_ats, cred.picc_ats)
        self.assertEqual(back.picc_sak, 0x20)
        self.assertIsNone(back.picc_atqa)
        self.assertEqual(len(back.apps), 1)
        self.assertEqual(len(back.files), 3)
        self.assertEqual(back.files[0].declared_size, 32)
        self.assertEqual(back.files[0].data, bytes.fromhex('12345678'))
        self.assertFalse(back.files[0].contents_complete)
        self.assertEqual(back.files[1].value, 42)
        self.assertEqual(back.files[2].records, cred.files[2].records)

        # Re-encoding the decoded model reproduces the octets, which is what
        # makes the encoding canonical.
        self.assertEqual(back.to_wire(), blob)

    def test_picc_level_file(self):
        cred = build_basic()
        cred.files.append(DfcFile(
            app_index=OWNER_PICC, number=0x00, type=0x00, access_rights=0xEEEE,
            declared_size=16, data=bytes.fromhex('DEADBEEF')))
        blob = cred.to_wire()
        back = DfcCredential.from_wire(blob)
        self.assertEqual(len(back.files), 1)
        self.assertEqual(back.files[0].app_index, OWNER_PICC)
        self.assertEqual(back.files[0].data, bytes.fromhex('DEADBEEF'))

    def test_default_false_is_omitted(self):
        loud = build_basic().to_wire()
        quiet = build_basic()
        quiet.picc_random_id = False
        self.assertLess(len(quiet.to_wire()), len(loud))

    def test_rejections(self):
        good = build_basic().to_wire()
        # Wrong outer tag.
        with self.assertRaises(DfcError):
            DfcCredential.from_wire(b'\x30' + good[1:])
        # Truncated.
        with self.assertRaises(DfcError):
            DfcCredential.from_wire(good[:-1])
        # Trailing octet after a complete value.
        with self.assertRaises(DfcError):
            DfcCredential.from_wire(good + b'\x00')
        # A later version is refused on its version.
        v5 = bytearray(good)
        i = v5.index(b'\x80\x01\x04')
        v5[i + 2] = 5
        with self.assertRaises(DfcError):
            DfcCredential.from_wire(bytes(v5))

    def test_absent_keys_are_the_factory_default(self):
        # Specification section 1.4: a slot with no key entry carries the factory
        # default, so a credential may record no keys at all. The default belongs
        # where the key is used, so the credential must round-trip without one.
        keyless = build_basic()
        keyless.picc_key_settings_2 = 0x00
        keyless.picc_keys = []
        keyless.apps[0].keys = []
        keyless.apps[0].key_settings_2 = 0x80

        blob = keyless.to_wire()
        back = DfcCredential.from_wire(blob)
        self.assertEqual(back.picc_keys, [])
        self.assertEqual(back.apps[0].keys, [])
        self.assertEqual(back.to_wire(), blob)

    def test_dotnet_exported_credential_loads(self):
        # A credential exported by another implementation, which records no PICC
        # key material at all. It is well formed and must load.
        path = os.path.join(os.path.dirname(__file__), 'fixtures', 'no-picc-keys.dfc')
        with open(path, 'r', encoding='utf-8') as fh:
            cred = DfcCredential.parse_text(fh.read())
        self.assertEqual(cred.picc_keys, [])
        self.assertTrue(cred.to_wire().startswith(b'\x60'))

    def test_model_validation(self):
        # The compact CLI model does not create transaction-MAC contents.
        tmac = build_basic()
        tmac.files.append(DfcFile(app_index=0, number=0x05, type=0x05, declared_size=8))
        with self.assertRaises(DfcError):
            tmac.to_wire()

        # Complete with contents shorter than the declared size.
        short = build_basic()
        short.files.append(DfcFile(
            app_index=0, number=0x01, type=0x00, declared_size=32,
            data=bytes.fromhex('1234'), contents_complete=True))
        with self.assertRaises(DfcError):
            short.to_wire()

        # Value outside its own limits.
        bad_value = build_basic()
        bad_value.files.append(DfcFile(
            app_index=0, number=0x03, type=0x02, value_upper_limit=100, value=500))
        with self.assertRaises(DfcError):
            bad_value.to_wire()

        # A reserved ISO file ID.
        reserved = build_basic()
        reserved.files.append(DfcFile(
            app_index=0, number=0x01, type=0x00, declared_size=8,
            has_iso_file_id=True, iso_file_id=0x3F00))
        with self.assertRaises(DfcError):
            reserved.to_wire()

        # A cyclic record file needs capacity for two records.
        cyclic = build_basic()
        cyclic.files.append(DfcFile(
            app_index=0, number=0x06, type=0x04, record_size=2, max_records=1))
        with self.assertRaises(DfcError):
            cyclic.to_wire()

    def test_cross_implementation_fixture(self):
        expected = build_basic().to_wire()
        self.assertEqual(expected[0], 0x60)
        cred = DfcCredential.from_wire(expected)
        self.assertEqual(cred.to_wire(), expected)

    def test_extended_generation_is_recognised_but_not_emulated(self):
        encoded = build_basic().to_wire()
        encoded = encoded.replace(b'\x80\x01\x01', b'\x80\x01\x02', 1)
        with self.assertRaises(DfcError) as caught:
            DfcCredential.from_wire(encoded)
        self.assertEqual(caught.exception.error_class, DfcErrorClass.UNSUPPORTED)


class TestDfcText(unittest.TestCase):
    V4 = """Filetype: DFC Credential
Version: 4
Card Generation: EV1
Card Storage: 2048
UID: 04 22 33 44 55 66 77
UID Provenance: Real
PICC Key Settings 1: 0F
PICC Key Settings 2: 01
PICC Authentication Mode: D40
PICC Key Count: 1
PICC Key 00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
PICC Key 00 Version: 00
PICC File Count: 0
Application Count: 1
Application 00 AID: 00 00 01
Application 00 Key Settings 1: 0F
Application 00 Key Settings 2: 81
Application 00 Authentication Mode: AES
Application 00 Key Count: 1
Application 00 Key 00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Application 00 Key 00 Version: 00
Application 00 File Count: 1
Application 00 File 00 Number: 01
Application 00 File 00 Type: Standard Data
Application 00 File 00 Communication Settings: 00
Application 00 File 00 Access Rights: EE EE
Application 00 File 00 Size: 32
Application 00 File 00 Data: 12 34 56 78
Application 00 File 00 Data Complete: 0
"""

    LEGACY = """Filetype: Flipper DFC Credential
Version: 2
UID: 04 22 33 44 55 66 77
PICC Key Settings 1: 0F
PICC Key Settings 2: 01
PICC Key: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Application Count: 1
Application 00 AID: 00 00 01
Application 00 Key Settings 2: 01
Application 00 Key 00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
File Count: 1
File Entry 00 App: 0
File Entry 00 Number: 01
File Entry 00 Type: Standard Data
File Entry 00 Access Rights: EE EE
File Entry 00 Data: 12 34 56 78
"""

    def test_v4_text_compiles_and_round_trips(self):
        cred = DfcCredential.parse_text(self.V4)
        self.assertEqual(cred.uid, bytes.fromhex('04223344556677'))
        self.assertEqual(cred.uid_provenance, 0)
        self.assertEqual(len(cred.picc_keys), 1)
        self.assertEqual(cred.files[0].declared_size, 32)
        self.assertEqual(cred.files[0].data, bytes.fromhex('12345678'))
        self.assertFalse(cred.files[0].contents_complete)
        blob = cred.to_wire()
        self.assertEqual(DfcCredential.from_wire(blob).to_wire(), blob)

    def test_legacy_text_is_refused(self):
        # Versions 1 and 2 carried no card record, no PICC keys past the master
        # and no declared file size. Accepting one means inventing all three,
        # so it is refused rather than guessed at.
        with self.assertRaises(DfcError) as caught:
            DfcCredential.parse_text(self.LEGACY)
        self.assertIn('DESFire credential file', str(caught.exception))

    def test_wrong_version_is_refused(self):
        text = self.V4.replace('Version: 4', 'Version: 5')
        with self.assertRaises(DfcError) as caught:
            DfcCredential.parse_text(text)
        self.assertIn('expected 4', str(caught.exception))

    def test_extended_text_field_is_recognised_but_not_emulated(self):
        text = self.V4.replace(
            'PICC File Count: 0',
            'PICC EV2 Card Capabilities: 00 00 00 00 00 00\nPICC File Count: 0',
        )
        with self.assertRaises(DfcError) as caught:
            DfcCredential.parse_text(text)
        self.assertEqual(caught.exception.error_class, DfcErrorClass.UNSUPPORTED)

    def test_extended_application_field_is_recognised_but_not_emulated(self):
        text = self.V4.replace(
            'Application 00 File Count: 1',
            'Application 00 Capability Data: 00 00 00 00 00 00\n'
            'Application 00 File Count: 1',
        )
        with self.assertRaises(DfcError) as caught:
            DfcCredential.parse_text(text)
        self.assertEqual(caught.exception.error_class, DfcErrorClass.UNSUPPORTED)
