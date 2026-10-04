#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
"$ROOT/tools/build_relay.sh" all "$@"
"$ROOT/tools/build_app.sh" "$@"
