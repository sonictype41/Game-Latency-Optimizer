## [0.1.5-beta] - 2026-10-27

### Added
- Teamfight Tactics as the fourth Official Services card, with uploaded TFT poster artwork and supported-game selection in the native Services handoff.
- Advanced Routing selection from v0.1.4 remains an official-service preference, not a feature of the OSS game packet core.

### Improved
- Desktop window width fits exactly four 210px cards plus 17px gaps and 32px margins at normal DPI; responsive fallback on small monitors.
- Correct Vietnamese/English Advanced Routing labels and tooltips.
- No duplicate "Connected / Waiting for game" text for ordinary relay connections; use WAITING/RELAY and the optional Advanced Routing badge.
- Time Bank, UI recovery and responsive Connected Dashboard improvements from prior updates are preserved.

### Fixed
- Corrected duplicated localization keys for the Advanced Routing checkbox and tooltip.
- Removed redundant waiting-status text on the Standard connected card.

### Compatibility and limitations
- Live TFT routing requires the Production operator to publish a verified SG game profile; the client cannot fabricate destinations.
- No ART/UMTP changes to the OSS network engine, relay, packet transport or wire protocol. Windows binary not built on Linux.

## [0.1.4-beta] - 2026-10-27

### Added
- Optional Advanced Routing checkbox immediately to the left of region selection in GLO Services, default off, with a translated tooltip.
- Read-only ART Time-Factor sourced from authenticated Production catalogue; ART selection disabled if the server does not advertise a valid factor.
- Advanced Routing label and Route name on the Connected dashboard when an official ART session has been chosen.
- Native-to-frontend display metadata that does not modify the session-config schema, network core, ordinary relay engine or client–relay wire protocol.

### Changed
- Official Services prepare request includes standard/advanced routing preference; backend owns route selection and billing.
- Cached authenticated catalogue retains the advertised factor after restoring WebView2.

### Fixed
- ART factor cache survives WebView2 reconstruction without falling back to a fabricated default.

### Security & compatibility
- No ART/UMTP logic is introduced into the OSS network engine, relay, packet inspection, or wire protocol.
- Third-party Custom Connect stays unchanged and can still use ordinary provider URI sessions.
- GLO Services errors are displayed from backend error codes; the client cannot independently guarantee refunds.

### Known limitations
- No trusted downstream ART RTT feed is available in this baseline; client measures the ordinary candidate HEAD RTT, not a verified end-to-end estimate.
- Windows executable/installer has not been built or smoke-tested in this Linux environment.

# Changelog — public OSS releases only

## [0.1.3-r5-beta] - 2026-10-27

### Added
- **On-demand Free Time Bank refresh**: opening the 400px Time Bank dropdown requests a wallet read from the native Official API with a **15-second successful-result cache**, loading indicator (`Đang cập nhật...`) and unobtrusive error status. In-flight requests are coalesced; stale balances remain visible after transient failures.
- **Connection-quality placeholder**: a local inline SVG and localized gameplay-waiting/collecting messages occupy the empty stats area until real RELAY gameplay samples exist. No synthetic metrics or mock chart are drawn.
- **Auth recovery**: transient startup/network errors receive a bounded identity verification retry; a previously verified account does not lose the Services UI because of a failed balance refresh. WebView2 recreation replays cached account, catalog and wallet data without an automatic wallet poll.

### Changed
- Wallet checks are separated from catalog retrieval: the Time Bank refresh only requests `/app/me` (the existing balance source). A cache is invalidated after official connection attempts or completed Disconnect, and cleared on Logout. No new backend endpoint or billing semantics.
- The game tile is presentation-only. **Only** its actual Connect button starts a session (also with keyboard activation); clicking cover art, title or card background does nothing.
- Quality history collection/graph rendering is always enabled when eligible gameplay data exists. The redundant Show Graph setting, native persistence field and its UI/translation handling are removed. Legacy `show_network_quality_graph=false` settings are ignored.

### Fixed
- A transient network/HTTP 429/5xx failure on a wallet refresh no longer automatically turns an authenticated Services window into the blocking `Could not verify sign-in` screen. Definitive token invalidation is still enforced.
- Unnecessary 30-second timer-based wallet/account/catalog polling is removed; restoring the desktop UI does not trigger a network wallet refresh.

### Security / compatibility
- Retains Windows DPAPI refresh token handling, authenticated preparation, and Safe Logout/Disconnect sequencing; no token material is exposed to WebView2. No ART/UMTP knowledge is introduced to OSS client/ordinary relay.
- Does not change the Wintun dataplane, public relay wire protocol, Production, or any WebView2 System Tray minimize/recreate policy. `VERSION` remains `0.1.3`.

