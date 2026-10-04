#!/usr/bin/env bash
set -euo pipefail
umask 077
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
DEFAULT_BIN="$ROOT/bin"
OUT_ROOT="$DEFAULT_BIN"
while (($#)); do case "$1" in --output-root) [[ $# -ge 2 ]] || { echo 'Missing value for --output-root' >&2; exit 2; }; OUT_ROOT="$2"; shift 2;; *) echo "Unknown argument: $1" >&2; exit 2;; esac; done
for tool in cmake tar make sha256sum; do command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }; done
if command -v python3 >/dev/null 2>&1; then PYTHON=python3; elif command -v python >/dev/null 2>&1; then PYTHON=python; else echo "Missing build dependency: python" >&2; exit 1; fi
WINTUN_DLL="$ROOT/third_party/wintun/bin/amd64/wintun.dll"
if [[ ! -f "$WINTUN_DLL" ]]; then
  case "${GLO_OFFLINE:-0}" in
    1|on|true|yes)
      echo "Missing pinned Wintun runtime in offline mode: $WINTUN_DLL" >&2
      echo "Fetch it first with: $PYTHON ./tools/fetch_wintun.py" >&2
      exit 1
      ;;
  esac
  echo "Pinned Wintun runtime is missing; fetching the verified upstream artifact..."
  "$PYTHON" "$ROOT/tools/fetch_wintun.py"
fi
"$PYTHON" "$ROOT/tools/verify_windows_resources.py" >/dev/null
# VERSION and RELEASE are the public OSS release mirrors. Release packaging
# updates them before publication; standalone checkouts never inspect service files.
START_TS="$($PYTHON - <<'PY'
import time;print(time.time())
PY
)"
CACHE_ROOT="${GLO_BUILD_CACHE_DIR:-${TMPDIR:-/tmp}/glo-build-cache}"
mkdir -p "$CACHE_ROOT" "$CACHE_ROOT/deps/libsodium" "$CACHE_ROOT/cmake" "$CACHE_ROOT/ccache"; chmod 700 "$CACHE_ROOT" 2>/dev/null || true
ARCHIVE="$ROOT/third_party/libsodium/libsodium-1.0.20.tar.gz"; EXPECTED=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
[[ -f "$ARCHIVE" ]] || { echo "Missing local dependency: $ARCHIVE" >&2; exit 1; }; ACTUAL="$(sha256sum "$ARCHIVE")"; [[ "${ACTUAL%% *}" == "$EXPECTED" ]] || { echo 'libsodium source checksum mismatch' >&2; exit 1; }
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  for tool in ninja g++; do command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }; done
  case "$(g++ -dumpmachine)" in x86_64-w64-mingw32) ;; *) echo 'Expected Windows x64 MinGW compiler.' >&2; exit 1;; esac
  CC=gcc; CXX=g++; TOOLCHAIN_ID="$(g++ -dumpmachine)|$(g++ -dumpfullversion -dumpversion 2>/dev/null || g++ -dumpversion)|native-msys"; CONFIG_ARGS='--disable-shared --enable-static'
else
  for tool in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ x86_64-w64-mingw32-windres; do command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }; done
  CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++; TOOLCHAIN_ID="$(x86_64-w64-mingw32-g++ -dumpmachine)|$(x86_64-w64-mingw32-g++ -dumpfullversion -dumpversion 2>/dev/null || x86_64-w64-mingw32-g++ -dumpversion)|cross-linux"; CONFIG_ARGS='--host=x86_64-w64-mingw32 --disable-shared --enable-static'
fi
hash_text(){ printf '%s' "$1" | sha256sum | awk '{print $1}'; }
SODIUM_KEY="$(hash_text "$EXPECTED|$TOOLCHAIN_ID|$CONFIG_ARGS")"; SODIUM="$CACHE_ROOT/deps/libsodium/$SODIUM_KEY/prefix"; SODIUM_HIT=0
if [[ -s "$SODIUM/lib/libsodium.a" && -d "$SODIUM/include" ]]; then SODIUM_HIT=1; else
  rm -rf "$CACHE_ROOT/deps/libsodium/$SODIUM_KEY"; mkdir -p "$CACHE_ROOT/deps/libsodium/$SODIUM_KEY"
  SODIUM_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/glo-sodium.XXXXXX")"; trap 'rm -rf "$SODIUM_STAGE"' EXIT; mkdir -p "$SODIUM_STAGE/src" "$SODIUM_STAGE/prefix"
  tar --no-same-owner -xzf "$ARCHIVE" -C "$SODIUM_STAGE/src" --strip-components=1
  if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then (cd "$SODIUM_STAGE/src" && ./configure --prefix="$SODIUM_STAGE/prefix" --disable-shared --enable-static && make -j"${JOBS:-4}" && make install); else (cd "$SODIUM_STAGE/src" && ./configure --host=x86_64-w64-mingw32 --prefix="$SODIUM_STAGE/prefix" --disable-shared --enable-static && make -j"${JOBS:-4}" && make install); fi
  mkdir -p "$(dirname "$SODIUM")"; cp -a "$SODIUM_STAGE/prefix" "$SODIUM"; rm -rf "$SODIUM_STAGE"; trap - EXIT
