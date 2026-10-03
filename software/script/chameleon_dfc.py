"""DESFire credential handling: `.dfc` text parsing and the `.dfcb` wire format.

Device-independent on purpose, so it can be unit tested and used offline via
`hf des parse`.

Two encodings are involved. `.dfc` is line-oriented `Key: Value` text, the form
a human edits. `.dfcb` is the binary form the device consumes: a restricted DER
profile with implicit context tags, one canonical encoding per credential. The
device carries no text parser, so this module compiles text to octets before a
transfer.

Properties of the text grammar that matter and are easy to get wrong:

* Version 4 is the only version. Every container is spelled with an index
  (`Application 00 Key 00`) and a file's declared `Size` is stated separately
  from its known `Data`. Anything declaring another version is refused rather
  than read, because earlier versions carried no card record, no PICC keys past
  the master and no declared size, so reading one means inventing all three.
* Key length is never stored. It is derived from the Key Settings 2 crypto-type
  bits, and a key whose hex length disagrees is an error rather than something
  to pad or truncate.
* A repeated key is an error, not a first-occurrence-wins scan. Line order is
  otherwise irrelevant among recognised keys.

Properties of the binary encoding that matter:

* Lengths are definite and minimal, integers are minimal two's complement, and
  booleans are `00` or `FF`. Two conforming writers given the same model emit
  identical octets, which is what makes a cross-implementation comparison
  meaningful.
* `PICC Random ID` and `PICC Format Disabled` default to false, and DER omits a
  component equal to its default, so a false one is absent rather than `00`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from enum import Enum

# Version 4 is the only format, and it declares exactly one Filetype.
DFC_FILETYPE = "DFC Credential"
DFC_VERSION = 4

# Largest encoded credential. Bounds every length to two octets.
DER_MAX_SIZE = 65535

KEY_TYPE_MASK = 0xC0
KEY_TYPE_3K3DES = 0x40
KEY_TYPE_AES = 0x80
NUM_KEYS_MASK = 0x0F

UID_LEN = 7
UID_LENGTHS = (4, 7)
UID_FIRST_BYTE = 0x04
MAX_KEY_LEN = 24
MAX_KEYS = 14
U24_MAX = 16777215
U32_MAX = 4294967295
# The reference parser caps a single file payload at this size.
MAX_FILE_DATA_PER_ENTRY = 512
# User memory a credential advertises when the text does not say.
DEFAULT_CARD_STORAGE = 2048

# Owner of a file that lives at PICC level rather than inside an application.
OWNER_PICC = -1

FILE_TYPE_STANDARD = 0x00
FILE_TYPE_BACKUP = 0x01
FILE_TYPE_VALUE = 0x02
FILE_TYPE_LINEAR = 0x03
FILE_TYPE_CYCLIC = 0x04

FILE_TYPES = {
    "Standard Data": FILE_TYPE_STANDARD,
    "Backup Data": FILE_TYPE_BACKUP,
    "Value": FILE_TYPE_VALUE,
    "Linear Record": FILE_TYPE_LINEAR,
    "Cyclic Record": FILE_TYPE_CYCLIC,
}

GENERATIONS = {"EV1": 1, "EV2": 2, "EV3": 3}
PROVENANCES = {"Real": 0, "Random": 1, "Unknown": 2}

# ISO file IDs that name the master file or are otherwise reserved, so they
# cannot identify an application or a file.
RESERVED_ISO_FILE_IDS = (0x0000, 0x3F00, 0x3FFF, 0xFFFF)

# Exactly the names the engine accepts, including its aliases. These are the
# values that appear in real .dfc files -- do not substitute the cipher names,
# which is a different vocabulary.
AUTH_MODES = {
    "D40": 0x0A,
    "Legacy": 0x0A,
    "NativeD40": 0x0A,
    "Native-D40": 0x0A,
    "ISO": 0x1A,
    "AES": 0xAA,
}
# Authentication mode as the binary encoding numbers it.
AUTH_MODE_CODES = {0x0A: 0, 0x1A: 1, 0xAA: 2}
AUTH_COMMANDS_BY_CODE = {0: 0x0A, 1: 0x1A, 2: 0xAA}


class DfcErrorClass(str, Enum):
    """Stable failure classes defined by DFC format version 4."""

    MALFORMED = "malformed"
    UNSUPPORTED = "unsupported"
    CAPACITY = "capacity"


class DfcError(Exception):
    """A `.dfc` file or `.dfcb` blob that cannot be used as-is."""

    def __init__(
        self,
        message: str,
        error_class: DfcErrorClass = DfcErrorClass.MALFORMED,
    ) -> None:
        super().__init__(message)
        self.error_class = error_class


def key_length_for_ks2(ks2: int) -> int:
    """Mirror of the engine's key-length derivation."""
    crypto = ks2 & KEY_TYPE_MASK
    if crypto == KEY_TYPE_3K3DES:
        return 24
    return 16


def auth_command_for_ks2(ks2: int) -> int:
    crypto = ks2 & KEY_TYPE_MASK
    if crypto == KEY_TYPE_AES:
        return 0xAA
    if crypto == KEY_TYPE_3K3DES:
        return 0x1A
    return 0x0A


@dataclass
class DfcKey:
    value: bytes = b""
    version: int = 0


@dataclass
class DfcApplication:
    aid: bytes = b"\x00\x00\x00"
    iso_aid: bytes = b""
    has_iso_file_id: bool = False
    iso_file_id: int = 0
    key_settings_1: int = 0x0F
    key_settings_2: int = 0x01
    auth_command: int = 0x0A
    key_len: int = 16
    keys: list[DfcKey] = field(default_factory=list)


@dataclass
class DfcFile:
    app_index: int = 0
    number: int = 0
    type: int = FILE_TYPE_STANDARD
    comm_settings: int = 0x00
    access_rights: int = 0x0000
    has_iso_file_id: bool = False
    iso_file_id: int = 0
    # Declared allocation, which is what a read returns. `data` is the contents
    # actually known and may be shorter.
    declared_size: int = 0
    data: bytes = b""
    contents_complete: bool = False
    value_lower_limit: int = 0
    value_upper_limit: int = 0
    value: int = 0
    limited_credit: int = 0
    record_size: int = 0
    max_records: int = 0
    record_count: int = 0
    # Leading known records, in card order. Fewer than record_count is legal.
    records: list[bytes] = field(default_factory=list)