### Validation limits
- Source/contract tests, Node.js frontend simulations, HTML structure and ZIP CRC are checked in this environment. Windows EXE/NSIS compilation and live WebView2/API/gameplay validation are not available here. See `RELEASE_VALIDATION_0.1.3-r5-beta.md`.

## [0.1.3-r4-beta] - 2026-10-27

### Added
- **Topbar Time Bank selector:** the Free wallet is now a compact icon beside Account, visible only for logged-in GLO Services sessions, including both lobby and active-session views. The dropdown is approximately **400 CSS px** wide with exactly two visual rows: the selected **Free Time Bank (x / y phút)** item (checkmark) and a single line containing the progress bar plus the green **Nhận thời gian / Earn time** button. Its original `open.time` action and backend wallet fields are retained, including `remaining_seconds + reserved_seconds` for the displayed total; reserved/available information remains accessible to assistive technologies.
- An exclusive `radiogroup` / `radio` selection foundation for future bank types. **Only Free exists in r4 and remains selected**. Premium is neither simulated nor selectable. Any future bank switch must be backend-confirmed, only one bank may be active, and active sessions must not switch bank midway.
- Internal worker-IPC backward-compatible *diagnostics-only* metadata (`hide_relay_endpoint_in_log` and a verified-session relay label). The worker accepts legacy 15-/16-field CONNECT commands and new 18-field commands. No game packet routing, relay protocol or session entitlement semantics changed.
- Source regression tests for the new UI, session-state reset, privacy policy, and older worker IPC acceptance; r4 runtime UI simulation covers topbar actions, Time Bank and Connecting recovery.

### Fixed
- **Topbar interaction:** remove the r3 CSS that disabled and greyed out the entire Account/Settings/Theme group while connected. Keep **only** the Services/Connect mode dropdown disabled during Connecting, Connected and Disconnecting; Account, Settings, Theme and Time Bank remain clickable. Opening one topbar menu closes the other; clicking outside or pressing Escape dismisses menus.
- **Log privacy:** trusted GLO Services relay-selection (`VERIFY000`) and DNS-resolution (`NET002`) entries write the safely normalized server name (or `GLO-Relay` fallback) **instead of relay IP:port**, including failure cases. Custom/third-party Connect retains the full endpoint for diagnostics. Redaction changes only human-readable logs, never the actual relay socket address; because OSS clients need that address to connect, this is **not** IP secrecy or access control.
- **Stuck Connecting card:** each game button's caption is reconciled on every render after translation, instead of permanently overriding text after a single Connect action. A native disconnected/idle snapshot with no pending worker or prepare activity clears the UI's pending label even when Connecting failed before a worker was observed. No client-side fake disconnection timeout was added.
- **Window layout:** remove the oversized Services wallet panel, move the game section upward, reduce its spacing, and set the default/minimum client-area height target from 720 to 700 logical pixels. The existing small-monitor work-area cap and responsive scrolling on genuinely smaller screens remain in place.

### Preserved boundaries
- Unmodified Wintun routing, packet forwarding, GLO relay wire protocol, ART/UMTP transparency, Production service, native splash behavior, safe logout and Win32 WebView2 System Tray lifecycle. The only internal worker IPC extension is optional presentation metadata, not a change to relay/game network traffic.

### Validation and limitations
- Source/contract tests plus Node.js UI simulations and ZIP CRC validation are run in this environment. **Windows EXE/NSIS compilation, real DPI/scrollbar rendering and on-device connection tests remain unverified** without the Windows WebView2 toolchain. See `RELEASE_VALIDATION_0.1.3-r4-beta.md`.

## [0.1.3-r3-beta] - 2026-10-27

### Added
- **Transactional safe sign-out:** the native Win32 host now accepts `auth.logout` as a coordinated operation, cancels any unfinished URI inspection/handoff, discards in-flight Official `prepared` results and stops an active network-worker session **before** calling the Official API logout routine. A dedicated progress overlay prevents conflicting UI operations while this transaction is running.
- The Account sign-out confirmation now warns explicitly that an active or connecting GLO session will be disconnected. Cancel leaves the session intact. Native disconnect confirmation is authoritative; if disconnection cannot be confirmed, sign-out is aborted without clearing saved credentials.
- Sign-out progress and completion diagnostics (`AUTH030`–`AUTH034`); late pending preparation responses and stale auth/catalog/wallet UI callbacks are suppressed while logout is pending. An explicit final `reason=logout` acknowledgment identifies the actual completion, rather than treating unrelated `logged:false` responses as success.
- **Desktop input context menu:** themed Copy, Cut, Paste and Select All menu for GLO text input / textarea only, with keyboard shortcuts displayed. Copy, Cut and Paste have native Windows clipboard bridge actions; Cut only removes selected text after a successful native clipboard write acknowledgment. Disabled/read-only inputs are respected.
- Native startup timing markers (`PERF001`–`PERF004`) for the Win32 window, WebView2 startup request, JavaScript bridge readiness and first frontend-ready signal. The markers enable on-device profiling without exposing token/URI data.
- Additional source and Node.js UI regression tests covering active-session mode lock, account visibility, safe logout, asynchronous event ordering and desktop edit policy.