fi
# Mutable CMake/Ninja state needs content-aware invalidation. Release bundles may
# be extracted over the same source path with preserved/equal timestamps, which
# can otherwise leave stale objects/libraries even though source bytes changed.
# Keep immutable/cacheable work separate: libsodium is keyed by archive checksum
# + toolchain above, and ccache keeps reusable compiler objects across invalidations.
BUILD_CACHE_SCHEMA="glo-app-cmake-v2"
FINGERPRINT_TOOL="$ROOT/tools/build_cache_fingerprint.py"
[[ -f "$FINGERPRINT_TOOL" ]] || { echo "Missing cache fingerprint helper: $FINGERPRINT_TOOL" >&2; exit 1; }
SOURCE_FINGERPRINT="$("$PYTHON" "$FINGERPRINT_TOOL" "$ROOT")"
[[ "$SOURCE_FINGERPRINT" =~ ^[0-9a-f]{64}$ ]] || { echo 'Invalid source fingerprint' >&2; exit 1; }
ROOT_KEY="$(hash_text "$ROOT|$TOOLCHAIN_ID|Release|GLO_STATIC_MINGW=ON|$BUILD_CACHE_SCHEMA")"
BUILD="$CACHE_ROOT/cmake/$ROOT_KEY"
# One-time cleanup of the pre-fingerprint mutable tree for this exact source path
# and toolchain. Immutable libsodium and shared ccache directories are untouched.
LEGACY_ROOT_KEY="$(hash_text "$ROOT|$TOOLCHAIN_ID|Release|GLO_STATIC_MINGW=ON")"
LEGACY_BUILD="$CACHE_ROOT/cmake/$LEGACY_ROOT_KEY"
LEGACY_PURGED=0
if [[ "$LEGACY_BUILD" != "$BUILD" && -d "$LEGACY_BUILD" ]]; then
  rm -rf "$LEGACY_BUILD"
  LEGACY_PURGED=1
fi
FINGERPRINT_FILE="$BUILD/.glo-source-fingerprint"
CMAKE_HIT=0
CMAKE_CACHE_STATE="MISS"
CMAKE_CACHE_REASON="no prior CMake build tree"
if [[ $LEGACY_PURGED == 1 ]]; then CMAKE_CACHE_REASON="legacy mutable tree purged; immutable caches preserved"; fi
if [[ -f "$BUILD/CMakeCache.txt" ]]; then
  if [[ -f "$FINGERPRINT_FILE" ]]; then
    PREVIOUS_FINGERPRINT="$(tr -d '\r\n' < "$FINGERPRINT_FILE")"
    if [[ "$PREVIOUS_FINGERPRINT" == "$SOURCE_FINGERPRINT" ]]; then
      CMAKE_HIT=1
      CMAKE_CACHE_STATE="HIT"
      CMAKE_CACHE_REASON="build inputs unchanged (${SOURCE_FINGERPRINT:0:12})"
    else
      CMAKE_CACHE_STATE="INVALIDATED"
      CMAKE_CACHE_REASON="build inputs changed (${PREVIOUS_FINGERPRINT:0:12}->${SOURCE_FINGERPRINT:0:12})"
      rm -rf "$BUILD"
    fi
  else
    CMAKE_CACHE_STATE="INVALIDATED"
    CMAKE_CACHE_REASON="legacy/incomplete tree has no source fingerprint"
    rm -rf "$BUILD"
  fi
fi
mkdir -p "$BUILD" "$OUT_ROOT/app"; rm -f "$OUT_ROOT/app/glo-app-windows-amd64.exe" "$OUT_ROOT/app/glo-app-windows-amd64.build.json"
CCACHE_ENABLED=0
CCACHE_REASON="unavailable (optional)"
CCACHE_EXE=""
CCACHE_CMAKE=""
CCACHE_MODE="${GLO_CCACHE:-auto}"
case "${CCACHE_MODE,,}" in
  auto|0|off|false|no|disabled|1|on|true|yes|required) ;;
  *) echo "Invalid GLO_CCACHE=$CCACHE_MODE (use auto, required/on/1, or off/0)." >&2; exit 2 ;;
