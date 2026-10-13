# GLO OSS v0.0.5-beta — 2026-10-13

## Implemented
- VALORANT GameId and non-invasive `VALORANT-Win64-Shipping.exe` process enumeration; existing Roblox code and Wintun datapath retained.
- Windows tray notification API (`Shell_NotifyIconW`, `NIF_INFO`, GLO application icon): one warning per session at <=300s and <=60s. Do Not Disturb / Focus Assist is respected by Windows.
- Relay policy parser accepts several exact non-identical UDP ranges for the same public /32, without widening allowed ports.

## Verified here
- OSS Python tests: 59 run, 2 skipped, no failures.
- Native portable C++ tests: game profile, session warning, session config (including VALORANT) pass.
- Go relay/dataplane tests pass.

## Required Windows runtime acceptance
- Full Windows client/installer compilation was not available in this Linux validation container; compile with MSYS2 toolchain and run Actions.
- Verify notifications with application minimized to tray; verify GLO icon/title, focus assist behavior, threshold deduplication, reconnection reset.
- Verify Roblox unchanged and test VALORANT while Vanguard is active (lobby, matchmaking, practice/match, disconnect, route cleanup).
- Known design limitation: Windows /32 host routes are system-wide; traffic to routed IPs outside allowed UDP port range may be dropped. No WFP/direct-forwarder was introduced.