### Fixed
- **Native splash flicker:** Win32 splash now draws to a compatible offscreen bitmap and commits atomically via `BitBlt`, eliminating visible intermediate erase/logo/text frames. The loading animation is a continuously rotating, anti-aliased **solid arc** instead of twelve stepped dots.
- Splash render cadence changed from the unrelated 250 ms status timer (~4 FPS) to a dedicated ~33 ms timer (~30 FPS) that stops after the WebView2 frontend is revealed. GDI+ high-quality image interpolation and pixel offsets improve scaling from the bundled raster wordmark. The splash remains completely independent of WebView2.
- **Connected topbar:** Services/Connect mode is now a noninteractive, static-looking label with its chevron hidden throughout Connecting, Connected, Disconnecting and Logout. The Account button remains accessible to authenticated users who may need to sign out.
- **Unauthenticated topbar:** hide Account while logged out or verifying authentication; leave Settings and Dark/Light controls visible.
- **Mixed sign-in/dashboard screen:** never show the sign-in form while the native session is active; clear stale UI metadata on successful sign-out and ignore old positive auth events during logout.
- Prevent native Custom Connect confirmation from starting during a pending logout, including if a delayed UI callback arrives after the user has requested sign-out.
- Remove browser-like selection/copy/drag of application labels and artwork outside editable fields while preserving keyboard focus, navigation and native editing/IME inside fields.

### Performance
- Remove the extra fixed 80 ms readiness delay after initial UI render; schedule frontend-ready notification at the next event-loop turn, **after** `render()` completes. WebView2 still remains hidden until the native host receives readiness.
- Emit the authenticated UI state as soon as `/app/me` verifies the account, without waiting for `/app/booster/catalog` to complete; the catalog continues loading separately. The backend API and existing account/token format are unchanged.
- Reduce redundant WebView2 DOM work during 1-second snapshots: translate static labels only on language change, update status text only when its value changes, skip unchanged catalog/region layouts and avoid reassigning identical artwork sources.
- Performance changes are intended to improve perceived readiness and frontend CPU overhead, **not** to promise a specific RAM or launch-time reduction. Actual Windows WebView2 process totals require measurement.

### Compatibility, security and explicit exclusions
- Preserve `VERSION=0.1.3` and all packet/wire/relay/Wintun behavior; only release/display strings advance to `0.1.3-r3-beta`.
- **No change whatsoever to `minimize_to_tray()` or WebView2 release/recreate policy on tray**, including existing Optimize Memory settings.
- Keep ART/UMTP, route selection and Production orchestration entirely outside OSS client/ordinary relay.
- Native clipboard contents are transferred only for a user-triggered edit action on the local WebView2, capped at 8192 bytes, and never written to logs; no provider URI/session grant/private key is intentionally logged.
- No original vector wordmark is bundled in the r2 archive. The r3 splash uses the original PNG with improved filtering; **this is not true SVG/vector rendering**. A genuine logo SVG would be needed for resolution-independent edges.

### Validation / release status
- Python/Node source and simulated UI tests are recorded in `RELEASE_VALIDATION_0.1.3-r3-beta.md`.
- **Windows EXE build, WebView2 runtime, high-DPI rendering, on-device clipboard behavior, cold-start benchmark and a live session/logout test have not been verified on this Linux host.** Windows GitHub Actions and real-device smoke tests are required before calling this a validated binary release.

## [0.1.3-r2-beta] - 2026-10-27

### Added
- **Native Win32 splash screen:** dark GLO-branded startup/restoration screen with the bundled long PNG wordmark, an animated native GDI+ activity indicator and status text. It is painted by the Windows host before WebView2 starts; it does **not** depend on HTML, the WebView2 renderer, network requests, or authentication.
- A WebView2 UI-ready handshake: the controller starts hidden and is revealed after the local frontend initializes its DOM and notifies the native host. The existing local `ui/assets/wordmark-light.png` is reused, with a native text fallback if the image cannot be loaded.
- A 30-second native first-frame watchdog with Retry/Cancel messaging rather than an indefinitely blank application window. Optional tray memory optimization continues to dispose of and recreate only WebView2, not the live network connection.
- Shared Connecting progress UI for Official Services and Custom Connect URI inspection/confirmation; new inspection, relay-probe and handoff-redeem stages (driven by existing native handoff worker state, not invented percentages). VI/EN strings for new stages.
- Additional sanitized `UI001`–`UI004`, `CONNECT001`, `CONNECT012`–`CONNECT014`, `URI021`–`URI022`, and `API021` lifecycle/diagnostic events; API preparation failures now carry allowlisted error codes, the HTTP status and local WinHTTP error number where available.
- New source-level and Node.js VM regression tests for startup, connection state, native-to-UI transitions, dashboard game identity, TFT Custom Connect, and locked navigation.