esac
LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=)

ccache_is_required() {
  case "${CCACHE_MODE,,}" in
    1|on|true|yes|required) return 0 ;;
    *) return 1 ;;
  esac
}

ccache_is_disabled() {
  case "${CCACHE_MODE,,}" in
    0|off|false|no|disabled) return 0 ;;
    *) return 1 ;;
  esac
}

ccache_fail_or_fallback() {
  local message="$1"
  if ccache_is_required; then
    echo "ERROR: $message" >&2
    exit 1
  fi
  CCACHE_REASON="$message; direct compiler fallback"
  echo "WARNING: $message; continuing without ccache." >&2
}

ccache_compile_probe() {
  local probe_dir cc_cmd cxx_cmd c_src cxx_src c_obj cxx_obj
  probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/glo-ccache-probe.XXXXXX")"
  printf 'int glo_ccache_c_probe(void){return 0;}\n' >"$probe_dir/probe.c"
  printf 'int glo_ccache_cxx_probe(){return 0;}\n' >"$probe_dir/probe.cpp"
  if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
    # Native MinGW ccache and compiler receive native Windows paths. This uses
    # ccache's documented prefix mode: ccache compiler [compiler options].
    cc_cmd="$(cygpath -m "$(command -v gcc.exe 2>/dev/null || command -v gcc)")"
    cxx_cmd="$(cygpath -m "$(command -v g++.exe 2>/dev/null || command -v g++)")"
    c_src="$(cygpath -m "$probe_dir/probe.c")"
    cxx_src="$(cygpath -m "$probe_dir/probe.cpp")"
    c_obj="$(cygpath -m "$probe_dir/probe-c.o")"
    cxx_obj="$(cygpath -m "$probe_dir/probe-cxx.o")"
  else
    cc_cmd="$(command -v "$CC")"
    cxx_cmd="$(command -v "$CXX")"
    c_src="$probe_dir/probe.c"
    cxx_src="$probe_dir/probe.cpp"
    c_obj="$probe_dir/probe-c.o"
    cxx_obj="$probe_dir/probe-cxx.o"
  fi
  if "$CCACHE_EXE" "$cc_cmd" -c "$c_src" -o "$c_obj" >/dev/null 2>&1 \
     && "$CCACHE_EXE" "$cxx_cmd" -std=c++20 -c "$cxx_src" -o "$cxx_obj" >/dev/null 2>&1 \
     && [[ -s "$probe_dir/probe-c.o" && -s "$probe_dir/probe-cxx.o" ]]; then
    rm -rf "$probe_dir"
    return 0
  fi
  rm -rf "$probe_dir"
  return 1
}

if ccache_is_disabled; then
  CCACHE_REASON="disabled by GLO_CCACHE"
