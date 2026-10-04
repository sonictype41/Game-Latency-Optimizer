#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BUILD="$ROOT/build/relay"
DEFAULT_BIN="$ROOT/bin"
OUT_ROOT="$DEFAULT_BIN"
ARCHES=(amd64 arm64)
while (($#)); do
  case "$1" in
    --output-root) [[ $# -ge 2 ]] || { echo 'Missing value for --output-root' >&2; exit 2; }; OUT_ROOT="$2"; shift 2;;
    amd64|arm64) ARCHES=("$1"); shift;;
    all) ARCHES=(amd64 arm64); shift;;
    *) echo "Unknown argument: $1" >&2; exit 2;;
  esac
done
command -v go >/dev/null || { echo 'Missing build dependency: go' >&2; exit 1; }
[[ -f "$ROOT/vendor/modules.txt" ]] || { echo 'Missing vendor/modules.txt; offline relay build cannot continue.' >&2; exit 1; }
mkdir -p "$BUILD" "$OUT_ROOT/server"
cd "$ROOT"
export GOPROXY=off GOSUMDB=off
GOFLAGS="${GOFLAGS:-} -mod=vendor" go test ./...
GOFLAGS="${GOFLAGS:-} -mod=vendor" go vet ./...
for arch in "${ARCHES[@]}"; do
  dst="$OUT_ROOT/server/glo-relay-linux-$arch"
  CGO_ENABLED=0 GOOS=linux GOARCH="$arch" GOFLAGS="${GOFLAGS:-} -mod=vendor" \
    go build -buildvcs=false -trimpath -ldflags='-s -w' -o "$dst" ./relay/dataplane
  cp "$dst" "$BUILD/glo-relay-linux-$arch"
  echo "Built $dst"
done
cp relay/dataplane/relay-policy.json "$BUILD/relay-policy.json"