### Fixed
- The first visible window no longer displays WebView2's pre-navigation white surface: the native splash stays visible while the WebView2 controller is hidden.
- Custom Connect progress is no longer inside the Services-only `#service-picker`. The shared progress panel works on both pages, including URI inspection and paste.
- Prevent switching Services/Connect, opening the mode dropdown, or interacting with topbar actions during connecting or disconnecting; the mode label remains visible without its chevron. Native operation remains authoritative.
- Hide the entire topbar on Settings while retaining the local Back button and moving the Settings title toward the top.
- Remove the fallback to the previously selected VALORANT game from native **session snapshots**. The selection is kept separately for Services convenience and is not an indication of a live session.
- Remove the VALORANT placeholder from the disconnected dashboard HTML and JavaScript. Render current-game artwork only from confirmed active connection metadata; use the neutral placeholder for unknown/custom games, including `tft`, which has the display name Teamfight Tactics.
- Clear stale active-game presentation on disconnect; avoid using the previously selected Services region for Custom Connect sessions.
- Remove the JavaScript-only 60-second Services timeout that unlocked controls while native API/handoff/network workers could still be running. State transitions are tied to native snapshots, authoritative errors and handoff completion instead.
- Return localized VI/EN, actionable, non-sensitive API failure categories such as `NETWORK_TIMEOUT`, `AUTH_REQUIRED`, `RATE_LIMITED`, `SERVICE_MAINTENANCE`, `PROFILE_UNAVAILABLE` and `HANDOFF_CANDIDATES_UNAVAILABLE`. Preserve the insufficient-time flow for `BALANCE_EMPTY`.
- Record a safe handoff failure category in the native log **before** notifying the UI; do not forward untrusted/raw provider errors that may contain secrets.

### Changed
- Keep `VERSION=0.1.3` (wire/compatibility version); bump both `DISPLAY_VERSION` and `RELEASE` to `0.1.3-r2-beta` and update the footer and official app HTTP user agent.
- Keep the r1 official/custom session handoff and network-worker IPC unchanged. The progress UI represents existing native stages; no new provider API methods, session fields on the wire, game routing rules, Wintun hooks or ordinary relay behaviors are introduced.
- Preserve archived changelog entries for historical beta revisions. Update release-version assertions in repository tests and document the new UI behavioral contracts.

### Security and OSS boundary
- GLO OSS knows only client-visible game/session/network-worker state and public service errors. No ART/UMTP, proprietary route selection, provider secrets or Production-only code is added.
- No raw token, private key, Authorization header, full provider URI or remote response body should appear in new logs. API error strings are local, fixed messages; only safe identifiers and numeric statuses are emitted.
- The existing Wintun/TUN gameplay-UDP routing, security/admission policy, relay protocol and Production `v0.2.6-r10-beta` remain untouched.

### Validation and known limitations
- Source checks: `node --check app/frontend/ui/app.js`, Node.js UI simulation, release/build-policy tests and complete Python test suite (results recorded separately in `RELEASE_VALIDATION_0.1.3-r2-beta.md`).
- **No Windows EXE/installer built or live Valorant/Roblox/TFT gameplay tested here.** A native MinGW/Windows CI build and a Windows WebView2/tray smoke test remain mandatory before publishing a verified binary release. Linux host also lacks libsodium development dependencies for a local CMake build.
- The splash image depends on the existing copied `ui/assets/wordmark-light.png` being present alongside the built application. Text fallback is provided if packaging omits it.

## [0.1.3-r1-beta] - 2026-10-22

### Fixed
- Synchronize WebView2 connection lifecycle after failed Custom Connect handoffs or invalid session configs; errors now reach the dashboard and unblock retry without restarting GLO.
- Keep Custom Connect verification progress separate from Official Services game-card states, preventing unrelated Roblox/VALORANT/PUBG cards from being marked as connecting.
- Guard against stale Services connection-timeout callbacks resetting a newer attempt.
- Reset pending UI state on errors, expiry, insufficient time and connection termination.

### Added
- Enable hidden TFT TEST/Custom Connect support without changing the Official Services catalog.

