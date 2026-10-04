#!/usr/bin/env python3
"""Verify vendored Windows runtime resources. Offline only."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INFO = ROOT / "third_party" / "wintun" / "INFO.json"


def pe_info(path: Path):
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError(f"not a PE file: {path}")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
        raise ValueError(f"invalid PE header: {path}")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B):
        raise ValueError(f"unknown PE optional header: {path}")
    data_dirs = opt + (112 if magic == 0x20B else 96)
    cert_offset, cert_size = struct.unpack_from("<II", data, data_dirs + 8 * 4)
    return data, machine, cert_offset, cert_size


def verify(info_path: Path = INFO):
    meta = json.loads(info_path.read_text(encoding="utf-8"))
    if meta.get("schema") != 1 or meta.get("name") != "Wintun":
        raise ValueError("unsupported Wintun INFO.json schema")
    rel = meta["file"]["path"]
    dll = info_path.parent / rel
    if not dll.is_file():
        raise FileNotFoundError(f"missing canonical Wintun DLL: {dll}")
    data, machine, cert_offset, cert_size = pe_info(dll)
    actual = hashlib.sha256(data).hexdigest()
    expected = str(meta["file"]["sha256"]).lower()
    if actual != expected:
        raise ValueError(f"Wintun SHA-256 mismatch: expected {expected}, got {actual}")
    if len(data) != int(meta["file"]["size"]):
        raise ValueError(f"Wintun size mismatch: expected {meta['file']['size']}, got {len(data)}")
    if machine != 0x8664:
        raise ValueError(f"Wintun architecture mismatch: expected AMD64 (0x8664), got 0x{machine:04x}")
    if meta["file"].get("authenticode_security_directory") and (cert_offset == 0 or cert_size == 0):
        raise ValueError("Wintun PE has no Authenticode security directory")
    if not (info_path.parent / meta["license"]).is_file():
        raise ValueError("Wintun license is missing")
    return dll, meta, actual


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--info", type=Path, default=INFO)
    args = ap.parse_args()
    dll, meta, digest = verify(args.info.resolve())
    print(f"Wintun {meta['version']} {meta['architecture']} verified")
    print(f"dll={dll}")
    print(f"sha256={digest}")
    print(f"archive_sha256={meta['upstream']['archive_sha256']}")


if __name__ == "__main__":
    main()
