"""Bounded, checksum-verified LF waveform files and offline exports.

The checksum detects accidental corruption; it does not authenticate a source.
Legacy ADC responses have no continuity guarantee and retain that distinction.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import stat
import struct
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional, Protocol, Sequence

from chameleon_com import Response
from chameleon_enum import Command, Status

CAPTURE_COMMAND = 3042  # Provisional allocation shared with the firmware proposal.
SCHEMA = "chameleon-lf-waveform-v1"
MAX_FILE = 65536
MAX_SAMPLES = 4000


class WaveformError(ValueError):
    pass


class Transport(Protocol):
    @property
    def commands(self) -> Sequence[int]: ...

    def send_cmd_sync(
        self, cmd: int, data: Optional[bytes] = None, status: int = 0, timeout: int = 3
    ) -> Response: ...


def _integer(value: Any, low: int, high: int, label: str) -> int:
    if type(value) is not int or not low <= value <= high:
        raise WaveformError(f"invalid {label}")
    return value


def _canonical(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode()


def _unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise WaveformError("duplicate JSON member")
        result[key] = value
    return result


def _seal(value: dict[str, Any]) -> dict[str, Any]:
    return dict(value, document_sha256=hashlib.sha256(_canonical(value)).hexdigest())


def make_waveform(samples: bytes, *, command: int, contiguous: bool) -> dict[str, Any]:
    if type(samples) is not bytes or not 1 <= len(samples) <= MAX_SAMPLES:
        raise WaveformError("capture must contain 1 through 4000 byte samples")
    if command not in (int(Command.ADC_GENERIC_READ), CAPTURE_COMMAND):
        raise WaveformError("unsupported acquisition command")
    if contiguous != (command == CAPTURE_COMMAND):
        raise WaveformError("continuity must match the acquisition protocol")
    if command == Command.ADC_GENERIC_READ and len(samples) != 800:
        raise WaveformError("legacy acquisition must contain exactly 800 samples")
    return _seal(
        {
            "schema": SCHEMA,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "command": command,
            "sample_format": "u8-shift5-clamped",
            "nominal_sample_rate_hz": 125000,
            "sample_count": len(samples),
            "continuity": "dma-complete" if contiguous else "legacy-unverified",
            "samples_hex": samples.hex(),
            "samples_sha256": hashlib.sha256(samples).hexdigest(),
        }
    )


def validate_waveform(value: Any) -> dict[str, Any]:
    expected = {
        "schema",
        "created_utc",
        "command",
        "sample_format",
        "nominal_sample_rate_hz",
        "sample_count",
        "continuity",
        "samples_hex",
        "samples_sha256",
        "document_sha256",
    }
    if type(value) is not dict or set(value) != expected or value["schema"] != SCHEMA:
        raise WaveformError("unsupported waveform schema or fields")
    if value["sample_format"] != "u8-shift5-clamped":
        raise WaveformError("unsupported sample format")
    _integer(value["nominal_sample_rate_hz"], 125000, 125000, "nominal rate")
    count = _integer(value["sample_count"], 1, MAX_SAMPLES, "sample count")
    command = _integer(value["command"], 0, 65535, "command")
    if command not in (int(Command.ADC_GENERIC_READ), CAPTURE_COMMAND):
        raise WaveformError("unsupported acquisition command")
    required = "dma-complete" if command == CAPTURE_COMMAND else "legacy-unverified"
    if value["continuity"] != required or (
        command == Command.ADC_GENERIC_READ and count != 800
    ):
        raise WaveformError("invalid continuity provenance")
    encoded = value["samples_hex"]
    if type(encoded) is not str or len(encoded) != count * 2:
        raise WaveformError("sample count does not match encoded bytes")
    try:
        samples = bytes.fromhex(encoded)
        timestamp = datetime.fromisoformat(value["created_utc"])
    except (TypeError, ValueError) as error:
        raise WaveformError("invalid samples or timestamp") from error
    if samples.hex() != encoded or timestamp.utcoffset() is None:
        raise WaveformError("noncanonical bytes or missing timestamp zone")
    if hashlib.sha256(samples).hexdigest() != value["samples_sha256"]:
        raise WaveformError("sample checksum mismatch")
    payload = {k: v for k, v in value.items() if k != "document_sha256"}
    if hashlib.sha256(_canonical(payload)).hexdigest() != value["document_sha256"]:
        raise WaveformError("document checksum mismatch")
    return value


def check_output(path: Path) -> None:
    if path.exists() or path.is_symlink():
        raise WaveformError("output already exists")
    if not path.parent.is_dir():
        raise WaveformError("output parent directory must already exist")


def save_bytes(path: Path, content: bytes) -> None:
    check_output(path)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            output = os.fdopen(descriptor, "wb")
        except BaseException:
            os.close(descriptor)
            raise
        with output:
            if output.write(content) != len(content):
                raise OSError("short write")
            output.flush()
            os.fsync(output.fileno())
        # Report success only after the complete file is closed and reread.
        with path.open("rb") as saved:
            if saved.read(len(content) + 1) != content:
                raise OSError("saved bytes differ")
    except OSError as error:
        raise WaveformError(f"output publication failed: {error}") from error


def save_waveform(path: Path, value: dict[str, Any]) -> None:
    validate_waveform(value)
    save_bytes(path, _canonical(value) + b"\n")


def load_waveform(path: Path) -> dict[str, Any]:
    try:
        if not stat.S_ISREG(path.lstat().st_mode):
            raise WaveformError("input must be a regular file")
        with path.open("rb") as source:
            data = source.read(MAX_FILE + 1)
        if len(data) > MAX_FILE:
            raise WaveformError("waveform file is too large")
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_unique)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise WaveformError(f"cannot read waveform: {error}") from error
    return validate_waveform(value)


def _call(
    device: Transport,
    command: int,
    data: Optional[bytes] = None,
    *,
    status: Status = Status.SUCCESS,
    length: Optional[int] = 0,
) -> bytes:
    response = device.send_cmd_sync(command, data)
    if (
        response.status != status
        or type(response.data) is not bytes
        or (length is not None and len(response.data) != length)
    ):
        raise WaveformError(f"command {int(command)} returned invalid status/data")
    return response.data


def capture_waveform(device: Transport, count: int = 800) -> dict[str, Any]:
    _integer(count, 1, MAX_SAMPLES, "requested sample count")
    required = {Command.GET_DEVICE_MODE, Command.CHANGE_DEVICE_MODE}
    if not required.issubset(device.commands):
        raise WaveformError("firmware does not expose mode control")
    versioned = CAPTURE_COMMAND in device.commands
    if not versioned and (
        Command.ADC_GENERIC_READ not in device.commands or count != 800
    ):
        raise WaveformError("legacy firmware supports only an 800-sample capture")
    original = _call(device, Command.GET_DEVICE_MODE, length=1)[0]
    if original not in (0, 1):
        raise WaveformError("invalid device mode")
    primary: Optional[BaseException] = None
    try:
        if original == 0:
            _call(device, Command.CHANGE_DEVICE_MODE, b"\x01")
        if versioned:
            data = _call(
                device,
                CAPTURE_COMMAND,
                struct.pack("!H", count),
                status=Status.LF_TAG_OK,
                length=12 + count,
            )
            version, bits, actual, rate, flags, requested = struct.unpack(
                "!BBHIHH", data[:12]
            )
            if (version, bits, actual, rate, flags, requested) != (
                1,
                8,
                count,
                125000,
                3,
                count,
            ):
                raise WaveformError("unsupported or incomplete capture header")
            result = make_waveform(data[12:], command=CAPTURE_COMMAND, contiguous=True)
        else:
            data = _call(
                device, Command.ADC_GENERIC_READ, status=Status.LF_TAG_OK, length=800
            )
            result = make_waveform(
                data, command=int(Command.ADC_GENERIC_READ), contiguous=False
            )
    except BaseException as error:
        primary = error
        raise
    finally:
        if original == 0:
            try:
                _call(device, Command.CHANGE_DEVICE_MODE, b"\x00")
                if _call(device, Command.GET_DEVICE_MODE, length=1) != b"\x00":
                    raise WaveformError("original mode readback failed")
            except Exception as error:
                if primary is None:
                    raise WaveformError(
                        "failed to restore original device mode"
                    ) from error
                # Preserve cancellation and the primary error on Python 3.9 too.
                setattr(primary, "restore_error", str(error))
    return result


def summarize(value: dict[str, Any]) -> dict[str, Any]:
    validate_waveform(value)
    samples = bytes.fromhex(value["samples_hex"])
    mean = math.fsum(samples) / len(samples)
    threshold = (min(samples) + max(samples)) / 2
    transitions = sum(
        (a > threshold) != (b > threshold) for a, b in zip(samples, samples[1:])
    )
    return {
        "samples": len(samples),
        "minimum": min(samples),
        "maximum": max(samples),
        "mean": mean,
        "ac_rms": math.sqrt(math.fsum((x - mean) ** 2 for x in samples) / len(samples)),
        "midpoint_transitions": transitions,
        "continuity": value["continuity"],
        "nominal_duration_ms": len(samples) * 1000 / value["nominal_sample_rate_hz"],
        "samples_sha256": value["samples_sha256"],
    }


def export_waveform(value: dict[str, Any], path: Path, format_name: str) -> None:
    validate_waveform(value)
    samples = bytes.fromhex(value["samples_hex"])
    rate = value["nominal_sample_rate_hz"]
    if format_name == "bin":
        data = samples
    elif format_name == "csv":
        # Times are nominal sample coordinates, not measured wall-clock timing.
        data = (
            "sample,nominal_time_us,adc_u8\n"
            + "".join(
                f"{i},{i * 1_000_000 / rate:.6f},{sample}\n"
                for i, sample in enumerate(samples)
            )
        ).encode()
    elif format_name == "svg":
        points = " ".join(
            f"{40 + 920 * i / max(1, len(samples) - 1):.2f},{270 - sample * 240 / 255:.2f}"
            for i, sample in enumerate(samples)
        )
        label = f"{len(samples)} samples; nominal {rate} Hz; {value['continuity']}"
        data = (
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 320">'
            '<rect width="1000" height="320" fill="white"/>'
            '<path d="M40 30V270H960" fill="none" stroke="#444"/>'
            f'<polyline points="{points}" fill="none" stroke="#17678b" stroke-width="1"/>'
            f'<text x="40" y="305" font-family="sans-serif" font-size="14">{label}</text></svg>\n'
        ).encode()
    else:
        raise WaveformError("export format must be bin, csv or svg")
    save_bytes(path, data)
