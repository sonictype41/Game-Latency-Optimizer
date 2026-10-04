#!/usr/bin/env python3
"""Content fingerprint for mutable inputs of the Windows GLO app CMake build.

This deliberately excludes immutable dependency caches (for example libsodium)
and compiler-object caches (ccache).  It exists to make the mutable CMake/Ninja
build tree safe when a release is extracted/copied over an existing source path
with timestamps that may not reveal changed file contents.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

SCHEMA = "glo-windows-app-source-v1"

# Files/directories that affect the GLO Windows executable or its generated
# build identity/resources.  Keep dependency archives out of this list: their
# immutable cache has its own checksum/toolchain key in build_app.sh.
TOP_LEVEL_FILES = (
    "CMakeLists.txt",
    "VERSION",
    "RELEASE",
)
SOURCE_TREES = (
    "cmake",
    "app",
    "protocol",
    "secure_transport",
)
EXTRA_FILES = (
    "third_party/wintun/INFO.json",
    "tools/build_app.sh",
    "tools/build_cache_fingerprint.py",
    "tools/stamp_build_info.py",
)


def _inputs(root: Path) -> list[Path]:
    root = root.resolve()
    files: set[Path] = set()
    for rel in TOP_LEVEL_FILES:
        path = root / rel
        if not path.is_file():
            raise FileNotFoundError(f"required build input missing: {path}")
        files.add(path)
    for rel in SOURCE_TREES:
        base = root / rel
        if not base.is_dir():
            raise FileNotFoundError(f"required build input directory missing: {base}")
        for path in base.rglob("*"):
            if path.is_file() and not path.is_symlink():
                files.add(path)
    for rel in EXTRA_FILES:
        path = root / rel
        if not path.is_file():
            raise FileNotFoundError(f"required build input missing: {path}")
        files.add(path)
    return sorted(files, key=lambda p: p.relative_to(root).as_posix())


def compute_fingerprint(root: Path) -> str:
    root = root.resolve()
    digest = hashlib.sha256()
    digest.update((SCHEMA + "\0").encode("utf-8"))
    for path in _inputs(root):
        rel = path.relative_to(root).as_posix().encode("utf-8")
        data = path.read_bytes()
        digest.update(len(rel).to_bytes(4, "big"))
        digest.update(rel)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    print(compute_fingerprint(args.root))


if __name__ == "__main__":
    main()
