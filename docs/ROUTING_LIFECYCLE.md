# Routing lifecycle (OSS 0.0.4-beta)

The client accepts a validated, bounded game profile (`profile_id`, revision, list of public IPv4 `/32` destinations, UDP port range) from its provider or local self-host configuration. The default offline Roblox SG profile is bundled for self-hosting.

1. The Windows client verifies the signed session grant and authenticates the relay before routing changes.
2. Wintun starts and installs up to 32 verified exact Windows `/32` routes from the selected profile before gameplay UDP flow creation. The former WFP connect BLOCK gate and Roblox log watcher are removed.
3. The Wintun UDP classifier forwards only destination/port matches using the lightweight plaintext gameplay framing. It does not encrypt the gameplay payload. Relay policy must independently enforce destination and port restrictions.
4. Only forwarded traffic plus matching return traffic confirms relay usage (`ROUTE008`/`ROUTE009`); failures remove the temporary host routes and revert Direct.
5. Disconnect always removes the routes owned by the session.

**Limitations:** Windows `/32` routes are system-wide, not restricted by PID. Traffic from other processes to these destinations can be diverted or disrupted, including TCP; the client does not implement a transparent direct bypass. Do not claim strict process-only/gameplay-only isolation from this routing mode. Live Windows/Roblox 0.742 behavior must be tested before declaring the fix verified. TCP games are not yet supported.
