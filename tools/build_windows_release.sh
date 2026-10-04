#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
if command -v python3 >/dev/null 2>&1; then PY=python3; else PY=python; fi
START="$($PY - <<'PY'
import time;print(time.time())
PY
)"
"$ROOT/tools/build_app.sh"
"$ROOT/tools/build_installer.sh" "$@"
END="$($PY - <<'PY'
import time;print(time.time())
PY
)"
TOTAL="$($PY - <<PY
print(f'{float("$END")-float("$START"):.2f}')
PY
)"
echo; echo 'GLO Windows release summary'; echo '────────────────────────────────'; echo "Total elapsed: ${TOTAL}s"; echo "Cache root: ${GLO_BUILD_CACHE_DIR:-${TMPDIR:-/tmp}/glo-build-cache}"; echo '────────────────────────────────'