elif [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  # MSYS2 ships separate MSYS and MinGW ccache packages. For a MinGW compiler
  # use the native ccache.exe from the same MinGW prefix; never /usr/bin/ccache.
  COMPILER_BIN="$(dirname "$(command -v g++.exe 2>/dev/null || command -v g++)")"
  NATIVE_CCACHE=""
  if [[ -n "${MINGW_PREFIX:-}" && -x "${MINGW_PREFIX}/bin/ccache.exe" ]]; then
    NATIVE_CCACHE="${MINGW_PREFIX}/bin/ccache.exe"
  elif [[ -x "$COMPILER_BIN/ccache.exe" ]]; then
    NATIVE_CCACHE="$COMPILER_BIN/ccache.exe"
  fi
  PACKAGE_PREFIX="${MINGW_PACKAGE_PREFIX:-}"
  if [[ -n "$PACKAGE_PREFIX" ]]; then
    PACKAGE_HINT="${PACKAGE_PREFIX}-ccache"
  else
    PACKAGE_HINT="the ccache package matching the active MinGW environment"
  fi
  if [[ -z "$NATIVE_CCACHE" ]]; then
    FOUND_CCACHE="$(command -v ccache 2>/dev/null || true)"
    if [[ -n "$FOUND_CCACHE" ]]; then
      ccache_fail_or_fallback "native MinGW ccache.exe not found next to the active MinGW toolchain (ignoring $FOUND_CCACHE). Install with: pacman -S $PACKAGE_HINT"
    else
      ccache_fail_or_fallback "native MinGW ccache.exe not installed. Install with: pacman -S $PACKAGE_HINT"
    fi
  else
    CCACHE_EXE="$NATIVE_CCACHE"
    CCACHE_CMAKE="$(cygpath -m "$CCACHE_EXE")"
    CCACHE_DIR_POSIX="$CACHE_ROOT/ccache"
    mkdir -p "$CCACHE_DIR_POSIX"
    export CCACHE_DIR="$(cygpath -m "$CCACHE_DIR_POSIX")"
    "$CCACHE_EXE" --max-size "${GLO_CCACHE_MAX_SIZE:-1G}" >/dev/null 2>&1 || true
    if ccache_compile_probe; then
      "$CCACHE_EXE" --zero-stats >/dev/null 2>&1 || true
      CCACHE_ENABLED=1
      CCACHE_REASON="enabled (native MinGW: $CCACHE_CMAKE)"
      LAUNCHER=("-DCMAKE_C_COMPILER_LAUNCHER=$CCACHE_CMAKE" "-DCMAKE_CXX_COMPILER_LAUNCHER=$CCACHE_CMAKE")
    else
      ccache_fail_or_fallback "native MinGW ccache compile probe failed ($CCACHE_CMAKE)"
      CCACHE_EXE=""
      CCACHE_CMAKE=""
    fi
  fi
else
  if command -v ccache >/dev/null 2>&1; then
    CCACHE_EXE="$(command -v ccache)"
    export CCACHE_DIR="$CACHE_ROOT/ccache"
    mkdir -p "$CCACHE_DIR"
    "$CCACHE_EXE" --max-size "${GLO_CCACHE_MAX_SIZE:-1G}" >/dev/null 2>&1 || true
    if ccache_compile_probe; then
      "$CCACHE_EXE" --zero-stats >/dev/null 2>&1 || true
      CCACHE_ENABLED=1
      CCACHE_CMAKE="$CCACHE_EXE"
      CCACHE_REASON="enabled ($CCACHE_EXE)"
      LAUNCHER=("-DCMAKE_C_COMPILER_LAUNCHER=$CCACHE_CMAKE" "-DCMAKE_CXX_COMPILER_LAUNCHER=$CCACHE_CMAKE")
    else
      ccache_fail_or_fallback "ccache compile probe failed ($CCACHE_EXE)"
      CCACHE_EXE=""
      CCACHE_CMAKE=""
    fi
  else
    ccache_fail_or_fallback "ccache is not installed"
  fi
fi
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSODIUM_ROOT="$SODIUM" -DGLO_STATIC_MINGW=ON "${LAUNCHER[@]}"
else
  cmake -S "$ROOT" -B "$BUILD" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres -DSODIUM_ROOT="$SODIUM" -DGLO_STATIC_MINGW=ON -DCMAKE_BUILD_TYPE=Release "${LAUNCHER[@]}"
fi
cmake --build "$BUILD" --target GLO --parallel "${JOBS:-4}"
[[ -s "$BUILD/GLO.exe" && -s "$BUILD/GLO.build.json" ]] || { echo 'Missing executable or build identity' >&2; exit 1; }
# Commit the fingerprint only after a successful configure/build. A failed or
# interrupted mutable tree is therefore treated as incomplete on the next run.
printf '%s\n' "$SOURCE_FINGERPRINT" > "$FINGERPRINT_FILE.tmp.$$"
mv -f "$FINGERPRINT_FILE.tmp.$$" "$FINGERPRINT_FILE"
"$PYTHON" "$ROOT/tools/stamp_build_info.py" "$BUILD/GLO.exe" "$BUILD/GLO.build.json"; cp "$BUILD/GLO.exe" "$OUT_ROOT/app/glo-app-windows-amd64.exe"; cp "$BUILD/GLO.build.json" "$OUT_ROOT/app/glo-app-windows-amd64.build.json"
END_TS="$($PYTHON - <<'PY'
import time;print(time.time())
PY
)"; ELAPSED="$($PYTHON - <<PY
print(f'{float("$END_TS")-float("$START_TS"):.2f}')
PY
)"
echo; echo 'GLO app build cache'; echo '────────────────────────────────'
printf 'libsodium        : %s\n' "$([[ $SODIUM_HIT == 1 ]] && echo HIT || echo MISS)"
printf 'CMake build tree : %s - %s\n' "$CMAKE_CACHE_STATE" "$CMAKE_CACHE_REASON"
printf 'source fingerprint: %s\n' "${SOURCE_FINGERPRINT:0:16}"
if [[ $CCACHE_ENABLED == 1 ]]; then
  echo "ccache            : $CCACHE_REASON"
  "$CCACHE_EXE" --show-stats 2>/dev/null || true
else
  echo "ccache            : $CCACHE_REASON"
fi
echo "Build elapsed     : ${ELAPSED}s"; echo '────────────────────────────────'; echo "Built $OUT_ROOT/app/glo-app-windows-amd64.exe"
