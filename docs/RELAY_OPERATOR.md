# Relay operator quickstart

The OSS relay is a generic Linux dataplane. The official service is outside this repository.

## Build

```sh
./tools/build_relay.sh all
```

Artifacts are written under `bin/server/`.

## Keys and issuer trust

A relay needs its relay identity key and one or more trusted issuer public keys according to the relay's command-line contract. Private issuer keys belong to the provider/issuer and must not be copied to relay clients or session configs.

Run the binary with `--help` on the target release to see the authoritative flag set. Before starting a deployed instance, use the relay's config/check mode where provided to fail early on bad paths or values.

Typical operator concerns include:

- UDP listen address/port and provider firewall rules;
- relay private-key permissions;
- trusted issuer public-key files;
- redeemed-ticket/session journals;
- session/flow capacity and shaping limits;
- allowlists/target policy;
- maintenance and restart behavior;
- log rotation and telemetry retention.

## Network exposure

Only expose the relay ports required by the protocol. Keep management interfaces separate from public gameplay ingress. Any service-side operation is deployment-specific and is not supplied by this OSS relay repository.

## Compatibility

Keep the relay/client protocol compatibility version aligned with `VERSION`. `RELEASE` may advance for packaging/documentation fixes without necessarily changing the protocol compatibility version.

## Branding

An independently operated relay is not the official GLO service merely because it runs GLO code. Clearly identify your operator/service and follow [`../BRAND_POLICY.md`](../BRAND_POLICY.md).
