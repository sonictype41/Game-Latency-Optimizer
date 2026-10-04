#!/usr/bin/env python3
"""Stage the generic GLO Windows client from canonical offline repository inputs."""
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

from verify_windows_resources import verify as verify_windows_resources

ROOT = Path(__file__).resolve().parents[1]


def machine(path: Path):
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"Not a PE file: {path}")
    off = struct.unpack_from("<I", data, 60)[0]
    if off + 6 > len(data) or data[off:off+4] != b"PE\0\0":
        raise ValueError(f"Invalid PE: {path}")
    return struct.unpack_from("<H", data, off + 4)[0]


def app_inputs(build_dir: Path):
    pairs = [
        (build_dir / "GLO.exe", build_dir / "GLO.build.json"),
        (build_dir / "glo-app-windows-amd64.exe", build_dir / "glo-app-windows-amd64.build.json"),
    ]
    for exe, manifest in pairs:
        if exe.is_file() and manifest.is_file():
            return exe, manifest
    raise FileNotFoundError(f"Could not find canonical GLO.exe/build manifest in {build_dir}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build_dir", type=Path, help="CMake output directory or bin/app directory")
    ap.add_argument("output_dir", type=Path, help="empty staging/output directory")
    args = ap.parse_args()
    build_dir = args.build_dir.resolve()
    output_dir = args.output_dir.resolve()
    exe, manifest = app_inputs(build_dir)
    meta = json.loads(manifest.read_text(encoding="utf-8"))
    version = (ROOT / "VERSION").read_text(encoding='utf-8').strip()
    release = (ROOT / "RELEASE").read_text(encoding='utf-8').strip()
    if meta.get("version") != version or meta.get("release") != release or meta.get("client") != "generic-config" or meta.get("build_id") != "generic-config-client":
        raise ValueError("Generic client build identity mismatch")
    actual_exe_sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    if meta.get("exe_sha256") != actual_exe_sha:
        raise ValueError("Executable SHA-256 does not match build manifest")
    if machine(exe) != 0x8664:
        raise ValueError(f"Expected amd64 PE: {exe}")
    wintun, _, _ = verify_windows_resources()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise ValueError("Output directory must be empty")
    output_dir.mkdir(parents=True, exist_ok=True)
    copies = [
        (exe, "GLO.exe"),
        (manifest, "BUILD_INFO.json"),
        (wintun, "wintun.dll"),
        (ROOT / "third_party/wintun/INFO.json", "WINTUN_INFO.json"),
        (ROOT / "LICENSE", "LICENSE.txt"),
        (ROOT / "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        (ROOT / "third_party/wintun/LICENSE.txt", "WINTUN_LICENSE.txt"),
        (ROOT / "third_party/libsodium/LICENSE", "LIBSODIUM_LICENSE.txt"),
    ]
    for source, name in copies:
        shutil.copyfile(source, output_dir / name)
    sums = []
    for path in sorted(output_dir.iterdir()):
        if path.is_file():
            sums.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")
    (output_dir / "SHA256SUMS.txt").write_text("".join(sums), encoding="utf-8", newline="\n")
    print(f"Packaged generic config client {release} (protocol {version}) -> {output_dir}")


if __name__ == "__main__":
    main()
