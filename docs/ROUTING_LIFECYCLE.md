# Client routing lifecycle: 0.0.3-beta (Windows)

## Scope and trust

- **Generic core** (`client_core.cpp`, `route_events.hpp`): accepts typed session boundary/endpoint hints and WFP/Wintun packet events. It owns generation/cycle transitions, exact route `/32` installation, strict post-forward/reverse verification, fail-open cleanup, and user-visible route state.
- **Game adapter** (`game_session_hints.cpp`, `game_log_hint_watcher.cpp`): optional local, read-only Roblox Player log tailer. The parser recognizes `! Joining game`, `UDMUX Address = <IP>, Port = <port>` and disconnect markers. It extracts only a validated destination IP/UDP port and session boundary; it does **not** store or transmit the full game logs, credentials, device identifiers, or player IDs. It does not read memory, inject code or change Roblox binaries.
- **Policy** (`route_scope.cpp`, `preflight_policy.cpp`): the present release supports only Roblox UDP gameplay. Hints cannot expand beyond the current endpoint and port allowlist. Windows host routes are not process-exclusive: non-game packets to the selected `/32` are discarded by GLO while that route is active. Avoid using hints for general Internet prefixes or services.
- **Platform**: the project continues to use Windows WFP via documented user-mode APIs, the official Wintun component, the existing GLO6 transport and relay. No new system driver or anti-cheat interaction is introduced.

## Lifecycle

1. Relay control handshake and Wintun session initialize, without installing a game route.
2. The existing WFP game-process gate arms. If the supported game adapter observes a fresh session boundary, it may reset a previous Direct/Relay lock even in the **same** PID.
3. If a strict game endpoint hint arrives while waiting, GLO installs a **single** `/32` and releases the gate. Otherwise, the original first-flow WFP event discovers an endpoint, installs that `/32`, and releases the gate. `ROUTE007 source=game_hint|wfp_gate` records which path was used.
4. Wintun forwards only profile-valid UDP traffic; reverse confirmation is required before `ROUTE009` locks Relay. An IP hint or successful route installation **never** counts as gameplay proof.
5. If forward/reverse verification fails, fail-open removes route/filter, locks Direct, and leaves the game running normally. The next **confirmed new match** may reset that state. It must not retry endlessly on the same flow.

## Important limitations

- The October 8 incident exposed that new UDP sockets can follow an updated `/32` while sockets already connected to the LAN may not. **No public API in this client can guarantee migration of an existing external game's UDP socket.**
- Roblox's UDMUX log line may be written only milliseconds before or after network connection initiation. File polling is intentionally bounded (10 ms for size changes and 200 ms for file discovery); it is best effort, not deterministic pre-connect routing. If the hint is late, fallback WFP and Direct mode remain. Treat real Roblox 0.742 relay success as **unverified** until a live Windows test confirms it.
- Supported game log formats may change. The adapter fails closed on unknown hint format and keeps normal Direct fallback. Do not enable broad host-range routes or arbitrary log-driven endpoints.
- A `/32` route is Windows-wide. It is kept only during gameplay verification and confirmed use; unrelated packets to the same host are locally discarded, not tunneled. This is a known limitation of routing through a user-mode virtual adapter.

## Maintenance / testing

- Portable parser: `glo_game_session_hints_test` checks valid and rejected hints without Windows dependencies.
- Reproduce on Windows with Debug enabled; compare `HINT002`, `ROUTE007 source=`, `ROUTE008`, `ROUTE009`, `ROUTE014` and `ROUTE016` plus `WINTUN007/NET006`.
- Native Windows build, actual match joining/teleporting, same-PID recovery, log tailing under live writes, WFP cleanup on errors, and behavior with antivirus/anti-cheat are **not** simulated by Linux unit tests.
- Future games should implement a separate adapter that yields the generic hint contract and policy validation, without adding game-specific log parsing or endpoint constants to the generic routing core.
