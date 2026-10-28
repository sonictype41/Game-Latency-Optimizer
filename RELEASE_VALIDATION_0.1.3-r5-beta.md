# GLO v0.1.3-r5-beta OSS — Release validation

Date: 2026-10-27  
Baseline: `GLO_v0.1.3-r4-beta_OSS.zip`  
Type: **source** (Windows executable not compiled here)

## Changes implemented

1. Auth Resilience: transient `/app/me`/token-refresh network faults do not invalidate a previously verified signed-in UI; bounded two-attempt startup/explicit recovery with 500 ms retry interval. Definitive refresh credential rejection still invalidates the login. Credentials stay within native DPAPI storage.
2. Time Bank: no 30-second polling; menu-opening event `wallet.refresh` only. Native single-flight request to existing `/app/me` balance source, 15-second successful-response TTL, 5-second retry backoff after a failed attempt, status indicators, stale balance preserved. Cached account/wallet/catalog is replayed after WebView2 UI recreation. Cache invalidated on Services connection start and completed Disconnect and cleared on Logout. No catalogue fetch during wallet refresh.
3. Game cards: only actual `Kết nối`/Connect buttons are clickable, including native keyboard activation; tile/cover/title inert.
4. Connection quality: inline SVG placeholder during Waiting/Standby and while gameplay metrics are not ready; real graph displayed for valid gameplay RELAY samples. No mock telemetry.
5. Always-on graph: remove Settings toggle and its JavaScript/native handlers, stop persisting the graph-preference field and ignore old `false` value. Native quality sample collection remains limited to valid game traffic.

## Verification performed (source-level / simulated)

- `python -m pytest tests/ -q`: **130 passed, 2 skipped**.
- `node --check app/frontend/ui/app.js`: PASS.
- `node tests/r2_ui_runtime.cjs`, `r3_ui_runtime.cjs`, `r4_ui_runtime.cjs`, `r5_ui_runtime.cjs`: PASS.
- HTML parser nesting and DOM reference/ID checks: PASS.
- Compare r4 vs r5: no changes under `app/core/` and no changes to `minimize_to_tray()` / WebView2 controller's tray lifecycle.
- ZIP `testzip`/CRC verification: PASS when packaged.

## Limitations / deployment caution

- **Windows compiler, WebView2 Runtime, game sessions, real API and network connectivity were not available in this Linux build environment**. Source-test PASS does not prove EXE/NSIS build or runtime readiness. Windows GitHub Actions build and live smoke tests remain necessary before production release.
- The existing backend `/app/me` response must continue to include the `balance` object. No new API endpoints or Production patch was made.
- The wallet value shown after an API failure is intentionally the last known result, not a guarantee of current spendable balance. Backend is still authoritative during `/app/session/prepare`.
- The spinner/theme/System Tray policies introduced in r3/r4 are preserved. The networking dataplane and ART/UMTP remain unchanged.
