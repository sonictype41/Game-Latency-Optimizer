#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
if command -v python3 >/dev/null 2>&1; then PYTHON=python3; elif command -v python >/dev/null 2>&1; then PYTHON=python; else echo "Missing build dependency: python" >&2; exit 1; fi
exec "$PYTHON" "$ROOT/tools/build_installer.py" "$@"