### Experimental (not published)
- Recognize `tft` (Teamfight Tactics) in the session-config parser, native Game Registry and privileged network worker for TEST/Custom Connect URIs only.
- Detect TFT Unreal gameplay via `TFTClient-Win64-Shipping.exe` (Windows process enumeration).
- No TFT Official Services card, catalog publishing or default CIDR/port rules; routing policy must come from a valid provider-issued TEST config.
- Retain Wintun/TUN-only UDP gameplay routing and existing protocol compatibility.

## [0.1.3-beta] - 2026-10-21
### Added
- Added local PUBG: BATTLEGROUNDS game-card artwork, hover/focus caption behavior and a unified Connection Dashboard for Official Services and custom URI sessions.
- Display the selected game's supported artwork, or a neutral Custom Game placeholder if a game lacks packaged artwork. Unrecognized routing profiles remain subject to the native safety policy and are not granted arbitrary UDP access.
- Added a custom provider IP/port visibility toggle with a masked display when hidden; Official Services continues to display relay node names.
- Display the five-minute relay quality graph for both Official and Custom connections only when valid gameplay RELAY telemetry is present and the graph setting is enabled.
- Added a native anti-overlap guard for Official prepare, custom URI inspection, Windows deep links, provider confirmation, connection and disconnection, with contextual warnings referencing the current game and Services/Connect mode.
### Fixed
- Prevent initiating a second handoff while a Services session, custom session, or pending provider confirmation is active; preserve the original session source.
- Correct Custom Connect node masking and graph display without duplicating the dashboard.
### Changed
- Refined the GitHub SVG icon, reduced the default window height, and only reveal game-card details and Connect buttons on hover or keyboard focus (also accessible on non-hover devices).
- Preserved existing Wintun routing, gameplay-only traffic policy, session admission, official identity and custom URI confirmation.
### Known limitations
- New unsupported game identifiers cannot be routed safely without a compatible native gameplay profile; a placeholder is a presentation fallback, not an implicit authorization for unrestricted tunneling.
- Windows MinGW build and gameplay/relay behavior require GitHub Actions and on-device verification.

## [0.1.2-beta] - 2026-10-20
### Added
- Separate titlebar Minimize from close-to-tray. By default, Minimize keeps the WebView2 UI alive; close-to-tray preserves the current optional UI memory release behavior.
- Add a default-off subordinate setting to release WebView2 on titlebar Minimize when main memory optimization is enabled.
- Set DPI-aware minimum window tracking dimensions, respecting the usable monitor size; center the Settings layout.
- Restore r3-style hover/focus reveal of individual game Connect buttons with connection progress feedback.
- Show native session restoration/loading progress instead of temporarily presenting the sign-in screen; preserve encrypted credentials on transient API failures.
- On the Connected dashboard, hide Free Time Bank, put Disconnect at the bottom of the game card, and expand Session Left and Node when gameplay relay metrics are not available.
### Fixed
- Avoid false sign-out flashes during async account restoration and retain credentials during transient API outages.
- Hide inapplicable Ping/Packet Loss cards and empty graph while waiting for gameplay.

### Security and compatibility
- No Wintun, routing protocol, relay admission, or Production API contract changes.

## [0.1.1-beta] - 2026-10-20
### Added
- Default-on memory optimization when minimized to tray and optional relay quality chart.
- Native five-minute ping/loss ring buffer and separate Connected Dashboard.

### Fixed
- Services cards modeled after Production r3, full VI/EN interface switching, SVG icons, responsive settings layout.
- Safer asynchronous WebView2 creation/destruction and renderer restoration from tray.
- GLO C++/Wintun transport remains unchanged.

## [0.1.0-beta] - 2026-10-20

### Added

- Rebuilt the Windows application UI using locally packaged HTML/CSS/JS and Microsoft WebView2 Evergreen Runtime.
- Added native one-time code sign-in, DPAPI-protected refresh sessions, Official Services, Time Bank and game/region selection.
- Added API-driven Official handoff flow while preserving the existing Wintun/network worker, relay probing, and custom URI inspection/confirmation.
- Added a connection dashboard with gameplay relay/direct state, latency/loss, session remaining, and node information.
- Integrated theme, settings, account state, tray behavior and GLO Web links.
- WebView2 SDK is a pinned build-time NuGet dependency; WebView2 Evergreen Runtime is needed on client machines.

### Fixed

- Replaced the website-led gameplay connection workflow with Native Booster while preserving custom URI handoff and relay policy behavior.

## [0.0.6-beta] - 2026-10-18

