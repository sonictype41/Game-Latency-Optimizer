# GLO OSS 0.0.5-r3-beta — release validation notes

Date: 2026-10-14. Source ZIP, **not** a precompiled Windows installer.

## Scope

- Windows `RelayRejected temporary` with a nonzero Flow ID is flow-local; only session-level rejection, heartbeat loss, Wintun stop, and verification deadline can cause Direct fallback.
- Wintun local flow tables idle-evict after 60s; 5s sweep, touched by both client-to-server and server-to-client packets, with synchronised removal of mapping and notification indices. Relay's existing egress socket cleanup is not modified.
- Keep wire `VERSION=0.0.5`, the existing session schemas and the upstream OSS Relay implementation.

## Tests executed in the packaging environment (Linux)

- `python -m unittest discover -s tests -p 'test_*.py' -q` — 78 tests, passed (2 skipped).
- `g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I app/core/include -I protocol/include tests/flow_lifecycle_test.cpp -o ... && ...` — passed. Includes a 150-flow synthetic burst with reverse-traffic survival and tuple-index cleanup.
- `go test ./relay/...` — passed for buildpolicy, core, dataplane.
- Full CMake configuration could **not** complete here: libsodium development headers/library symlink are not installed (runtime .so only). This is a local environment limitation, not proof of a successful Windows build.
- Production changelog package regression suite (`Production/Tests`) — 37 tests, passed.

## Required Windows validation before distributing

- Run the GitHub Actions Windows/MSYS2 build and installer workflow. Confirm bundled `wintun.dll`, NSIS discovery, native GUI launch, and no C++ compiler errors.
- Live VALORANT: create >128 flows across several matches and check one `RELAY007 temporary` causes `FLOW010 action=keep_tunnel`, **not** `ROUTE026`; ensure relay stays healthy.
- Live Roblox: keep UDP traffic active for >=60s (including reverse-only activity) and ensure no false eviction; test route changes and Disconnect.
- Simulate real relay heartbeat timeout and unexpected Wintun stop; verify safe Direct fail-open remains.

The included tests are not a claim that these live Windows/network tests have been completed.
