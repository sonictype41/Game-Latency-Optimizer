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
| `GATE` | WFP endpoint gate and candidate IP/port | `GATE003`, `GATE005` |
| `ROUTE` | Exact Windows route, relay verification, fail-open | `ROUTE007`, `ROUTE008`, `ROUTE009`, `ROUTE016` |
| `NET` | Local socket delivery of encapsulated gameplay | `NET005` (socket configuration), `NET006` (verification send summary) |
| `QUAL`, `TELEM` | Quality statistics after relay lock | `QUAL001`, `TELEM001` |

## Successful route

`RELAY003 → WINTUN002 → GATE005 → ROUTE007 → ROUTE008 → ROUTE009` means the relay control handshake succeeded, endpoint was detected, the /32 route installed, **at least one** gameplay datagram was sent via the client socket, and a matching reverse flow was verified. Only then is a relay gameplay session marked active. The app showing Connected after control handshake does not mean packets are yet flowing.

## Timeout report (new in 0.0.2-beta-r1)

When `ROUTE016` occurs with Debug already enabled, GLO emits **one bounded report (3 lines) no more than once every 2 seconds**, rather than a line per packet. Additional timeouts are counted as `suppressed=N` in the next emitted `ROUTE016`:

```text
WARN code=ROUTE016 event=fail_open reason=relay_verification_timeout stage=no_wintun_packet epoch=8 verify_ms=1800
INFO code=WINTUN007 event=verification_snapshot epoch=8 rx_total=0 rx_game_host=0 rx_game_udp=0 forwarded=0 send_failed=0 bad_ip=0 other_host=0 non_udp=0 wrong_port=0 stale_epoch=0 malformed_udp=0 oversize=0
INFO code=NET006 event=gameplay_send_snapshot send_ok=0 encode_failed=0 socket_failed=0 last_wsa_error=0
```

Example numbers are illustrative. All are **datagram counts for the currently installed host-route observation window**, not byte counts or evidence of whether the game emitted packets outside Wintun. The per-route snapshot uses lock-free counters; a packet in flight at route turnover may not be included.

`stage` values and what they establish:

- `no_wintun_packet`: no datagrams observed by GLO's Wintun receive loop during the observation window. **Does not prove Roblox sent none.** Check Windows route selection, WFP gate and game UDP retry behavior.
- `no_packet_for_routed_host`: packets reached Wintun but not the selected /32 host. Check route selection and other host traffic.
- `packet_filtered_or_malformed`: selected-host packets were observed, but none passed gameplay UDP validation. Check `non_udp`, `wrong_port`, `bad_ip`, `malformed_udp`.
- `gameplay_send_failed`: at least one valid gameplay UDP datagram hit a local send failure with no successful forward. Check `NET006` `encode_failed`, `socket_failed` and numeric Winsock error.
- `no_forwarded_gameplay`: GLO saw candidate gameplay but had no successful transfer to the relay socket; inspect payload size and rejection counters.
- `awaiting_matching_reply`: at least one datagram was sent to the relay socket, but no valid matching reverse packet arrived before timeout. Investigate relay gameplay flow and reverse injection.

`diagnostic_unavailable` means debug counters were not active for the complete verification window. `ROUTE016` is a timeout classification, **not proof the relay itself has failed**. `WINTUN007` and `NET006` together identify the furthest confirmed stage. `ROUTE008` is only recorded after `send()` reports successful local delivery into a UDP socket; it is **not** confirmation of remote network delivery.

## Safe incident workflow

1. Reproduce once with debug enabled; capture complete log from `RELAY003` through `ROUTE016` (or `ROUTE009`).
2. Match the session ID between relay's `SESS001` and client `RELAY003` when available; compare `flows_opened` and `gameplay_sessions`. Counters may be cumulative across sessions.
3. Do not increase the verify timeout or disable fail-open before determining the missing stage.
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
WFP, Wintun, game latency, frame time, or actual GLO packets.** The file-append
test is single-process and does not measure the Windows inter-process lock.
For an official release, still build and test the native Windows client.
