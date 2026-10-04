# GLO6 secure control protocol

GLO6 remains the authenticated/encrypted session and control transport in v0.12.2. Gameplay DATA no longer uses GLO6; see `PROTOCOL.md` for GLOD1.

This is a project-specific handshake using X25519, HKDF-SHA256 and ChaCha20-Poly1305. It is not WireGuard or a standardized Noise implementation and has not received an independent cryptographic audit.

## Handshake

The GLH6 HELLO/RETRY/AUTH_HELLO/WELCOME exchange retains the existing cookie, relay-pin and signed-ticket behavior. A successful WELCOME derives independent C2S/S2C GLO6 traffic keys.

The client then sends encrypted inner `FINISH=16`; the relay validates the existing control session, marks both secure transport and `DataPlaneCore` active, and replies with encrypted `FINISH_ACK=17`. GLOD gameplay is rejected before this activation point.

## Encrypted control records

```text
GLO6:4 | zero:4 | session_id:8 | crypto_counter:8 | ciphertext:N | tag:16
```

The complete 24-byte outer header is AEAD associated data. The nonce is four zero bytes followed by the per-direction 64-bit crypto counter. Authentication precedes replay-window updates and control side effects.

The plaintext inside GLO6 is a complete 32-byte GLO2 **control** record plus its control payload. Naked GLO2 network datagrams are rejected. GLO2 packet IDs 12/13 are reserved in v0.12.2 and cannot be used to smuggle gameplay DATA back through the encrypted path.

GLO6 carries low-rate control messages such as FINISH/ACK, PING/PONG, BYE, ERROR, FLOW_CLOSE and STATS request/response. Its 4096-packet replay window and endpoint binding remain unchanged.

## Gameplay separation

After FINISH activation, gameplay uses plaintext GLOD1:

```text
client/game -> GLOD1 plaintext gameplay -> relay -> native game UDP
```

GLO does not encrypt, decrypt, compress or inspect GLOD gameplay payload bytes. Userspace still checks active session state, TTL, exact peer endpoint, rate limits, flow ownership and destination policy before forwarding. A service deployment may add additional infrastructure-side protections outside this repository; those protections are not part of the OSS protocol and are never an authentication authority.


### v0.12.2 data-wire marker
`FINISH` and `FINISH_ACK` require flag `0x0001` (GLOD1 capability flag). This intentionally rejects v0.11.4 peers at session activation instead of attempting a legacy encrypted-DATA fallback.
