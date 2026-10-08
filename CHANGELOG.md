# Changelog

All notable public changes to GLO OSS are documented here.

The public release history starts at `v0.0.1-beta`.

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
