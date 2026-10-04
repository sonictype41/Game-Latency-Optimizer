#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"; CACHE_ROOT="${GLO_BUILD_CACHE_DIR:-${TMPDIR:-/tmp}/glo-build-cache}"
case "${1:-}" in
  "") rm -rf "$ROOT/build" "$ROOT/bin"; echo 'Removed OSS-local build/ and bin/ artifacts. Build cache preserved.';;
  --cache) rm -rf "$CACHE_ROOT"; echo "Removed GLO build cache: $CACHE_ROOT";;
  --all) rm -rf "$ROOT/build" "$ROOT/bin" "$CACHE_ROOT"; echo 'Removed local outputs and GLO build cache.';;
  --cache-stats) if [[ -d "$CACHE_ROOT" ]]; then du -sh "$CACHE_ROOT"; find "$CACHE_ROOT" -maxdepth 2 -type d -print; else echo 'GLO build cache is empty.'; fi;;
  *) echo 'Usage: clean.sh [--cache|--all|--cache-stats]' >&2; exit 2;;
esac
