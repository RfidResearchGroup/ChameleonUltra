"""Encode and publish complete read-only Classic 1K backdoor backups."""

import hashlib
import json
import os
import tempfile
from pathlib import Path
from typing import Optional

from magic_classic import MagicCardDump, MagicCardReadError


def encode_backup(result: MagicCardDump, output_format: str) -> bytes:
    """Represent the same 1024-byte image in BIN or metadata-bearing JSON."""
    if len(result.image) != 1024:
        raise MagicCardReadError("Classic 1K backup is incomplete")
    if output_format == "bin":
        return result.image
    if output_format != "json":
        raise ValueError("backup format must be bin or json")
    identity = result.identity
    document = {
        "format": "chameleon-gen1a-classic1k-v1",
        "read_profile": "gen1a-compatible-classic1k",
        "uid": identity.uid.hex().upper(),
        "atqa_wire_order": identity.atqa.hex().upper(),
        "sak": identity.sak.hex().upper(),
        "block_size": 16,
        "block_count": 64,
        "independent_reads": result.independent_reads,
        "key_material_verified": False,
        "key_handling": "raw bytes returned by card; no key reconstruction or authentication",
        "sha256": hashlib.sha256(result.image).hexdigest(),
        "blocks": [
            result.image[offset : offset + 16].hex().upper()
            for offset in range(0, 1024, 16)
        ],
    }
    return (json.dumps(document, indent=2) + "\n").encode("utf-8")


def publish_backup(target: Path, content: bytes) -> None:
    """Publish fully written bytes while refusing to replace another backup."""
    temporary: Optional[Path] = None
    try:
        # Same-directory staging keeps publication on one filesystem. A failed
        # write, flush or close must never expose a partial destination file.
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=target.parent,
            prefix=f".{target.name}.",
            suffix=".tmp",
            delete=False,
        ) as backup:
            temporary = Path(backup.name)
            if backup.write(content) != len(content):
                raise OSError("incomplete Classic 1K backup write")
            backup.flush()
            os.fsync(backup.fileno())
        if os.name == "nt":
            # Windows rename fails if the destination appeared during RF reads.
            os.rename(temporary, target)
            temporary = None
        else:
            # POSIX rename replaces files; an atomic hard link refuses EEXIST.
            os.link(temporary, target)
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
