#!/usr/bin/env python3
"""Reject incomplete or changed emulator inputs before using the build environment."""
from hashlib import sha256
import json
from pathlib import Path
import sys


def verify(directory: Path, patch: Path) -> None:
    expected = {
        "qemu_version": "8.2.2",
        "source_sha256": "5e692dd20950a819ff6550cd3974e00319a93f47c0a15f808c58df1a9e4d2478",
        "patch_sha256": "a2838c7f61ec52a29a59e5622a2d69a488a7f5a58d761d0276188702a42ad4e9",
        "upstream_comparison_package_sha256": "a8f4e6121b7d1710885ba00fdb1e5a8256fb396f12ade2da9ed8a3693ad6d1bd",
    }
    manifest = json.loads((directory / "manifest.json").read_text())
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"Unexpected emulator {key}: {manifest.get(key)!r}")
    if sha256(patch.read_bytes()).hexdigest() != expected["patch_sha256"]:
        raise ValueError("Tracked emulator patch checksum differs from the pinned patch")
    actual = sha256((directory / "bin/qemu-aarch64").read_bytes()).hexdigest()
    if manifest.get("binary_sha256") != actual:
        raise ValueError("Prepared emulator binary does not match its manifest")
    if (directory / "SHA256SUMS").read_text() != actual + "  bin/qemu-aarch64\n":
        raise ValueError("Prepared emulator checksum file does not match its binary")
    print(f"Verified QEMU {manifest['qemu_version']} binary SHA256 {actual}")


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, KeyError, IndexError) as error:
        raise SystemExit(f"Emulator preparation is missing or invalid: {error}. "
                         "Run dockerfiles/scripts/prepare-patched-qemu.sh explicitly before rebuilding.")