@dataclass
class DfcCredential:
    uid: bytes = b""
    generation: int = 1
    storage: int = DEFAULT_CARD_STORAGE
    uid_provenance: int = 2
    picc_key_settings_1: int = 0x0F
    picc_key_settings_2: int = 0x01
    picc_auth_command: int = 0x0A
    picc_keys: list[DfcKey] = field(default_factory=list)
    picc_random_id: bool = False
    picc_format_disabled: bool = False
    picc_ats: bytes = b""
    picc_sak: int | None = None
    picc_atqa: bytes | None = None
    picc_sm_disable: int | None = None
    apps: list[DfcApplication] = field(default_factory=list)
    files: list[DfcFile] = field(default_factory=list)

    @property
    def picc_key_len(self) -> int:
        return key_length_for_ks2(self.picc_key_settings_2)

    # ---------------------------------------------------------------- text --

    @classmethod
    def parse_text(cls, text: str) -> "DfcCredential":
        fields = _scan_fields(text)

        filetype = fields.get("Filetype")
        if filetype != DFC_FILETYPE:
            raise DfcError(f"not a DESFire credential file (Filetype: {filetype!r})")
        version = _parse_int(fields.get("Version", "0"))
        if version != DFC_VERSION:
            # Earlier versions carried no card record, no PICC keys beyond the
            # master, and no declared file size. Reading one means inventing
            # those, and inventing them is what this format exists to stop.
            raise DfcError(
                f"unsupported .dfc version {version}, expected {DFC_VERSION}",
                DfcErrorClass.UNSUPPORTED,
            )
        _validate_text_keys(fields)
        return _parse_v4(cls, fields)

    # ---------------------------------------------------------------- wire --

    def to_wire(self) -> bytes:
        """Encode as `.dfcb` octets, the form the device accepts."""
        return der_encode(self)

    @classmethod
    def from_wire(cls, blob: bytes) -> "DfcCredential":
        """Decode `.dfcb` octets, as the device returns them."""
        return der_decode(blob, cls)

    # --------------------------------------------------------------- report --

    def describe(self) -> str:
        generation = _name_of(GENERATIONS, self.generation, f"gen {self.generation}")
        provenance = _name_of(PROVENANCES, self.uid_provenance, "?")
        lines = [
            f"UID: {self.uid.hex().upper()} ({provenance})",
            f"Card: {generation}, {self.storage} bytes of user memory",
        ]
        picc = (
            f"PICC: key settings {self.picc_key_settings_1:02X}/"
            f"{self.picc_key_settings_2:02X}, auth 0x{self.picc_auth_command:02X}, "
            f"{len(self.picc_keys)} key(s) of {self.picc_key_len} bytes"
        )
        extras = []
        if self.picc_random_id:
            extras.append("random ID")
        if self.picc_format_disabled:
            extras.append("format disabled")
        if self.picc_ats:
            extras.append(f"ATS {self.picc_ats.hex().upper()}")
        if self.picc_sak is not None:
            extras.append(f"SAK {self.picc_sak:02X}")
        if self.picc_atqa is not None:
            extras.append(f"ATQA {self.picc_atqa.hex().upper()}")
        if self.picc_sm_disable is not None:
            extras.append(f"SM disable {self.picc_sm_disable:02X}")
        if extras:
            picc += " [" + ", ".join(extras) + "]"
        lines.append(picc)

        lines.append(f"Applications: {len(self.apps)}")
        for i, app in enumerate(self.apps):
            desc = (
                f"  [{i}] AID {app.aid.hex().upper()} "
                f"keys {len(app.keys)}x{app.key_len}B auth 0x{app.auth_command:02X}"
            )
            if app.has_iso_file_id:
                desc += f" ISO FID {app.iso_file_id:04X}"
            if app.iso_aid:
                desc += f" DF name {app.iso_aid.hex().upper()}"
            lines.append(desc)

        lines.append(f"Files: {len(self.files)}")
        type_names = {v: k for k, v in FILE_TYPES.items()}
        for f in self.files:
            owner = "PICC" if f.app_index == OWNER_PICC else f"app {f.app_index}"
            desc = (
                f"  {owner} file {f.number:02X} "
                f"{type_names.get(f.type, f'type {f.type}')} "
                f"comm {f.comm_settings:02X} access {f.access_rights:04X}"
            )
            if f.type == FILE_TYPE_VALUE:
                desc += f" value {f.value} [{f.value_lower_limit}..{f.value_upper_limit}]"
            elif f.type in (FILE_TYPE_LINEAR, FILE_TYPE_CYCLIC):
                desc += (
                    f" records {f.record_count}/{f.max_records} of {f.record_size}B"
                    f", {len(f.records)} known"
                )
            else:
                desc += f" size {f.declared_size}B, {len(f.data)}B known"
                if f.contents_complete:
                    desc += " (complete)"
            lines.append(desc)
        return "\n".join(lines)


# ------------------------------------------------------------- DER writer --


def _der_len(n: int) -> bytes:
    if n < 128:
        return bytes([n])
    if n < 256:
        return bytes([0x81, n])
    if n <= 0xFFFF:
        return bytes([0x82, n >> 8, n & 0xFF])
    raise DfcError(f"value of {n} bytes exceeds the encoding's two-octet length")


def _tlv(tag: int, body: bytes) -> bytes:
    return bytes([tag]) + _der_len(len(body)) + body


