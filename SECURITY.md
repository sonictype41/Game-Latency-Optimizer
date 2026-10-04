# GLO v0.0.1-beta security notes

## Reporting

Do not publish active session grants, credentials, private keys, personal data or exploitable security details in a public issue. Use the support/security contact published at <https://gloptimizer.com> for a private report when available.

## Security boundary

- Relay identity is pinned by the public identity supplied through the provider/session contract.
- Session admission uses short-lived signed bearer material; issuer private keys do not belong in the client installation.
- Redeemed-ticket/session journals are used to reject replay where the relay configuration enables them.
- The desktop client accepts strict provider/session data and rejects unknown or unsafe fields rather than accepting local executable paths or provider credentials.
- Provider handoff tokens are secrets and should be short-lived, HTTPS-protected and omitted from logs.
- Candidate relay measurements reported by an open-source client are untrusted input for security decisions. A compatible service must validate all security-sensitive state independently.
- The client-to-relay secure transport protects GLO control/session traffic. Protection from relay to the game server depends on the game's own protocol.
- Local relay file arguments such as relay private keys, issuer public-key files, journals and allowlists are operator configuration and never client session-config fields.

## Service boundary

The official service is outside this OSS tree. A third-party provider is responsible for its own service implementation, security, logs and retention policy.

## Cryptography status

GLO uses standard cryptographic primitives through bundled/pinned libraries, but the project-specific protocol composition has not been represented as independently audited cryptographic software. Review the protocol and threat model before relying on it for a security-sensitive deployment.

## Branding is not a security signal

A logo or project name does not prove a service is official or trustworthy. Check the operator and origin. See [`BRAND_POLICY.md`](BRAND_POLICY.md).
