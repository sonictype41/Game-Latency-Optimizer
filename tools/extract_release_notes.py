#!/usr/bin/env python3
"""Extract one public release entry from CHANGELOG.md for GitHub Releases."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def normalize_version(tag_or_version: str) -> str:
    value = tag_or_version.strip()
    if value.startswith("v"):
        value = value[1:]
    if not value or any(ch.isspace() for ch in value):
        raise ValueError("release tag/version must be non-empty and contain no whitespace")
    return value


def extract_entry(changelog: str, tag_or_version: str) -> str:
    version = normalize_version(tag_or_version)
    header = re.compile(rf"^## \[{re.escape(version)}\](?:\s+-\s+.*)?\s*$")
    next_release = re.compile(r"^## \[[^]]+\](?:\s+-\s+.*)?\s*$")

    lines = changelog.splitlines()
    start = None
    for index, line in enumerate(lines):
        if header.match(line):
            start = index + 1
            break
    if start is None:
        raise ValueError(f"CHANGELOG.md has no release entry for {version}")

    body: list[str] = []
    for line in lines[start:]:
        if next_release.match(line):
            break
        body.append(line)

    notes = "\n".join(body).strip()
    if not notes:
        raise ValueError(f"CHANGELOG.md release entry for {version} is empty")
    return notes + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tag", help="release tag or version, e.g. v1.2.3 or 1.2.3-beta")
    parser.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    notes = extract_entry(args.changelog.read_text(encoding="utf-8"), args.tag)
    if args.output:
        args.output.write_text(notes, encoding="utf-8", newline="\n")
    else:
        print(notes, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
