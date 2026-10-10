# Strict session config (manual/import and provider redemption)

GLO has **two entry points into one secure session**: (1) a provider `glo://` handoff which returns a config at redemption, and (2) user-supplied JSON via paste/file import/`--config`. A provider HTTP implementation is optional for manual self-hosting.

## Exact JSON fields

| Field | Required | Meaning |
|---|---|---|
| `schema` | Yes | Integer `2` (strict; no legacy schema) |
| `relay` | Yes | Reachable relay host and UDP port (`host:port`) |
| `relay_public_key` | Yes | Relay identity X25519 public key as **64 hex characters** |
| `game` | Yes | Supported game profile key (initially `roblox`) |
| `grant` | Yes | One-time signed GSK2 session ticket as **152 bytes / 304 hex characters** |
| `relay_name` | No | Operator-supplied display name, maximum 96 chars |
| `timeout_message` | No | Text shown for session expiry, maximum 512 chars |
| `profile_id` | Yes | Provider or bundled game-profile identifier |
| `profile_revision` | Yes | Positive profile revision |
| `gameplay_ipv4` | Yes | CSV of 1–32 exact public IPv4 `/32` destinations |
| `port_min` / `port_max` | Yes | Allowed gameplay destination UDP port range |

Parser rejects duplicate keys, unknown keys, malformed types, missing fields and invalid grant length/encoding. See `app/core/src/session_config.cpp` for authoritative validation; do not silently add keys to configs or assume a web-session token is a valid grant.

The sample [`../tools/Session_Config.example.json`](../tools/Session_Config.example.json) is **structural only**. All-zero relay key/grant are placeholders and are not valid admission material. To produce a real config, follow [SELF_HOSTING.md](SELF_HOSTING.md) using the offline issuer CLI.

## One-time signed grant

The Ed25519 issuer signs a ticket containing its issuer key ID, relay identity hash, unique ticket ID, issue and redemption deadlines, and the session lifetime. The relay must trust that issuer **public** key. A ticket can be redeemed only once, within its short redemption window. The issuer **private** key must remain private to the operator, not the relay or Windows client.

A manual JSON config is therefore **not** an authentication bypass. Importing from an untrusted operator lets that operator choose the relay and session parameters; verify operator and relay identity, and never execute embedded instructions (config is data-only). For URI mode, see [HANDOFF_PROVIDER.md](HANDOFF_PROVIDER.md).

## Privacy / sharing

A `grant` is a bearer credential until consumed. Do not post session configs, handoff links, secret files or raw packets in issue reports. Redact identifiers in logs before sharing. Expired/redeemed grants need to be freshly issued; changing the timestamps in JSON does not renew a signed ticket.