### Added
- PUBG Game ID and process detection (TslGame.exe) from Game Profile; no hardcoded PUBG IPs or UDP ports.
- Aligned public IPv4 CIDR targets /16 through /32 in session parser, Windows Wintun routes and Go relay policy.
- Non-game CIDR safeguard: add more-specific native /32 direct-route exceptions on first non-game packet. The first affected packet is dropped (no raw packet replay); on exemption failure, fail-open the whole profile to direct. No WFP.
- Async dispatch for incoming glo:// WM_COPYDATA handoff while Settings/UI is open, plus a failed-forward notification.
- Targeted CIDR / PUBG / UDP expression and relay-policy regression tests.

### Fixed
- Incoming URI handoff is deferred out of synchronous WM_COPYDATA handling, reducing Settings UI lockups.
- Forwarding failures now notify the user rather than silently exiting.

### Compatibility / limitations
- Source backport based on the previously available OSS 0.0.7-beta tree, released with coherent 0.0.6-beta metadata by explicit request. The application wire protocol is unchanged.
- Deploy updated relay and Node Agent before enabling wide CIDR profiles in Production 0.0.7-r1-beta. Legacy runtime rejects /16 instead of silently routing.
- Native Windows Wintun and Settings-to-URI behavior require integration testing on Windows before public rollout; portable tests are not a substitute.
- Broad CIDR routes are system-wide. A non-game IP is excluded entirely, which may also leave gameplay to that same IP direct.

## [0.0.5-r3-beta] - 2026-10-14

### Fixed
- Isolated flow-scoped `RelayRejectReason::Temporary` responses from session-level transport failure. A temporary rejection of one nonzero Flow ID now retains the existing relay route and can be retried naturally by subsequent packets; it no longer causes whole-session `ROUTE026` fail-open. Session-capacity and maintenance rejection still fail-open, with accurately classified reasons.
- Windows Wintun local UDP tuple mappings are reclaimed after 60 seconds of inactivity in **both** traffic directions, with a periodic five-second sweep. Route teardown continues clearing local state, and flow IDs remain monotonic for a relay session (not constrained by `max-flows-per-session`).
- Separated actual heartbeat loss and unexpected Wintun shutdown in fail-open logs, avoiding misleading `relay_unavailable` for every rejection.

### Added
- `FLOW010` rejection diagnostics with rejection reason, Flow ID and action; `FLOW011` aggregated local idle cleanup telemetry with remaining local flows. No game payloads, tokens or private provider information are logged.
- Portable C++ policy tests and source-level regression checks for flow timeout/rejection and unaffected real transport fail-open.

### Notes and limitations
- `VERSION=0.0.5` wire compatibility and current session schemas remain unchanged; compatible OSS relays and self-host sessions require no upgrade for this client-only behavior.
- Local idle tuple eviction does not change relay socket GC settings. If a relay rejects a flow for being over quota, that specific flow may still lose packets; the tunnel stays available for other flows. Real heartbeat loss and Wintun failures still fail-open.
- Windows/Vanguard live regression testing is required before broad release.

## [0.0.5-r2-beta] - 2026-10-14

### Added
- UDP Port Rule Expressions for individual ports, several ranges, and mixed rules, such as `7000-8000,8181`. Validate, sort and merge overlaps/adjacent ranges; enforce a maximum of 32 entries and valid UDP ports (1–65535).
- Strict schema 3 session config support for multiple UDP ranges, including end-to-end forwarding through the Windows network worker IPC and Wintun. Schema 2 single-range sessions continue to work.
- Responsive handoff status UI with progress spinner for provider inspection, relay measurement and redeeming a session; Cancel prevents late responses from starting a connection.

### Fixed
- Win32 UI stalls / apparent Not Responding while HTTPS inspection, candidate RTT probes or redemption were running inside the window message procedure.
- New URI handoffs return quickly from the single-instance message handler instead of waiting for provider network calls.
- Noncontiguous allowlisted UDP ports no longer require opening the entire enclosing port span. An explicitly allowlisted port (such as 8181) is forwarded through the relay, not bypassed directly.

### Notes and limitations
- The provider must publish corresponding relay policy entries for every new port rule. An older OSS client cannot interpret schema 3 and needs upgrading before using a multi-rule profile.
- As before, Windows Wintun /32 host routes affect other protocols/ports destined for the same IPv4 hosts; traffic not allowlisted for relay is not transparently redirected to Direct.
- Cancellation ignores late results but may not interrupt an already running HTTPS request immediately. Windows GUI/relay integration testing is recommended before broad release.

## [0.0.5-r1-beta] - 2026-10-13

### Changed
- Increased the **global** relay reverse-flow verification grace period from 1,800 ms to 3,000 ms for all supported games. A valid S2C reply still confirms Relay immediately; only unsuccessful verification waits longer before Direct fail-open.
- Verification deadline now starts with the first successfully forwarded gameplay flow and is not restarted by later flow-opening events.

