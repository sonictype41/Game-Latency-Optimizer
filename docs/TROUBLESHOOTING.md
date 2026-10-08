# Troubleshooting GLO (Official and self-host)

This guide distinguishes **control plane reachability**, **Windows routing**, and **gameplay dataplane**. A successful login or handshake does not guarantee relay gameplay is active.

| Symptom | Verify | Next action |
|---|---|---|
| URI rejected | `URI001`/`URI004`/`URI010`; provider HTTPS | Create a fresh handoff token; check system clock and provider TLS/network |
| Manual JSON rejected | Client config parser; [SESSION_CONFIG.md](SESSION_CONFIG.md) | Check schema, supported game, relay identity and 152-byte GSK2 ticket; do not reuse grants |
| Relay handshake fails | `RELAY003` missing; server `SESS001` absent | Check UDP port/firewall, relay key, issuer public key, grant expiry and journal |
| Roblox IP detected but route verification times out | `GATE005`/`ROUTE007` present; `ROUTE008` missing | Read `WINTUN007` and `NET006` to locate first unconfirmed stage |
| `ROUTE008` exists but no `ROUTE009` | Data sender succeeded, reverse not verified | Compare relay `SESS005`, flow counters, reply path and timing |
| UI says waiting for gameplay | Control connected but no `RelayLocked` flow | Inspect above logs; do not equate the UI string with Roblox inactivity |
| Relay `gameplay_sessions=0` | `control_sessions` nonzero but `flows_opened` not advancing | Determine whether `ROUTE008` ever occurs; inspect client path before blaming game server |
| Latency or loss after route locks | `QUAL001`, `DATA001`, `LIMIT001` | Check relay egress shaping/drop counters and upstream conditions; avoid masking losses with longer verification timeout |
| Different source/IP per provider | Manual config imports | Treat unknown/self-host issuers as untrusted; only import intentional configs |

**Incident pattern observed 2026-10-08:** some clients detected Roblox endpoints and installed exact routes, yet timed out before `ROUTE008`. The cause is under investigation. Do not state that a Roblox protocol or terms-of-service change has been confirmed. The new counters assist diagnosis; they do not change the route state machine or guarantee a fix.

Minimal useful report: client release version, Windows version, a complete sanitized 10–20 second debug window, the selected relay's `SESS/ENG/DATA` lines, and whether other relays reproduce. Never submit bearer grants or private key files.
