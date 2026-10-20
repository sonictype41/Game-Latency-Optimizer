# Changelog — public OSS releases only

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