### Added
- Debug-only `VERIFY000`–`VERIFY006` diagnostics for relay endpoint, flow destination and ports, verification timing, local C2S/S2C/injection counters and timeout summary. No token or packet payload logging.
- Debug-only Linux relay first-flow TX and first reverse-reply diagnostics, correlated using session and flow IDs.
- Portable regression coverage for reverse-before-forward event ordering and bounded early reverse evidence.

### Fixed
- Prevented a successful Wintun reverse injection from being missed when its event is observed before `FirstForwarded` is processed. Flow and route epoch must still match before Relay lock.

### Known limitations
- A 3-second timeout cannot fix an upstream that never replies; when verification fails, Direct fail-open may now take up to 1.2 seconds longer. Existing system-wide Windows host-route limitations are unchanged.
- Live Windows / Vanguard compatibility and end-to-end A/B checks are still required; no claims of improved Valorant game ping are made.

## [0.0.5-beta] - 2026-10-13

### Added
- VALORANT is the second game supported by Game Latency Optimizer (GLO), following Roblox. Game and region endpoints remain provider-configurable.
- VALORANT process detection and multi-game session selection in the Windows client.
- Native Windows session notifications with the GLO app icon when an active session has 5 minutes and 1 minute remaining.

### Changed
- Generalized supported-game selection without changing the Wintun-based packet routing architecture or existing Roblox defaults.

### Fixed
- Removed the Roblox-only IPC worker rejection for validated VALORANT sessions while keeping unsupported game IDs rejected.

### Known limitations
- Windows exact host routes affect all traffic to selected game IPs; unrelated packets on a routed destination may be dropped. VALORANT routing and Vanguard compatibility require live Windows verification before broad rollout.
- The notification display depends on Windows notification settings and Do Not Disturb.

## [0.0.6-beta] - 2026-10-18

### Added
- PUBG Game ID and process detection (TslGame.exe) from Game Profile; no hardcoded PUBG IPs or UDP ports.
- Aligned public IPv4 CIDR targets /16 through /32 in session parser, Windows Wintun routes and Go relay policy.
- Non-game CIDR safeguard: add more-specific native /32 direct-route exceptions on first non-game packet. The first affected packet is dropped (no raw packet replay); on exemption failure, fail-open the whole profile to direct. No WFP.
- Async dispatch for incoming glo:// WM_COPYDATA handoff while Settings/UI is open, plus a failed-forward notification.
- Targeted CIDR / PUBG / UDP expression and relay-policy regression tests.

### Compatibility / limitations
- Source backport based on the previously available OSS 0.0.7-beta tree, released with coherent 0.0.6-beta metadata by explicit request. The application wire protocol is unchanged.
- Deploy updated relay and Node Agent before enabling wide CIDR profiles in Production 0.0.7-r1-beta. Legacy runtime rejects /16 instead of silently routing.
- Native Windows Wintun and Settings-to-URI behavior require integration testing on Windows before public rollout; portable tests are not a substitute.
- Broad CIDR routes are system-wide. A non-game IP is excluded entirely, which may also leave gameplay to that same IP direct.

## [0.0.4-beta] - 2026-10-09

### Added
- Profile-driven, bounded exact IPv4 host pre-routing through Wintun for UDP games; Roblox SG bundled profile.
- Accept provider/self-host gameplay profile identifiers, revisions, host sets and UDP port policy in strict session schema 2.
- Removed the previous WFP BLOCK endpoint gate and Roblox log hint watcher from the OSS client and build.
- Updated relay-side destination/port policy and diagnostics; kept plaintext gameplay datagrams.
### Fixed
- Fixed the Windows MinGW build by restoring the route-activity predicate and removing stale preflight endpoint references from the Wintun flow tracker.
- Removed the old first-UDP gate timing failure path; this change requires live Windows validation.

### Known limitations
- **Limitations:** Windows host routes apply system-wide; third-party traffic to those hosts can be affected. Gameplay-only isolation is not guaranteed by Windows route tables. Windows live validation required.


All notable public changes to GLO OSS are documented here.

The public release history starts at `v0.0.1-beta`.

## [0.0.3-beta] - 2026-10-08

### Added
- Optional, game-specific **read-only Roblox log hint adapter** behind a game-neutral session/endpoint event interface. It watches locally for match boundaries and UDMUX gameplay endpoint hints; reads only matching fields, never uploads log lines, and never needs game modification, injection, driver installation, or anti-cheat access.
- `HINT002` diagnostic event for a validated endpoint hint. `ROUTE007` now identifies its source (`game_hint` or `wfp_gate`) to distinguish a pre-connect hint from a WFP discovery on an existing UDP association.
- Portable parser tests and `docs/ROUTING_LIFECYCLE.md` explaining extension to other supported games, the limited trust of hints, and known Windows socket-routing limitations.

