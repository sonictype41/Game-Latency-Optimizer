# GLO v0.1.3-r4-beta OSS — release validation

Date: 2026-10-27  |  Input: `GLO_v0.1.3-r3-beta_OSS.zip`

## Scope shipped

1. Only the Services/Connect mode dropdown is locked while connection is pending/active/disconnecting; Account, Theme, Settings and Time Bank remain enabled as appropriate. Original safe-logout transaction retained.
2. A 400 CSS px topbar Time Bank selector with a two-row Free-only radio item, `(x / y phút)` on the first row and progress + action button on the second. The backend balance is `remaining_seconds + reserved_seconds` over `max_balance_seconds`. The earn action remains `open.time`. Premium is not simulated or selectable.
3. Services-specific relay diagnostic privacy: names or a safe fallback in `VERIFY000`/`NET002`, suppress candidate IDs in the app log for Services, and keep endpoint diagnostics for Custom Connect (including an Official-issued link opened in Custom mode). Adds two **optional internal network-worker IPC fields** with 15/16-field legacy parser acceptance. Relay socket target and client/relay wire protocol are unchanged. This is presentation redaction, not secrecy from the OSS client.
4. Connection-card captions are re-derived after every UI update; authoritative disconnected/idle native snapshots reset pending labels even if the worker never reached active.
5. Remove the large Services wallet card, compact section margins, adjust default/minimum logical client height from 720 to 700 (small work-area cap preserved).

## Verification performed

- `python -m pytest -q tests/`: **125 passed, 2 skipped**. These are source and Python contract checks, not a Windows executable run.
- `node --check app/frontend/ui/app.js`: passed.
- `node tests/r2_ui_runtime.cjs`: passed.
- `node tests/r3_ui_runtime.cjs`: passed.
- `node tests/r4_ui_runtime.cjs`: passed (Time Bank balances/visibility, topbar interactions while connected, pending/idle recovery, Custom mode).
- HTML ID uniqueness and referenced local asset existence: passed.
- Source comparison versus r3: `minimize_to_tray()` and `show_main_window()` have unchanged function bodies; Wintun implementation unchanged.
- ZIP CRC/integrity: verify at final packaging.

## Outstanding validation / limitations

- No Windows MinGW/MSVC/WebView2 SDK toolchain is available in this execution environment, so **no EXE/NSIS compilation or on-device connection was verified**.
- Verify with GitHub Actions Windows and a real Windows smoke test before publishing. Test both official GLO Services and Custom Connect to ensure diagnostic labels are correct in the elevated worker, and verify UI at 100%, 125% and 150% DPI; small monitors may still legitimately scroll.
- The log redaction does not make a relay IP secret: the OSS client and Windows networking stack must know the destination.
- The only worker IPC extension transports diagnostics labels. No relay wire framing or game routing parameters changed.
