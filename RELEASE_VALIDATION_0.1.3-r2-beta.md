# GLO v0.1.3-r2-beta OSS — Release validation

**Date:** 2026-10-27  
**Source baseline:** `GLO_v0.1.3-r1-beta_OSS(3).zip` (user-supplied)  
**Output:** `GLO_v0.1.3-r2-beta_OSS.zip` (source ZIP only)

## Patch boundaries
- **Changed:** Windows host `main.cpp`, `webview_host.cpp/.hpp`, official API error classification, local WebView2 UI/JavaScript/CSS, CMake Windows GDI+ link, release/version labels, changelog and regression tests.
- **Not changed:** Client network routing core, Wintun/TUN internals, network-worker IPC/wire protocol, relay engine or production/control-plane ART/UMTP. GLO Production v0.2.6-r10-beta was not modified.
- `VERSION` remains **0.1.3**; `RELEASE` and `DISPLAY_VERSION` are **0.1.3-r2-beta**.

## Automated checks performed in this environment
1. `node --check app/frontend/ui/app.js` — passed.
2. `node tests/r2_ui_runtime.cjs` — passed, including native ready handshake simulation, Roblox → disconnect → TFT, neutral placeholder after disconnect, Services busy-mode lock/release, Custom Connect progress and Settings layout state.
3. `python -m pytest -q tests --disable-warnings` — **114 passed, 2 skipped** after updating release-version expectations and a brittle exact serialization-helper count test.
4. ZIP archive integrity checked with Python `ZipFile.testzip()` — performed during packaging; no corruption reported.

## Build and manual-validation limitations
- **Windows EXE/NSIS were not built here:** this Linux container lacks the MinGW Windows toolchain, WebView2 Windows SDK and Wintun Windows runtime required by the Windows CMake target. The new native Win32/GDI+ code therefore requires a Windows CI build before being called binary-verified.
- A Linux host CMake configure was attempted and blocked by missing `libsodium` development package, so there is no compiled cross-platform C++ test result in this environment.
- WebView2 actual first-frame paint, system-tray restoration, live relay handoff, network-worker state, and gameplay sessions were **not** validated on a Windows host. Manual smoke tests must verify slow loading, missing UI assets/WebView2 runtime, flaky network, Custom URI, official Services, profile switching, disconnect and tray memory optimization.
- Native splash reads the already bundled `ui/assets/wordmark-light.png` on disk from the application directory, not WebView2; falls back to native text if the image is absent.

## Suggested Windows release gate
- Build x64 client and NSIS from the repository's existing Windows GitHub Actions workflows.
- On a clean Windows 10/11 VM verify the native splash appears immediately, WebView2 reveals without white flashes, Retry works for stalled initialization, and opening from tray with memory optimization reproduces the splash without disrupting the network worker.
- With a real provider verify Services/Custom progress, error codes, no token logging, a working connected dashboard, and no incorrect VALORANT card after TFT/Roblox disconnect.
- Do not publish a 'Windows build verified' claim without passing those steps.