### Changed
- The common Windows routing controller accepts optional early, validated endpoint hints. It can prepare a narrowly scoped `/32` before the game opens a UDP flow **when the hint arrives early enough**, while retaining the existing WFP discovery fallback and post-forward/reverse-flow verification.
- Per-game session-start/end events can reset the route cycle **without a Roblox PID change**. Same-process recovery re-arms the existing dynamic WFP gate and keeps bounded Direct fail-open when verification fails.
- Relay dataplane, node agent, signed session grants, GLO6 wire format, Wintun runtime, default game-only UDP forwarding rules, and production service logic remain unchanged.

### Fixed
- Recoverable DirectLocked state no longer requires terminating the game process when a trustworthy game-session boundary is observed.
- DirectLocked connection status is retained in the Windows client instead of reverting immediately to a generic waiting-for-gameplay message.

### Known limitations
- This is **best-effort compatibility**, not a verified fix for all Roblox 0.742 connections. UDMUX log emission may happen too late to precede a socket's route/source-address selection; GLO cannot transparently migrate an already connected UDP socket using its current non-invasive, user-mode design. Route verification and fail-open remain mandatory. Native Windows build and live multiplayer validation are required before wide deployment.

## [0.0.2-beta-r1] - 2026-10-08

### Added
- Verification-only `WINTUN007` receive/filter/forward diagnostics and `NET006` UDP serialization/socket-send counters. `ROUTE016` includes the furthest confirmed stage without capturing payloads or tokens; snapshots are emitted only when Debug is enabled.
- `docs/SELF_HOSTING.md`: Linux relay, trusted offline issuer and Windows manual JSON import from start to finish.
- `docs/SESSION_CONFIG.md`, `docs/DIAGNOSTICS.md` and `docs/TROUBLESHOOTING.md` explain strict config, one-time grants, log codes and incident triage.
- Offline example `tools/selfhost_issuer` for generating an Ed25519 issuer and a short-lived, one-time GSK2 JSON session config for self-host testing.

### Changed
- README EN/VI and architecture/provider docs now document **both** `glo://` handoff and manual JSON config import; removed the official API endpoint URL from both README homepages.
- Windows client Facebook link configured, and “How to use” opens **Use the official GLO service** (`/auth/`) with an updated non-clipping label.
- Retained `VERSION=0.0.2` compatibility and unchanged wire/routing behavior; this is a diagnostic/documentation beta revision, not a confirmed fix for the October 8 Roblox route-verification incident.

### Fixed
- Versioned changelog/docs so Windows and self-host operators can distinguish a missing Wintun packet from a local UDP send failure or an unverified reverse flow.
- Production-safe diagnostic overhead: no counter writes while Debug is off or after verification, 2-second grouped `ROUTE016`/snapshot throttle with suppression count, and bounded cross-process debug-log rotation (5 MiB each; two backups). `NET005` remains reserved for Winsock socket-configuration failures. No routing or wire-protocol changes.

## [0.0.2-beta] - 2026-10-04

### Added
- Windows notification-area integration for the GLO client.
- A connected-state tray icon that preserves the GLO icon and adds a small green status dot.
- Tray actions to reopen GLO, disconnect an active session, and quit the client.

### Changed
- Closing the main window now hides the UI while GLO continues running in the notification area.
- Exiting GLO is now an explicit Quit action from the tray menu.
- Quitting while a connection is active now asks for confirmation before ending the session.
- Windows Start Menu and Desktop shortcuts now use the branded GLO icon explicitly.

### Fixed
- Closing the main client window no longer terminates an active connection.

## [0.0.1-beta] - 2026-10-04

### Added
- Initial public beta release line.
- Open-source Windows game-routing client and Linux relay.
- Compatible service/provider handoff documentation.
- Relay operator, security, brand and self-hosting documentation.
- Synchronized GLO web/client/installer branding assets.

### Changed
- Reset public versioning to `0.0.1-beta`.
- Official project website is `https://gloptimizer.com`.
- Official source repository is `https://github.com/sonictype41/Game-Latency-Optimizer`.
- Windows CI fetches the pinned Wintun runtime from upstream and verifies both archive and DLL SHA-256.
- Release artwork is shipped at target sizes rather than full source-image dimensions.
- Public OSS boundary checks reject service-only path names and secret-like material.

### Fixed
- Web release/build version validation and download metadata flow.
- Theme-aware logo contrast on dark and light backgrounds.
- Windows app, taskbar and installer icon consistency.
- Web cache-buster cleanup and release asset naming.
- Fresh-clone Linux CI no longer requires the Windows-only Wintun DLL.
- Canonical root favicon paths for browsers and search crawlers.

### Known limitations
- This is beta software; compatible fixes and packaging changes may still occur before stable.