def _int_body(v: int) -> bytes:
    """Minimal two's-complement contents octets."""
    n = max(1, (v.bit_length() + 8) // 8)
    while True:
        try:
            b = v.to_bytes(n, "big", signed=True)
            break
        except OverflowError:
            n += 1
    while len(b) > 1 and (
        (b[0] == 0x00 and not b[1] & 0x80) or (b[0] == 0xFF and b[1] & 0x80)
    ):
        b = b[1:]
    return b


def _int(tag: int, v: int) -> bytes:
    return _tlv(tag, _int_body(v))


def _bool(tag: int, v: bool) -> bytes:
    return _tlv(tag, b"\xff" if v else b"\x00")


def _u16(v: int) -> bytes:
    return bytes([(v >> 8) & 0xFF, v & 0xFF])


def _encode_key(slot: int, key: DfcKey, key_len: int) -> bytes:
    value = key.value.ljust(key_len, b"\x00")[:key_len]
    body = _int(0x80, slot) + _tlv(0x81, value) + _tlv(0x82, bytes([key.version]))
    return _tlv(0x30, body)


def _encode_keys(tag: int, keys: list[DfcKey], key_len: int) -> bytes:
    body = b"".join(_encode_key(i, k, key_len) for i, k in enumerate(keys))
    return _tlv(tag, body)


def _encode_file(f: DfcFile) -> bytes:
    body = (
        _int(0x80, f.number)
        + _int(0x81, f.type)
        + _tlv(0x82, bytes([f.comm_settings]))
        + _tlv(0x83, _u16(f.access_rights))
    )
    if f.has_iso_file_id:
        body += _tlv(0x84, _u16(f.iso_file_id))

    if f.type in (FILE_TYPE_STANDARD, FILE_TYPE_BACKUP):
        contents = _int(0x80, f.declared_size)
        if f.data:
            contents += _tlv(0x81, f.data)
        contents += _bool(0x82, f.contents_complete)
        body += _tlv(0xA5, contents)
    elif f.type == FILE_TYPE_VALUE:
        contents = (
            _int(0x80, f.value_lower_limit)
            + _int(0x81, f.value_upper_limit)
            + _int(0x82, f.value)
            + _tlv(0x83, bytes([f.limited_credit]))
        )
        body += _tlv(0xA6, contents)
    else:
        records = b"".join(_tlv(0x04, r) for r in f.records)
        contents = (
            _int(0x80, f.record_size)
            + _int(0x81, f.max_records)
            + _int(0x82, f.record_count)
            + _bool(0x83, f.contents_complete)
            + _tlv(0xA4, records)
        )
        body += _tlv(0xA7, contents)
    return _tlv(0x30, body)


def _encode_files(tag: int, cred: DfcCredential, owner: int) -> bytes:
    body = b"".join(_encode_file(f) for f in cred.files if f.app_index == owner)
    return _tlv(tag, body)


def _encode_app(cred: DfcCredential, index: int, app: DfcApplication) -> bytes:
    body = _tlv(0x80, app.aid)
    if app.has_iso_file_id:
        body += _tlv(0x81, _u16(app.iso_file_id))
    if app.iso_aid:
        body += _tlv(0x82, app.iso_aid)
    body += (
        _tlv(0x83, bytes([app.key_settings_1]))
        + _tlv(0x84, bytes([app.key_settings_2]))
        + _int(0x85, AUTH_MODE_CODES.get(app.auth_command, 0))
        + _encode_keys(0xA6, app.keys, app.key_len)
        + _encode_files(0xA7, cred, index)
    )
    return _tlv(0x30, body)


def der_encode(cred: DfcCredential) -> bytes:
    """Encode `cred` as `.dfcb` octets. Raises DfcError on an invalid model."""
    _validate(cred)

    card = (
        _int(0x80, cred.generation)
        + _int(0x81, cred.storage)
        + _tlv(0x82, cred.uid)
        + _int(0x83, cred.uid_provenance)
    )
    picc = (
        _tlv(0x80, bytes([cred.picc_key_settings_1]))
        + _tlv(0x81, bytes([cred.picc_key_settings_2]))
        + _int(0x82, AUTH_MODE_CODES.get(cred.picc_auth_command, 0))
    )
    # DEFAULT FALSE: omit rather than emit an explicit false.
    if cred.picc_random_id:
        picc += _bool(0x83, True)
    if cred.picc_format_disabled:
        picc += _bool(0x84, True)
    if cred.picc_ats:
        picc += _tlv(0x85, cred.picc_ats)
    if cred.picc_sak is not None:
        picc += _tlv(0x86, bytes([cred.picc_sak]))
    if cred.picc_atqa is not None:
        picc += _tlv(0x87, cred.picc_atqa)
    if cred.picc_sm_disable is not None:
        picc += _tlv(0x88, bytes([cred.picc_sm_disable]))
    picc += _encode_keys(0xA9, cred.picc_keys, cred.picc_key_len)
    picc += _encode_files(0xAA, cred, OWNER_PICC)

    apps = b"".join(
        _encode_app(cred, i, app) for i, app in enumerate(cred.apps)
    )

    body = _int(0x80, DFC_VERSION) + _tlv(0xA1, card) + _tlv(0xA2, picc) + _tlv(0xA3, apps)
    out = _tlv(0x60, body)
    if len(out) > DER_MAX_SIZE:
        raise DfcError(f"credential encodes to {len(out)} bytes, the limit is {DER_MAX_SIZE}")
    return out


def _validate(cred: DfcCredential) -> None:
    if len(cred.uid) not in UID_LENGTHS:
        raise DfcError(f"UID must be 4 or 7 bytes, got {len(cred.uid)}")
    if cred.generation not in GENERATIONS.values():
        raise DfcError(f"unknown card generation {cred.generation}")
    if cred.generation != GENERATIONS["EV1"]:
        raise DfcError(
            f"{_name_of(GENERATIONS, cred.generation, 'this card generation')} "
            "is represented by DFC v4 but is not emulated by this firmware",
            DfcErrorClass.UNSUPPORTED,
        )
    if cred.uid_provenance not in PROVENANCES.values():
        raise DfcError(f"unknown UID provenance {cred.uid_provenance}")
    if not 0 <= cred.storage <= U32_MAX:
        raise DfcError(f"card storage {cred.storage} out of range")
    # A slot with no key entry carries the factory default (specification section
    # 1.4), so a credential may record no key material at all. The default is
    # supplied where the key is used, on the device, and is never written here.
    if len(cred.picc_keys) > MAX_KEYS:
        raise DfcError(f"{len(cred.picc_keys)} PICC keys, the limit is {MAX_KEYS}")

    seen_aids = set()
    for app in cred.apps:
        if len(app.aid) != 3:
            raise DfcError(f"AID must be 3 bytes, got {len(app.aid)}")
        if app.aid in seen_aids:
            raise DfcError(f"duplicate AID {app.aid.hex().upper()}")
        seen_aids.add(app.aid)
        if len(app.keys) > MAX_KEYS:
            raise DfcError(
                f"application {app.aid.hex().upper()} has {len(app.keys)} keys, "
                f"the limit is {MAX_KEYS}"
            )
        if len(app.iso_aid) > 16:
            raise DfcError("DF name longer than 16 bytes")
        if app.has_iso_file_id and app.iso_file_id in RESERVED_ISO_FILE_IDS:
            raise DfcError(f"ISO file ID {app.iso_file_id:04X} is reserved")

    seen_files = set()
    for f in cred.files:
        if f.app_index != OWNER_PICC and not 0 <= f.app_index < len(cred.apps):
            raise DfcError(
                f"file {f.number:02X} references application {f.app_index}, "
                f"but only {len(cred.apps)} are defined"
            )
        if not 0 <= f.number <= 0x1F:
            raise DfcError(f"file number {f.number:02X} out of range")
        if (f.app_index, f.number) in seen_files:
            raise DfcError(f"duplicate file number {f.number:02X}")
        seen_files.add((f.app_index, f.number))
        if f.has_iso_file_id and f.iso_file_id in RESERVED_ISO_FILE_IDS:
            raise DfcError(f"ISO file ID {f.iso_file_id:04X} is reserved")

        if f.type in (FILE_TYPE_STANDARD, FILE_TYPE_BACKUP):
            if not 1 <= f.declared_size <= U24_MAX:
                raise DfcError(
                    f"file {f.number:02X} declares a size of {f.declared_size}, "
                    f"expected 1 to {U24_MAX}"
                )
            if len(f.data) > f.declared_size:
                raise DfcError(
                    f"file {f.number:02X} carries {len(f.data)} bytes but declares "
                    f"only {f.declared_size}"
                )
            if f.contents_complete and len(f.data) != f.declared_size:
                raise DfcError(
                    f"file {f.number:02X} is marked complete but carries "
                    f"{len(f.data)} of {f.declared_size} bytes"
                )
        elif f.type == FILE_TYPE_VALUE:
            if not f.value_lower_limit <= f.value <= f.value_upper_limit:
                raise DfcError(
                    f"file {f.number:02X} value {f.value} is outside "
                    f"[{f.value_lower_limit}..{f.value_upper_limit}]"
                )
        elif f.type in (FILE_TYPE_LINEAR, FILE_TYPE_CYCLIC):
            if not 1 <= f.record_size <= U24_MAX:
                raise DfcError(f"file {f.number:02X} record size {f.record_size} out of range")
            if not 1 <= f.max_records <= U24_MAX:
                raise DfcError(f"file {f.number:02X} max records {f.max_records} out of range")
            if f.record_count > f.max_records:
                raise DfcError(
                    f"file {f.number:02X} holds {f.record_count} records but its "
                    f"capacity is {f.max_records}"
                )
            if f.type == FILE_TYPE_CYCLIC and f.max_records < 2:
                raise DfcError(f"cyclic file {f.number:02X} needs capacity for two records")
            if any(len(r) != f.record_size for r in f.records):
                raise DfcError(f"file {f.number:02X} has a record of the wrong length")
            if len(f.records) > f.record_count:
                raise DfcError(
                    f"file {f.number:02X} carries {len(f.records)} records but "
                    f"holds {f.record_count}"
                )
            if f.contents_complete and len(f.records) != f.record_count:
                raise DfcError(
                    f"file {f.number:02X} is marked complete but carries "
                    f"{len(f.records)} of {f.record_count} records"
                )
        else:
            raise DfcError(
                f"file type {f.type:02X} has no representation in version {DFC_VERSION}",
                DfcErrorClass.UNSUPPORTED,
            )


# ------------------------------------------------------------- DER reader --


def _read_tlv(blob: bytes, pos: int) -> tuple[int, bytes, int]:
    if pos + 2 > len(blob):
        raise DfcError("truncated encoding")
    tag = blob[pos]
    if tag & 0x1F == 0x1F:
        raise DfcError("high-tag-number identifiers are not part of this encoding")
    n = blob[pos + 1]
    hdr = 2
    if n & 0x80:
        count = n & 0x7F
        if count == 0 or count > 2:
            raise DfcError("indefinite or over-long length")
        if pos + 2 + count > len(blob):
            raise DfcError("truncated length")
        n = int.from_bytes(blob[pos + 2 : pos + 2 + count], "big")
        hdr = 2 + count
        if n < 128 or (count == 2 and n < 256):
            raise DfcError("non-minimal length")
    if pos + hdr + n > len(blob):
        raise DfcError("truncated value")
    return tag, blob[pos + hdr : pos + hdr + n], pos + hdr + n


def _split(body: bytes) -> list[tuple[int, bytes]]:
    out = []
    pos = 0
    while pos < len(body):
        tag, value, pos = _read_tlv(body, pos)
        out.append((tag, value))
    return out


def _fields(body: bytes, order: tuple[int, ...]) -> dict[int, bytes]:
    got: dict[int, bytes] = {}
    cursor = 0
    for tag, value in _split(body):
        if tag not in order:
            raise DfcError(f"tag 0x{tag:02X} is not a component of this type")
        if tag in got:
            raise DfcError(f"duplicate tag 0x{tag:02X}")
        idx = order.index(tag)
        if idx < cursor:
            raise DfcError(f"tag 0x{tag:02X} is out of declaration order")
        cursor = idx + 1
        got[tag] = value
    return got


def _req(got: dict[int, bytes], tag: int) -> bytes:
    if tag not in got:
        raise DfcError(f"required component 0x{tag:02X} is missing")
    return got[tag]


def _read_int(body: bytes) -> int:
    if not body or len(body) > 8:
        raise DfcError("integer of unusable length")
    if len(body) > 1 and (
        (body[0] == 0x00 and not body[1] & 0x80) or (body[0] == 0xFF and body[1] & 0x80)
    ):
        raise DfcError("non-minimal integer")
    return int.from_bytes(body, "big", signed=True)


def _read_uint(body: bytes, hi: int) -> int:
    v = _read_int(body)
    if not 0 <= v <= hi:
        raise DfcError(f"integer {v} out of range 0..{hi}")
    return v


def _read_bool(body: bytes) -> bool:
    if len(body) != 1 or body[0] not in (0x00, 0xFF):
        raise DfcError("boolean must be 00 or FF")
    return body[0] == 0xFF


def _read_default_false(got: dict[int, bytes], tag: int) -> bool:
    if tag not in got:
        return False
    if not _read_bool(got[tag]):
        raise DfcError(
            f"component 0x{tag:02X} defaults to false, so an explicit false is invalid"
        )
    return True


def _read_octets(body: bytes, want: int) -> bytes:
    if len(body) != want:
        raise DfcError(f"expected {want} octets, got {len(body)}")
    return body


def _read_iso_file_id(body: bytes) -> int:
    v = int.from_bytes(_read_octets(body, 2), "big")
    if v in RESERVED_ISO_FILE_IDS:
        raise DfcError(f"ISO file ID {v:04X} is reserved")
    return v


def _read_keys(body: bytes, key_len: int) -> list[DfcKey]:
    keys: list[DfcKey] = []
    for tag, value in _split(body):
        if tag != 0x30:
            raise DfcError(f"tag 0x{tag:02X} where a key was expected")
        got = _fields(value, (0x80, 0x81, 0x82))
        slot = _read_uint(_req(got, 0x80), 13)
        if slot != len(keys):
            raise DfcError(f"key slot {slot} out of ascending contiguous order")
        keys.append(
            DfcKey(_read_octets(_req(got, 0x81), key_len), _req(got, 0x82)[0])
        )
        if len(_req(got, 0x82)) != 1:
            raise DfcError("key version must be one octet")
    # An empty list is well formed: the slot then carries the factory default
    # (specification section 1.4, and rule 16 on a required empty SEQUENCE OF).
    return keys


def _read_file(body: bytes, owner: int) -> DfcFile:
    got = _fields(body, (0x80, 0x81, 0x82, 0x83, 0x84, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9))
    f = DfcFile(app_index=owner)
    f.number = _read_uint(_req(got, 0x80), 0x1F)
    f.type = _read_uint(_req(got, 0x81), 0xFF)
    if f.type == 0x05 or 0xA8 in got or 0xA9 in got:
        raise DfcError(
            "transaction-MAC and SDM files are represented by DFC v4 but are "
            "not emulated by this firmware",
            DfcErrorClass.UNSUPPORTED,
        )
    if f.type > FILE_TYPE_CYCLIC:
        raise DfcError(
            f"file type {f.type:02X} has no representation in version {DFC_VERSION}",
            DfcErrorClass.UNSUPPORTED,
        )
    f.comm_settings = _read_octets(_req(got, 0x82), 1)[0]
    f.access_rights = int.from_bytes(_read_octets(_req(got, 0x83), 2), "big")
    if 0x84 in got:
        f.has_iso_file_id = True
        f.iso_file_id = _read_iso_file_id(got[0x84])

    present = [t for t in (0xA5, 0xA6, 0xA7) if t in got]
    if len(present) != 1:
        raise DfcError("a file carries exactly one contents alternative")
    expected = {
        FILE_TYPE_STANDARD: 0xA5,
        FILE_TYPE_BACKUP: 0xA5,
        FILE_TYPE_VALUE: 0xA6,
        FILE_TYPE_LINEAR: 0xA7,
        FILE_TYPE_CYCLIC: 0xA7,
    }[f.type]
    if present[0] != expected:
        raise DfcError(
            f"file type {f.type:02X} requires contents 0x{expected:02X}, "
            f"not 0x{present[0]:02X}"
        )

    if expected == 0xA5:
        c = _fields(got[0xA5], (0x80, 0x81, 0x82))
        f.declared_size = _read_uint(_req(c, 0x80), U24_MAX)
        if f.declared_size == 0:
            raise DfcError("declared size must be at least one octet")
        f.contents_complete = _read_bool(_req(c, 0x82))
        if 0x81 in c:
            if not c[0x81] or len(c[0x81]) > f.declared_size:
                raise DfcError("known contents longer than the declared size")
            f.data = c[0x81]
        if f.contents_complete and len(f.data) != f.declared_size:
            raise DfcError("contents marked complete but shorter than the declared size")
    elif expected == 0xA6:
        c = _fields(got[0xA6], (0x80, 0x81, 0x82, 0x83))
        f.value_lower_limit = _read_int(_req(c, 0x80))
        f.value_upper_limit = _read_int(_req(c, 0x81))
        f.value = _read_int(_req(c, 0x82))
        for v in (f.value_lower_limit, f.value_upper_limit, f.value):
            if not -0x80000000 <= v <= 0x7FFFFFFF:
                raise DfcError(f"value {v} outside the signed 32-bit range")
        if not f.value_lower_limit <= f.value <= f.value_upper_limit:
            raise DfcError("value outside its own limits")
        f.limited_credit = _read_octets(_req(c, 0x83), 1)[0]
    else:
        c = _fields(got[0xA7], (0x80, 0x81, 0x82, 0x83, 0xA4))
        f.record_size = _read_uint(_req(c, 0x80), U24_MAX)
        f.max_records = _read_uint(_req(c, 0x81), U24_MAX)
        f.record_count = _read_uint(_req(c, 0x82), U24_MAX)
        if f.record_size == 0 or f.max_records == 0:
            raise DfcError("record size and capacity must be at least one")
        if f.record_count > f.max_records:
            raise DfcError("record count exceeds the file's capacity")
        if f.type == FILE_TYPE_CYCLIC and f.max_records < 2:
            raise DfcError("a cyclic file needs capacity for two records")
        f.contents_complete = _read_bool(_req(c, 0x83))
        for tag, value in _split(_req(c, 0xA4)):
            if tag != 0x04:
                raise DfcError(f"tag 0x{tag:02X} where a record was expected")
            if len(value) != f.record_size:
                raise DfcError("record of the wrong length")
            f.records.append(value)
        if len(f.records) > f.record_count:
            raise DfcError("more records carried than the file holds")
        if f.contents_complete and len(f.records) != f.record_count:
            raise DfcError("records marked complete but fewer carried than held")
    return f


def der_decode(blob: bytes, cls=DfcCredential) -> "DfcCredential":
    """Decode `.dfcb` octets into a credential. Raises DfcError on anything the
    encoding does not permit."""
    if len(blob) > DER_MAX_SIZE:
        raise DfcError(f"{len(blob)} bytes exceeds the {DER_MAX_SIZE}-byte limit")
    tag, body, end = _read_tlv(blob, 0)
    if tag != 0x60:
        raise DfcError(f"first identifier is 0x{tag:02X}, not 0x60")
    if end != len(blob):
        raise DfcError("trailing octets after the credential")

    got = _fields(body, (0x80, 0xA1, 0xA2, 0xA3))
    version = _read_uint(_req(got, 0x80), 0xFF)
    if version != DFC_VERSION:
        raise DfcError(
            f"credential encoding version {version} is not supported",
            DfcErrorClass.UNSUPPORTED,
        )

    cred = cls()

    card = _fields(_req(got, 0xA1), (0x80, 0x81, 0x82, 0x83, 0x84))
    if 0x84 in card:
        raise DfcError(
            "card static signatures are represented by DFC v4 but are not emulated",
            DfcErrorClass.UNSUPPORTED,
        )
    cred.generation = _read_uint(_req(card, 0x80), 0xFF)
    if cred.generation not in GENERATIONS.values():
        raise DfcError(f"unknown card generation {cred.generation}")
    if cred.generation != GENERATIONS["EV1"]:
        raise DfcError(
            f"{_name_of(GENERATIONS, cred.generation, 'this card generation')} "
            "is represented by DFC v4 but is not emulated by this firmware",
            DfcErrorClass.UNSUPPORTED,
        )
    cred.storage = _read_uint(_req(card, 0x81), U32_MAX)
    cred.uid = _req(card, 0x82)
    if len(cred.uid) not in UID_LENGTHS:
        raise DfcError(f"UID must be 4 or 7 bytes, got {len(cred.uid)}")
    cred.uid_provenance = _read_uint(_req(card, 0x83), 0xFF)
    if cred.uid_provenance not in PROVENANCES.values():
        raise DfcError(f"unknown UID provenance {cred.uid_provenance}")

    picc = _fields(
        _req(got, 0xA2),
        (0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0xA9, 0xAA,
         0x8B, 0xAC, 0xAD, 0xAE),
    )
    if any(tag in picc for tag in (0x8B, 0xAC, 0xAD, 0xAE)):
        raise DfcError(
            "this credential uses EV2/EV3 PICC capabilities that are represented "
            "by DFC v4 but are not emulated by this firmware",
            DfcErrorClass.UNSUPPORTED,
        )
    cred.picc_key_settings_1 = _read_octets(_req(picc, 0x80), 1)[0]
    cred.picc_key_settings_2 = _read_octets(_req(picc, 0x81), 1)[0]
    cred.picc_auth_command = AUTH_COMMANDS_BY_CODE[_read_uint(_req(picc, 0x82), 2)]
    cred.picc_random_id = _read_default_false(picc, 0x83)
    cred.picc_format_disabled = _read_default_false(picc, 0x84)
    if 0x85 in picc:
        if not picc[0x85]:
            raise DfcError("an emitted ATS must be at least one octet")
        cred.picc_ats = picc[0x85]
    if 0x86 in picc:
        cred.picc_sak = _read_octets(picc[0x86], 1)[0]
    if 0x87 in picc:
        cred.picc_atqa = _read_octets(picc[0x87], 2)
    if 0x88 in picc:
        cred.picc_sm_disable = _read_octets(picc[0x88], 1)[0]
    cred.picc_keys = _read_keys(_req(picc, 0xA9), cred.picc_key_len)

    for tag, value in _split(_req(got, 0xA3)):
        if tag != 0x30:
            raise DfcError(f"tag 0x{tag:02X} where an application was expected")
        app_fields = _fields(
            value, (0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0xA6, 0xA7, 0xA8, 0x89, 0xAA)
        )
        if any(tag in app_fields for tag in (0xA8, 0x89, 0xAA)):
            raise DfcError(
                "this application uses key sets, capability data, or delegation, "
                "which are represented by DFC v4 but not emulated by this firmware",
                DfcErrorClass.UNSUPPORTED,
            )
        app = DfcApplication()
        app.aid = _read_octets(_req(app_fields, 0x80), 3)
        if any(app.aid == other.aid for other in cred.apps):
            raise DfcError(f"duplicate AID {app.aid.hex().upper()}")
        if 0x81 in app_fields:
            app.has_iso_file_id = True
            app.iso_file_id = _read_iso_file_id(app_fields[0x81])
        if 0x82 in app_fields:
            if not 1 <= len(app_fields[0x82]) <= 16:
                raise DfcError("DF name must be 1 to 16 octets")
            app.iso_aid = app_fields[0x82]
        app.key_settings_1 = _read_octets(_req(app_fields, 0x83), 1)[0]
        app.key_settings_2 = _read_octets(_req(app_fields, 0x84), 1)[0]
        app.auth_command = AUTH_COMMANDS_BY_CODE[_read_uint(_req(app_fields, 0x85), 2)]
        app.key_len = key_length_for_ks2(app.key_settings_2)
        app.keys = _read_keys(_req(app_fields, 0xA6), app.key_len)

        owner = len(cred.apps)
        cred.apps.append(app)
        for ftag, fvalue in _split(_req(app_fields, 0xA7)):
            if ftag != 0x30:
                raise DfcError(f"tag 0x{ftag:02X} where a file was expected")
            cred.files.append(_read_file(fvalue, owner))

    for ftag, fvalue in _split(_req(picc, 0xAA)):
        if ftag != 0x30:
            raise DfcError(f"tag 0x{ftag:02X} where a file was expected")
        cred.files.append(_read_file(fvalue, OWNER_PICC))

    return cred


# ------------------------------------------------------------------ text --


def _scan_fields(text: str) -> dict[str, str]:
    """Collect the canonical `Key: Value` lines."""
    fields: dict[str, str] = {}
    for raw in text.splitlines():
        if raw.endswith((" ", "\t")):
            raise DfcError("a text line has trailing whitespace")
        line = raw
        if not line:
            continue
        if line.startswith("#"):
            continue
        sep = line.find(":")
        if sep < 0:
            raise DfcError(f"text line has no key separator: {line!r}")
        key = line[:sep]
        value = line[sep + 1 :]
        if key != key.rstrip() or not value.startswith(" "):
            raise DfcError(f"text line is not canonical: {line!r}")
        value = value[1:]
        if not key or not value:
            raise DfcError("a text key or value is empty")
        if key in fields:
            raise DfcError(f"duplicate text key {key!r}")
        fields[key] = value
    return fields


_STATIC_TEXT_KEYS = {
    "Filetype",
    "Version",
    "Card Generation",
    "Card Storage",
    "UID",
    "UID Provenance",
    "PICC Key Settings 1",
    "PICC Key Settings 2",
    "PICC Authentication Mode",
    "PICC Key Count",
    "PICC Random ID",
    "PICC Format Disabled",
    "PICC ATS",
    "PICC SAK",
    "PICC ATQA",
    "PICC SM Disable",
    "PICC File Count",
    "Application Count",
}

_UNSUPPORTED_STATIC_TEXT_KEYS = {
    "Card Static Signature",
    "PICC EV2 Card Capabilities",
    "PICC Proximity Key",
    "PICC Proximity Option",
    "PICC Proximity Published Response Time",
    "PICC Proximity Bitrate",
    "PICC Virtual Card Installation ID",
    "PICC Virtual Card Information",
    "PICC Virtual Card Capabilities",
    "PICC Virtual Card UID",
    "PICC Virtual Card Select MAC Key",
    "PICC Virtual Card Select Encryption Key",
    "PICC Virtual Card Authentication Mandatory",
    "PICC Virtual Card Proximity Mandatory",
    "PICC DAM Authentication Key",
    "PICC DAM MAC Key",
    "PICC DAM Encryption Key",
}

_UNSUPPORTED_APPLICATION_TEXT_PATTERN = re.compile(
    r"Application [0-9A-F]{2} (?:Key Set (?:Key Count|Maximum Key Size|Settings|Count)|"
    r"Key Set [0-9A-F]{2} (?:Version|Type|Initialized|Key Count|Key [0-9A-F]{2}(?: Version)?)|"
    r"Capability Data|Delegated (?:Slot Number|Slot Version|Quota Limit|Free Blocks))"
)

_UNSUPPORTED_FILE_TEXT_PATTERN = re.compile(
    r"(?:PICC |Application [0-9A-F]{2} )File [0-9A-F]{2} (?:"
    r"SDM (?:Options|Access Rights|UID Offset|Counter Offset|PICC Data Offset|"
    r"MAC Input Offset|MAC Offset|Encrypted File Offset|Encrypted File Length|"
    r"Counter Limit|Read Counter)|Transaction (?:Counter|MAC|Key Type|Key Version|Key)|"
    r"Previous Reader ID)"
)

_KEY_TEXT_PATTERN = re.compile(
    r"(?:PICC |Application [0-9A-F]{2} )Key [0-9A-F]{2}(?: Version)?"
)
_APPLICATION_TEXT_PATTERN = re.compile(
    r"Application [0-9A-F]{2} (?:AID|ISO File ID|DF Name|Key Settings 1|"
    r"Key Settings 2|Authentication Mode|Key Count|File Count)"
)
_FILE_TEXT_PATTERN = re.compile(
    r"(?:PICC |Application [0-9A-F]{2} )File [0-9A-F]{2} (?:Number|Type|"
    r"Communication Settings|Access Rights|ISO File ID|Size|Data|Data Complete|"
    r"Value Lower Limit|Value Upper Limit|Value|Limited Credit|Record Size|"
    r"Max Records|Record Count|Record Complete|Record [0-9A-F]{2})"
)


def _validate_text_keys(fields: dict[str, str]) -> None:
    for key in fields:
        if (key in _UNSUPPORTED_STATIC_TEXT_KEYS or
                _UNSUPPORTED_APPLICATION_TEXT_PATTERN.fullmatch(key) or
                _UNSUPPORTED_FILE_TEXT_PATTERN.fullmatch(key)):
            raise DfcError(
                f"{key!r} is represented by DFC v4 but is not emulated by this firmware",
                DfcErrorClass.UNSUPPORTED,
            )
        if key in _STATIC_TEXT_KEYS:
            continue
        if _KEY_TEXT_PATTERN.fullmatch(key):
            continue
        if _APPLICATION_TEXT_PATTERN.fullmatch(key):
            continue
        if _FILE_TEXT_PATTERN.fullmatch(key):
            continue
        raise DfcError(f"unrecognised text key {key!r}")

    for key, value in fields.items():
        if re.fullmatch(
            r"(?:PICC |Application [0-9A-F]{2} )File [0-9A-F]{2} Type", key
        ) and value == "Transaction MAC":
            raise DfcError(
                "transaction-MAC files are represented by DFC v4 but are not emulated",
                DfcErrorClass.UNSUPPORTED,
            )


def _name_of(table: dict[str, int], value: int, fallback: str) -> str:
    for name, v in table.items():
        if v == value:
            return name
    return fallback


def _parse_hex(value: str) -> bytes:
    value = value.strip()
    if not value:
        return b""
    if not re.fullmatch(r"[0-9A-F]{2}(?: [0-9A-F]{2})*", value):
        raise DfcError(f"bad canonical hex value {value!r}")
    try:
        return bytes.fromhex(value)
    except ValueError as exc:
        raise DfcError(f"bad hex value {value!r}") from exc


def _parse_int(value: str) -> int:
    value = value.strip()
    if not value:
        return 0
    try:
        return int(value, 0)
    except ValueError:
        # Several fields are spelled as a bare hex byte.
        try:
            return int(value, 16)
        except ValueError as exc:
            raise DfcError(f"bad integer value {value!r}") from exc


def _parse_octet(value: str, what: str) -> int:
    """A raw one-octet field. Spelled as a hex pair, never as a decimal number:
    `Key Settings 2: 81` is 0x81, and reading it as eighty-one would silently
    change the key type."""
    raw = _parse_hex(value)
    if len(raw) != 1:
        raise DfcError(f"{what} must be one hex octet, got {value!r}")
    return raw[0]


def _parse_decimal(value: str, what: str, signed: bool = False) -> int:
    """A decimal integer field. No hex, no leading zeros, no sign unless the
    field is one of the signed value-file limits."""
    text = value.strip()
    pattern = r"-?(0|[1-9][0-9]*)" if signed else r"(0|[1-9][0-9]*)"
    if not re.fullmatch(pattern, text):
        raise DfcError(f"{what} must be a decimal integer, got {value!r}")
    return int(text)


def _parse_bool(value: str, what: str) -> bool:
    value = value.strip()
    if value not in ("0", "1"):
        raise DfcError(f"{what} must be 0 or 1, got {value!r}")
    return value == "1"


def _auth_mode(text: str | None, ks2: int) -> int:
    if not text:
        return auth_command_for_ks2(ks2)
    if text not in AUTH_MODES:
        raise DfcError(f"unknown authentication mode {text!r}")
    return AUTH_MODES[text]


def _parse_uid(fields: dict[str, str]) -> bytes:
    uid = _parse_hex(fields.get("UID", ""))
    if len(uid) not in UID_LENGTHS:
        raise DfcError(f"UID must be 4 or 7 bytes, got {len(uid)}")
    if len(uid) == UID_LEN and uid[0] != UID_FIRST_BYTE:
        raise DfcError(f"UID must start with 0x{UID_FIRST_BYTE:02X}")
    return uid


def _parse_key_list(fields: dict[str, str], prefix: str, count: int, key_len: int) -> list[DfcKey]:
    keys = []
    for k in range(count):
        raw = fields.get(f"{prefix}Key {k:02X}", "")
        value = _parse_hex(raw)
        if value and len(value) != key_len:
            raise DfcError(
                f"{prefix}Key {k:02X} is {len(value)} bytes, expected {key_len}"
            )
        version = _parse_octet(
            fields.get(f"{prefix}Key {k:02X} Version", "00"), f"{prefix}Key {k:02X} Version")
        keys.append(DfcKey(value or bytes(key_len), version))
    return keys


# ---- version 4 ----


def _parse_v4(cls, fields: dict[str, str]) -> "DfcCredential":
    cred = cls()
    generation = fields.get("Card Generation", "")
    if generation not in GENERATIONS:
        raise DfcError(f"unknown Card Generation {generation!r}")
    cred.generation = GENERATIONS[generation]
    cred.storage = _parse_decimal(
        fields.get("Card Storage", str(DEFAULT_CARD_STORAGE)), "Card Storage")
    cred.uid = _parse_uid(fields)
    provenance = fields.get("UID Provenance", "")
    if provenance not in PROVENANCES:
        raise DfcError(f"unknown UID Provenance {provenance!r}")
    cred.uid_provenance = PROVENANCES[provenance]

    cred.picc_key_settings_1 = _parse_octet(
        _need(fields, "PICC Key Settings 1"), "PICC Key Settings 1")
    cred.picc_key_settings_2 = _parse_octet(
        _need(fields, "PICC Key Settings 2"), "PICC Key Settings 2")
    cred.picc_auth_command = _auth_mode(
        fields.get("PICC Authentication Mode"), cred.picc_key_settings_2
    )
    picc_key_count = _parse_decimal(_need(fields, "PICC Key Count"), "PICC Key Count")
    if picc_key_count > MAX_KEYS:
        raise DfcError(f"PICC Key Count exceeds {MAX_KEYS}")
    cred.picc_keys = _parse_key_list(fields, "PICC ", picc_key_count, cred.picc_key_len)
    if "PICC Random ID" in fields:
        cred.picc_random_id = _parse_bool(fields["PICC Random ID"], "PICC Random ID")
    if "PICC Format Disabled" in fields:
        cred.picc_format_disabled = _parse_bool(
            fields["PICC Format Disabled"], "PICC Format Disabled"
        )
    if "PICC ATS" in fields:
        cred.picc_ats = _parse_hex(fields["PICC ATS"])
        if not cred.picc_ats:
            raise DfcError("PICC ATS is present but empty")
    if "PICC SAK" in fields:
        cred.picc_sak = _parse_octet(fields["PICC SAK"], "PICC SAK")
    if "PICC ATQA" in fields:
        atqa = _parse_hex(fields["PICC ATQA"])
        if len(atqa) != 2:
            raise DfcError(f"PICC ATQA must be 2 bytes, got {len(atqa)}")
        cred.picc_atqa = atqa
    if "PICC SM Disable" in fields:
        cred.picc_sm_disable = _parse_octet(fields["PICC SM Disable"], "PICC SM Disable")

    picc_file_count = _parse_decimal(fields.get("PICC File Count", "0"), "PICC File Count")
    for i in range(picc_file_count):
        cred.files.append(_parse_file_v3(fields, f"PICC File {i:02X} ", OWNER_PICC))

    app_count = _parse_decimal(_need(fields, "Application Count"), "Application Count")
    app_files: list[DfcFile] = []
    for i in range(app_count):
        prefix = f"Application {i:02X} "
        app = DfcApplication()
        app.aid = _parse_hex(_need(fields, f"{prefix}AID"))
        if len(app.aid) != 3:
            raise DfcError(f"{prefix}AID must be 3 bytes, got {len(app.aid)}")
        if f"{prefix}ISO File ID" in fields:
            fid = _parse_hex(fields[f"{prefix}ISO File ID"])
            if len(fid) != 2:
                raise DfcError(f"{prefix}ISO File ID must be 2 bytes")
            app.has_iso_file_id = True
            app.iso_file_id = int.from_bytes(fid, "big")
        if f"{prefix}DF Name" in fields:
            app.iso_aid = _parse_hex(fields[f"{prefix}DF Name"])
            if not 1 <= len(app.iso_aid) <= 16:
                raise DfcError(f"{prefix}DF Name must be 1 to 16 bytes")
        app.key_settings_1 = _parse_octet(
            _need(fields, f"{prefix}Key Settings 1"), f"{prefix}Key Settings 1")
        app.key_settings_2 = _parse_octet(
            _need(fields, f"{prefix}Key Settings 2"), f"{prefix}Key Settings 2")
        app.auth_command = _auth_mode(
            fields.get(f"{prefix}Authentication Mode"), app.key_settings_2
        )
        app.key_len = key_length_for_ks2(app.key_settings_2)
        key_count = _parse_decimal(_need(fields, f"{prefix}Key Count"), f"{prefix}Key Count")
        if key_count > MAX_KEYS:
            raise DfcError(f"{prefix}Key Count exceeds {MAX_KEYS}")
        app.keys = _parse_key_list(fields, prefix, key_count, app.key_len)
        cred.apps.append(app)

        file_count = _parse_decimal(
            _need(fields, f"{prefix}File Count"), f"{prefix}File Count")
        for j in range(file_count):
            app_files.append(_parse_file_v3(fields, f"{prefix}File {j:02X} ", i))

    # Applications own their files; the PICC container is encoded after them, so
    # keep the model in that order too.
    cred.files = app_files + cred.files
    return cred


def _need(fields: dict[str, str], key: str) -> str:
    if key not in fields:
        raise DfcError(f"required key {key!r} is missing")
    return fields[key]


def _parse_file_v3(fields: dict[str, str], prefix: str, owner: int) -> DfcFile:
    f = DfcFile(app_index=owner)
    f.number = _parse_octet(_need(fields, f"{prefix}Number"), f"{prefix}Number")
    type_name = _need(fields, f"{prefix}Type")
    if type_name not in FILE_TYPES:
        raise DfcError(f"{prefix}Type {type_name!r} is not a known file type")
    f.type = FILE_TYPES[type_name]
    f.comm_settings = _parse_octet(
        _need(fields, f"{prefix}Communication Settings"),
        f"{prefix}Communication Settings")
    access = _parse_hex(_need(fields, f"{prefix}Access Rights"))
    if len(access) != 2:
        raise DfcError(f"{prefix}Access Rights must be 2 bytes")
    f.access_rights = int.from_bytes(access, "big")
    if f"{prefix}ISO File ID" in fields:
        fid = _parse_hex(fields[f"{prefix}ISO File ID"])
        if len(fid) != 2:
            raise DfcError(f"{prefix}ISO File ID must be 2 bytes")
        f.has_iso_file_id = True
        f.iso_file_id = int.from_bytes(fid, "big")

    if f.type in (FILE_TYPE_STANDARD, FILE_TYPE_BACKUP):
        f.declared_size = _parse_decimal(_need(fields, f"{prefix}Size"), f"{prefix}Size")
        f.data = _parse_hex(fields.get(f"{prefix}Data", ""))
        f.contents_complete = _parse_bool(
            _need(fields, f"{prefix}Data Complete"), f"{prefix}Data Complete"
        )
    elif f.type == FILE_TYPE_VALUE:
        f.value_lower_limit = _parse_decimal(
            _need(fields, f"{prefix}Value Lower Limit"), f"{prefix}Value Lower Limit", True)
        f.value_upper_limit = _parse_decimal(
            _need(fields, f"{prefix}Value Upper Limit"), f"{prefix}Value Upper Limit", True)
        f.value = _parse_decimal(_need(fields, f"{prefix}Value"), f"{prefix}Value", True)
        f.limited_credit = _parse_octet(
            _need(fields, f"{prefix}Limited Credit"), f"{prefix}Limited Credit")
    else:
        f.record_size = _parse_decimal(
            _need(fields, f"{prefix}Record Size"), f"{prefix}Record Size")
        f.max_records = _parse_decimal(
            _need(fields, f"{prefix}Max Records"), f"{prefix}Max Records")
        f.record_count = _parse_decimal(
            _need(fields, f"{prefix}Record Count"), f"{prefix}Record Count")
        f.contents_complete = _parse_bool(
            _need(fields, f"{prefix}Record Complete"), f"{prefix}Record Complete"
        )
        for r in range(f.record_count):
            key = f"{prefix}Record {r:02X}"
            if key not in fields:
                break
            f.records.append(_parse_hex(fields[key]))
    return f
