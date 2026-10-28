# GLO v0.1.3-r3-beta OSS — Source validation

Date: 2026-10-27 (Asia/Ho_Chi_Minh)

## Scope

Built from the previously supplied `GLO_v0.1.3-r2-beta_OSS.zip` source. Includes r3 native splash animation/double-buffering, authentication-to-network-session sign-out ordering, static Services/Connect mode when busy/connected, authenticated Account visibility, input-only desktop context menu and native clipboard bridge, early authenticated UI visibility, and reduced redundant render work.

**Explicitly excluded:** any changes to `minimize_to_tray()` or WebView2 stop/recreate behavior upon tray minimize/restore. That function was byte-compared to r2 and is identical. No Wintun, UDP routing, network-worker wire protocol, relay or ART/UMTP changes.

## Validation conducted on this Linux container

- `python -m pytest -q tests`: **120 passed, 2 skipped**.
- `node --check app/frontend/ui/app.js`: **PASS**.
- `node tests/r2_ui_runtime.cjs`: **PASS** (previous Services/Custom lifecycle simulation).
- `node tests/r3_ui_runtime.cjs`: **PASS** (mode lock, account visibility, sign-out ordering presentation, stale auth events, native clipboard message behavior, input-only interaction policy).
- Test contracts validate native splash double buffering/timer, transaction guards, early authenticated UI, code locations, and preserved tray behavior. These are **source contracts** and do not substitute for executing Windows C++.
- Updated `RELEASE`, `DISPLAY_VERSION`, app footer and WinHTTP User-Agent to `0.1.3-r3-beta`; `VERSION` stays `0.1.3` for compatibility.
- ZIP integrity and file enumeration verified after packaging.

## Limitations and Windows smoke-test checklist

This environment **does not provide Windows SDK/MinGW and an installed WebView2 runtime**, so no Windows EXE, NSIS installer, native compile, WebView2 integration or gameplay test was performed. This ZIP is source-only. Before publishing a signed binary run GitHub Actions Windows build and test these manually:

1. Cold and warm start at 100%, 125%, 150%, and 200% scaling; check the solid arc, sharpness, and absence of flicker or dark-screen hang.
2. Connected Roblox/VALORANT/TFT and pending handoff: Services/Connect mode stays a static label without an active dropdown; Account remains usable when logged in.
3. Logout Cancel leaves the active session unchanged; Confirm cancels handoff/disconnects and only then clears the account. Verify no sign-in + old session overlap and no late handoff connection.
4. Retry while disconnect fails: login credentials remain, error is shown and connection status is not falsified.
5. Input menus on login code and custom URI, Cut/Copy/Paste/Select All shortcuts, disabled/read-only behavior, keyboard IME and text-edit accessibility. Context menus/selection outside inputs must be disabled without breaking controls.
6. Compare `PERF001..PERF004` markers and total WebView2 process private memory/working set between r2 and r3 on the same Windows machine. No measured reduction is claimed here.
7. System Tray optimization settings and network session persistence behave exactly as in r2; test both Optimize Memory on/off.

## Logo note

The r2 archive contains only a 640×204 PNG horizontal wordmark (no original SVG). The r3 native splash improves bitmap filtering but **cannot become truly resolution-independent SVG** without a genuine vector asset and Windows-native SVG renderer. The spinner is native GDI+, never HTML/WebView2.

## Security

Input clipboard messages originate from the local WebView2 UI and are bounded to 8192 bytes; clipboard contents and token/URI text must not be printed to logs. Native sign-out logs only fixed stage/status identifiers, and in-flight Official handoff tokens are ignored once sign-out starts.
