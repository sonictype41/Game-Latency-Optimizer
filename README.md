# GLO — Game Latency Optimizer

<p align="center"><img src="docs/assets/glo-wordmark-dark-640.png#gh-light-mode-only" width="560" alt="GLO Game Latency Optimizer"><img src="docs/assets/glo-wordmark-light-640.png#gh-dark-mode-only" width="560" alt="GLO Game Latency Optimizer"></p>

GLO (Game Latency Optimizer) OSS contains the generic Windows game-routing client, Linux relay, secure transport, session-admission code, protocol definitions, build tooling and documentation used by the GLO ecosystem. The official GLO service uses these open client/relay components; all service-only functionality remains outside this OSS tree.

The desktop client can connect through the official GLO service or a compatible third-party/self-hosted provider. The client probes only the relay candidates it is given and reports only measurements it can observe locally.

## Official project and brand

- Website: <https://gloptimizer.com>
- Official service endpoint: `https://api.gloptimizer.com`
- Repository: <https://github.com/sonictype41/Game-Latency-Optimizer>

The source code is MIT licensed. The GLO name/logo and claims of official affiliation are governed separately by [`BRAND_POLICY.md`](BRAND_POLICY.md). Open source does not mean a fork may impersonate the official project or service.

## Layout

- `app/` — Windows desktop client and game-routing core.
- `relay/` — Linux relay dataplane and reusable relay core.
- `session_key/` — signed one-time bearer grants.
- `secure_transport/`, `protocol/` — authenticated control transport and wire formats.
- `game_profiles/` — supported game-routing profiles.
- `installer/` — per-user NSIS installer source and installer branding.
- `tools/` — offline build/package helpers.
- `docs/` — architecture, provider handoff and relay-operator documentation.
- `vendor/`, `third_party/` — pinned local build inputs required for offline builds.

## Provider handoff boundary

The official/native app flow is provider-driven:

```text
service / provider
    |
    | glo:// short-lived handoff
    v
GLO desktop client
    |
    | POST /app/handoff/inspect
    | <- candidate relay endpoints + relay identity keys
    |
    | probe candidates from the client
    |
    | POST /app/handoff/redeem with measurements
    v
service/provider returns a session config
    |
    v
secure GLO client <============================> GLO relay
```

The final portable session config is strict and data-only. It contains only relay/session material required by the client and never contains service credentials, private keys or local executable paths. See [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Build

Required toolchains must already be installed. Pinned source dependencies are shipped locally; the signed Wintun Windows runtime is fetched from its official upstream URL when absent and is accepted only after the archive and DLL SHA-256 values in `third_party/wintun/INFO.json` match. Set `GLO_OFFLINE=1` to require an already-present verified runtime.

On MSYS2 MINGW64, optional compiler caching must use the native MinGW package for the active environment:

```sh
pacman -S "$MINGW_PACKAGE_PREFIX-ccache"
```

Then:

```sh
# Optional explicit fetch; build_app.sh also does this automatically when needed.
python ./tools/fetch_wintun.py

bash ./tools/build_relay.sh all
bash ./tools/build_app.sh
bash ./tools/build_installer.sh   # requires a locally installed NSIS/makensis
bash ./tools/build_all.sh
```

Build caches live under `${GLO_BUILD_CACHE_DIR:-$TMPDIR/glo-build-cache}` and are never packaged into a release. Final artifacts are written under `bin/` unless an explicit output path is supplied.

`VERSION` records protocol/client compatibility; `RELEASE` identifies the next public release snapshot. GitHub automation validates it, creates the matching tag, and publishes the release. Public version history belongs in [`CHANGELOG.md`](CHANGELOG.md) and GitHub Releases, not in this README.

## Self-hosting

Start with:

- [`docs/RELAY_OPERATOR.md`](docs/RELAY_OPERATOR.md) for relay operation.
- [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) for a compatible provider/handoff service.
- [`SECURITY.md`](SECURITY.md) for security boundaries and reporting.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for bundled dependency notices.

MIT licensed GLO source is covered by `LICENSE`; bundled third-party components retain their own licenses.
