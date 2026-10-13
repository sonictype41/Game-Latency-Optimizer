# Diagnostics and event codes (Windows client)

Turn on **Debug logging** from GLO Settings. The network worker and frontend append structured timestamped records to `%LOCALAPPDATA%\GLO\logs\dbg_log.txt`. The logger retains at most **5 MiB per file**, with two backups (`dbg_log.txt.1`, `.2`), for up to **15 MiB** in the current log family. File rollover is coordinated between the frontend and elevated worker; if a log file is locked by an external program, debug records may be skipped instead of growing without limit. An oversized log left by an older release is compacted to its newest ~5 MiB on the next successful append; archive any old logs you want to preserve before upgrading. The CLI / server and application have independent logs.

Diagnostic counters are collected **only during relay verification while Debug logging is enabled**. When Debug is off or gameplay is already relay-locked, the extra atomic counters are not updated. Toggle Debug on *before* reproducing a failure; enabling it halfway through a verification cannot reconstruct earlier packets. No UDP payloads, tokens or grants are recorded.

## Event families

| Family | Meaning | Examples |
|---|---|---|
| `URI` | Provider URI parsing, relay candidate probes, redemption | `URI010`, `URI020` |
| `RELAY` | Control transport handshake/heartbeat | `RELAY003`, `RELAY004` |
| `WINTUN` | Adapter/session and bounded receive-path counters | `WINTUN002`, `WINTUN007` |
| `PROC` | Game process detection/lifecycle | `PROC002` |
| `ROUTE` | Exact Windows route, relay verification, fail-open | `ROUTE007`, `ROUTE008`, `ROUTE009`, `ROUTE016` |
| `VERIFY` | First-flow timing and reverse-flow proof | `VERIFY001`, `VERIFY005`, `VERIFY006` |
| `NET` | Local socket delivery of encapsulated gameplay | `NET005` (socket configuration), `NET006` (verification send summary) |
| `QUAL`, `TELEM` | Quality statistics after relay lock | `QUAL001`, `TELEM001` |

## OSS 0.0.4-beta: profile-based early routes

Verified profile IPv4 /32 entries are installed before game UDP socket creation; no WFP endpoint gate or Roblox log watcher is used. The global Windows /32 route may affect other traffic to the same IP. See [ROUTING_LIFECYCLE.md](ROUTING_LIFECYCLE.md).

## Successful route

`RELAY003 → WINTUN002 → ROUTE007 → ROUTE008 → ROUTE009` means the relay control handshake succeeded, profile /32 routes were installed, **at least one** gameplay datagram was sent via the client socket, and a matching reverse flow was verified. Only then is a relay gameplay session marked active. The app showing Connected after control handshake does not mean packets are yet flowing.

## OSS 0.0.5-r1-beta: first-flow verification diagnostics

The default global verification deadline is **3,000 ms** for every supported game. This is an upper bound: a valid matched S2C packet immediately confirms Relay. An unsuccessful attempt still fails open to Direct. Additional flows do **not** restart the deadline.

Enable Debug **before** connecting. The following records are emitted once per stage or terminal outcome, not once per packet:

- `VERIFY000`: actual relay endpoint and profile used by the client (no session grant or bearer token).
- `VERIFY001`: start of the first successfully forwarded flow, exact IPv4/UDP tuple, route epoch and timeout.
- `VERIFY003`: first GLOD S2C packet of the session (may be from a flow that cannot be injected).
- `VERIFY004`: first S2C packet successfully injected into Wintun.
- `VERIFY005`: verified flow, event ordering and aggregate counters.
- `VERIFY006`: verification timeout or interruption and aggregate counters, including Winsock failure and Wintun RX/UDP filter data.
- `FLOW006` (relay `--debug`): first UDP datagram successfully written to the game endpoint, its target and outbound socket address. Existing relay `FLOW004` records the first upstream reply and priority queueing when debug is enabled.

Example (illustrative, not a real capture):

```text
INFO code=VERIFY001 event=begin game=VALORANT flow_id=1 route_epoch=4 destination=151.106.248.1:7077 local_port=60288 timeout_ms=3000
WARN code=VERIFY006 event=verification_timeout flow_id=1 elapsed_ms=3000 timeout_ms=3000 forwarded_flows=1 c2s_ok=88 encode_fail=0 socket_fail=0 last_wsa=0 s2c_rx=0 inject_ok=0 inject_fail=0 wintun_rx=90 wintun_game_udp=88 wintun_forwarded=88 wintun_wrong_port=2 wintun_send_failed=0
WARN code=ROUTE016 event=fail_open reason=relay_verification_timeout
```

`ROUTE008` / `c2s_ok` prove successful client **local socket writes**, not delivery to the relay or the game. `FLOW006` proves a successful relay UDP socket write, not acceptance by Riot. `FLOW004 first_game_rx` proves that the relay did receive a response from the game's socket. `VERIFY004` proves successful local Wintun injection; `VERIFY005` proves that the routing worker matched the correct flow/route epoch. A session can have a slow first response without the gameplay RTT itself being slow. A relay that never returns gameplay still fails open after the deadline.

Only counter summaries are recorded, not raw game traffic. The `VERIFY000` endpoint and `VERIFY001` IP/ports are potentially identifying diagnostics; redact these before sharing externally. Cross-machine correlation may use the debug-only session/flow IDs. There is no wire-protocol or WFP change.

## Safe incident workflow

1. Reproduce once with debug enabled; capture complete log from `RELAY003` through `ROUTE016` (or `ROUTE009`).
2. Match the session ID between relay's `SESS001` and client `RELAY003` when available; compare `flows_opened` and `gameplay_sessions`. Counters may be cumulative across sessions.
3. Compare the first-flow counters and relay `FLOW004`/`FLOW006` events; do not disable fail-open.
4. Remove/obscure bearer tokens, session configs, public IP addresses and other personal identifiers when publishing logs. Never collect raw payloads by default.

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## Reproducing the microbenchmark

From the repository root on a machine with a C++20 compiler:

```bash
g++ -std=c++20 -O2 -Iapp/core/include tools/benchmark_diagnostics.cpp -o glo_diag_bench
./glo_diag_bench
```

The benchmark compares 25 million synthetic atomic counter observations in the
old always-count path and the new Debug-off/verification-only paths. It also
compares bounded and unbounded file append costs. **It does not measure Windows
test is single-process and does not measure the Windows inter-process lock.
For an official release, still build and test the native Windows client.
