# GLO architecture

GLO OSS is the generic client/relay protocol implementation. The official service is outside this repository.

```text
compatible service / provider
          |
          | glo:// short-lived handoff
          v
    generic Windows client
          |
          | inspect candidate relays
          | client-side relay RTT probes
          | redeem with measured RTT/null values
          v
 service/provider returns strict session config
          |
          v
 generic Windows client <==== secure GLO control ====> generic Linux relay
          |                                                |
          +---- selected gameplay traffic -----------------+
```

## Service boundary

The OSS client receives only data defined by the public handoff/session contract: candidate relay endpoint, relay public identity and session material. It measures candidate reachability/RTT locally. Service-only state and decisions are outside the OSS contract.

A compatible service can implement the handoff contract documented in [`HANDOFF_PROVIDER.md`](HANDOFF_PROVIDER.md).

## Session-config boundary

The final session config is strict data, not executable policy. It carries only relay/session fields accepted by the client. Service credentials, private keys, local executable paths and undeclared fields are invalid config inputs.

## Admission and session time

Signed short-lived grants authorize relay admission. The relay verifies admission material before allocating the transport session and rejects replay according to its redeemed-ticket journal. Remaining session time is delivered inside the authenticated/encrypted control session rather than trusted from editable plaintext config.

## Client boundary

The Windows app contains game detection, WFP/Wintun routing, secure transport, relay probing, app-link handling and local connection state. It does not contain service-only state.

## Relay boundary

The relay owns transport admission, session/flow capacity, target policy, shaping, expiry and gameplay forwarding. It trusts configured issuer public keys and does not require service credentials.
