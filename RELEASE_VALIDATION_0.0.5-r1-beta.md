# GLO OSS v0.0.5-r1-beta — 2026-10-13

## Changes
- Global first-flow verify grace: 1,800 → 3,000 ms, immediate success on valid reply, Direct fail-open preserved.
- Deadline anchored to the first forwarded flow, not restarted by subsequent new flows.
- Bounded reverse-first evidence by exact route epoch and 5-tuple; added client and Linux relay debug-only stage diagnostics.
- `VERSION=0.0.5` wire/build compatibility retained; `RELEASE=DISPLAY_VERSION=0.0.5-r1-beta`.
- Production v0.0.5-r6 source remains unchanged except for the bilingual public OSS changelog entry (packaged separately).

## Runtime verification required
- Native Windows build in MSYS2/CI (Linux validation cannot exercise Wintun/Vanguard).
- With Debug on, compare `ROUTE008`, `VERIFY005`/`VERIFY006` and server `FLOW006`/`FLOW004`, and inspect selected relay endpoint.
- Regression-test reconnect, simultaneous flows, early/out-of-order S2C, stale route epochs, session shutdown and Direct fail-open.
- Game server ping may not improve, and failed verification can stall an extra 1.2 seconds.
