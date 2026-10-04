# Windows installer — GLO v0.0.2-beta

The per-user NSIS installer defaults to `%LOCALAPPDATA%\\GLO`. Setup and uninstall run without elevation. The normal GLO UI remains `asInvoker`; UAC is requested only when the client launches the privileged network worker required for a connection.

## Installer UX

- English and Tiếng Việt are built in.
- The welcome page shows `DISPLAY_VERSION`, the game-routing description and configured official/project links.
- The exact official website is configured in `installer/BRANDING.json`.
- The MIT license is shown before installation; third-party notices and bundled dependency licenses are installed with the client.
- Start Menu shortcuts are created; the Desktop shortcut remains optional.
- The setup/uninstaller icon is `installer/resources/glo-installer.ico`, generated from the current GLO controller-G brand mark at installer-appropriate icon sizes rather than embedding a full-size source image.

Version roles:

- `VERSION` = protocol/client compatibility version (`0.0.1`)
- `RELEASE` = public release version (`0.0.2-beta`)
- `DISPLAY_VERSION` = user-facing release label (`0.0.2-beta`)

## Brand boundary

The installer may use official GLO branding because it is part of the official source tree. Modified/independent distributions should replace the primary product name/logo and clearly identify their operator rather than presenting themselves as official GLO. See [`../BRAND_POLICY.md`](../BRAND_POLICY.md).

## Build

Build the app first, then run `tools/build_installer.py` or the provided wrappers. No installer script downloads NSIS or runtime dependencies.

The installer builder cleans its staging tree and stale outputs before packaging. On MSYS2/Git Bash it generates `build/installer/GLO.generated.nsi` with source-level `!define` values and invokes native `makensis.exe` without MSYS path-rewriting hazards.

The staged payload includes Wintun provenance metadata so the pinned archive/DLL hashes and upstream information travel with the binary.
