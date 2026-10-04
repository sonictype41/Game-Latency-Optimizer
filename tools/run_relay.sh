#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"
export GOPROXY=off GOSUMDB=off
exec go run -mod=vendor ./relay/dataplane "$@"
