#!/usr/bin/env python3
"""Fetch the pinned Wintun runtime declared by third_party/wintun/INFO.json."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INFO = ROOT / "third_party" / "wintun" / "INFO.json"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(info_path: Path = INFO, force: bool = False) -> Path:
    info_path = info_path.resolve()
    meta = json.loads(info_path.read_text(encoding="utf-8"))
    if meta.get("schema") != 1 or meta.get("name") != "Wintun":
        raise ValueError("unsupported Wintun INFO.json schema")

    target = info_path.parent / meta["file"]["path"]
    expected_dll = str(meta["file"]["sha256"]).lower()
    expected_size = int(meta["file"]["size"])

    if target.is_file() and not force:
        data = target.read_bytes()
        if len(data) == expected_size and sha256(data) == expected_dll:
            print(f"Wintun {meta['version']} already present and verified: {target}")
            return target

    req = urllib.request.Request(
        str(meta["upstream"]["url"]),
        headers={"User-Agent": "GLO-build/0.0.2-beta"},
    )
    with urllib.request.urlopen(req, timeout=60) as response:
        archive = response.read()

    expected_archive = str(meta["upstream"]["archive_sha256"]).lower()
    actual_archive = sha256(archive)
    if actual_archive != expected_archive:
        raise ValueError(
            f"Wintun archive SHA-256 mismatch: expected {expected_archive}, got {actual_archive}"
        )

    with tempfile.TemporaryDirectory(prefix="glo-wintun-") as td:
        archive_path = Path(td) / str(meta["upstream"]["archive"])
        archive_path.write_bytes(archive)
        with zipfile.ZipFile(archive_path) as zf:
            member = str(meta["upstream"]["member"])
            try:
                dll = zf.read(member)
            except KeyError as exc:
                raise ValueError(f"Wintun archive is missing {member}") from exc

    actual_dll = sha256(dll)
    if len(dll) != expected_size:
        raise ValueError(
            f"Wintun DLL size mismatch: expected {expected_size}, got {len(dll)}"
        )
    if actual_dll != expected_dll:
        raise ValueError(
            f"Wintun DLL SHA-256 mismatch: expected {expected_dll}, got {actual_dll}"
        )

    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(target.name + f".tmp.{os.getpid()}")
    tmp.write_bytes(dll)
    os.replace(tmp, target)
    print(f"Fetched and verified Wintun {meta['version']} {meta['architecture']}: {target}")
    print(f"sha256={actual_dll}")
    return target


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--info", type=Path, default=INFO)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    fetch(args.info, args.force)


if __name__ == "__main__":
    main()
