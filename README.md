# GLO — Game Latency Optimizer

<p align="center"><img src="docs/assets/glo-wordmark-dark-640.png#gh-light-mode-only" width="560" alt="GLO Game Latency Optimizer"><img src="docs/assets/glo-wordmark-light-640.png#gh-dark-mode-only" width="560" alt="GLO Game Latency Optimizer"></p>

GLO (Game Latency Optimizer) OSS contains the generic Windows game-routing client, Linux relay, secure transport, session-admission code, protocol definitions, build tooling and documentation used by the GLO ecosystem. The official GLO service uses these open client/relay components; all service-only functionality remains outside this OSS tree.

The desktop client can connect through the official GLO service or a compatible third-party/self-hosted provider. The client probes only the relay candidates it is given and reports only measurements it can observe locally.

## Official project and brand

- Website: <https://gloptimizer.com>
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

## Two supported ways to connect

GLO supports **provider handoff** and **manual session config**. These are two inputs to the same strict client-side validator and secure transport—not two routing protocols. The official network uses provider handoff. Manual config is intended for independent/self-hosted operators.

```text
A. Official / compatible provider              B. Self-host / independent issuer
   web service                                    signed config JSON
       |                                                |
       | short-lived glo:// app link                     | Paste JSON / Import file / --config
       v                                                v
   GLO desktop client                              GLO desktop client
       |                                                |
       | HTTPS inspect -> relay candidates               |
       | client probes -> RTT measurements               |
       | HTTPS redeem -> session config                  |
       +---------------------+--------------------------+
                             |
                    strict session config
                             |
                  secure GLO client <=====> Linux relay
                             |
                  supported gameplay UDP only
                             |
                         game server
```

**URI handoff:** inspect returns candidates and relay public identity; the client probes the candidates, redeems the short-lived token and receives signed session material. The client does not expose service credentials or private keys.

**Manual JSON:** a self-host issuer creates a compatible, signed, one-time session config. A user can paste or import it directly; no inspect/redeem HTTP API is required. This is **not** a raw IP-only mode: the client still validates the 152-byte GSK2 admission grant and relay identity. The sample JSON in `tools/` is deliberately **not connectable**.

Read the step-by-step [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md), [`docs/SESSION_CONFIG.md`](docs/SESSION_CONFIG.md), [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md), and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

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

**New:** Follow [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) from a blank Linux machine to a signed config and a Windows client. The optional offline issuer example under `tools/selfhost_issuer/` does not implement provider HTTP APIs. For packet-verification failures see [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md) and [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

Start with:

- [`docs/RELAY_OPERATOR.md`](docs/RELAY_OPERATOR.md) for relay operation.
- [`docs/HANDOFF_PROVIDER.md`](docs/HANDOFF_PROVIDER.md) for a compatible provider/handoff service.
- [`SECURITY.md`](SECURITY.md) for security boundaries and reporting.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for bundled dependency notices.

MIT licensed GLO source is covered by `LICENSE`; bundled third-party components retain their own licenses.

## Routing (OSS 0.0.4-beta)

