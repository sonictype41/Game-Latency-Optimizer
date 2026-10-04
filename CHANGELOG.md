# Changelog

All notable public changes to GLO OSS are documented here.

The public release history starts at `v0.0.1-beta`.

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
