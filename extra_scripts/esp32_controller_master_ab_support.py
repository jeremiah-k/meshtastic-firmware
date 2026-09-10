#!/usr/bin/env python3
"""Pinned original-ESP32 controller archive support for the 2026-09-07 master A/B."""

import hashlib
import os
import tempfile
from pathlib import Path

import requests

CONTROLLER_COMMIT = "e4281c149690558467f75137d64bd5334076e7e1"
CONTROLLER_BLOB_SHA1 = "19fd2a3cd0fd54a2dcf02fb5f2f85561078f38bd"
CONTROLLER_SIZE = 900296
CONTROLLER_URL = (
    "https://raw.githubusercontent.com/espressif/esp32-bt-lib/"
    f"{CONTROLLER_COMMIT}/esp32/libbtdm_app.a"
)


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    # Git blob identity is defined over SHA-1; this is a content-addressing
    # equality check against an upstream revision, not a security primitive.
    digest = hashlib.sha1(header + data, usedforsecurity=False).hexdigest()  # nosemgrep: python.lang.security.insecure-hash-algorithms.insecure-hash-algorithm-sha1  # fmt: skip
    return digest


def verify_controller_bytes(data: bytes) -> None:
    if len(data) != CONTROLLER_SIZE:
        raise ValueError(
            f"controller archive size mismatch: got {len(data)}, expected {CONTROLLER_SIZE}"
        )
    actual = git_blob_sha1(data)
    if actual != CONTROLLER_BLOB_SHA1:
        raise ValueError(
            f"controller archive Git blob mismatch: got {actual}, expected {CONTROLLER_BLOB_SHA1}"
        )


def ensure_controller_archive(cache_root: Path, timeout: int = 60) -> Path:
    cache_dir = Path(cache_root) / CONTROLLER_COMMIT
    archive = cache_dir / "libbtdm_app.a"

    if archive.is_file():
        try:
            verify_controller_bytes(archive.read_bytes())
            return archive
        except ValueError:
            archive.unlink()

    cache_dir.mkdir(parents=True, exist_ok=True)
    response = requests.get(
        CONTROLLER_URL,
        timeout=timeout,
        headers={"User-Agent": "meshtastic-firmware-esp32-controller-ab"},
    )
    response.raise_for_status()
    data = response.content
    verify_controller_bytes(data)

    fd, temporary_name = tempfile.mkstemp(
        prefix="libbtdm_app.", suffix=".tmp", dir=cache_dir
    )
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, archive)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return archive


def is_controller_library_entry(entry) -> bool:
    value = str(entry).replace("\\", "/")
    name = value.rsplit("/", 1)[-1]
    if name.startswith("-l"):
        name = name[2:]
    if name.startswith("lib"):
        name = name[3:]
    if name.endswith(".a"):
        name = name[:-2]
    return name == "btdm_app"


def controller_archive_member_paths(map_text: str):
    """Return normalized map lines that pulled members from libbtdm_app.a."""
    paths = []
    for raw_line in map_text.splitlines():
        line = raw_line.replace("\\", "/")
        marker = "libbtdm_app.a("
        if marker in line:
            paths.append(line.strip())
    return paths
