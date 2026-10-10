# GLO architecture

GLO OSS is the generic client/relay protocol implementation. The official service is outside this repository.

```text
Provider web / compatible service           Independent self-host issuer
             |                                        |
       glo:// handoff                       signed JSON session config
             |                                        |
         Windows client  <----------------------------+
             | inspect / probe / redeem (URI only)
             v
    strict session config validation
             |
        secure GLO client <==== transport/admission ====> generic Linux relay
             |                                                |
             +------------- supported gameplay UDP ------------+
```


## Service boundary

The OSS client receives only data defined by the public handoff/session contract: candidate relay endpoint, relay public identity and session material. It measures candidate reachability/RTT locally. Service-only state and decisions are outside the OSS contract.

A compatible service can implement the handoff contract documented in [`HANDOFF_PROVIDER.md`](HANDOFF_PROVIDER.md).

## Session-config boundary

The final session config is strict data, not executable policy. It carries only relay/session fields accepted by the client. Service credentials, private keys, local executable paths and undeclared fields are invalid config inputs.

## Admission and session time

Signed short-lived grants authorize relay admission. The relay verifies admission material before allocating the transport session and rejects replay according to its redeemed-ticket journal. Remaining session time is delivered inside the authenticated/encrypted control session rather than trusted from editable plaintext config.

## Client boundary


## Relay boundary

The relay owns transport admission, session/flow capacity, target policy, shaping, expiry and gameplay forwarding. It trusts configured issuer public keys and does not require service credentials.

## Client routing lifecycle (0.0.4-beta)



See [ROUTING_LIFECYCLE.md](ROUTING_LIFECYCLE.md) for limitations, tests and extension points.
