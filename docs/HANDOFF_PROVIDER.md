# Compatible provider handoff

This document describes the provider-facing boundary used by the GLO desktop client. It is intentionally provider-neutral: an independent provider can implement the same public contract.

## 1. App link

A provider opens the installed client with a short-lived `glo://` app link containing a provider endpoint and opaque token. The token is a bearer secret: keep it short-lived, do not place it in logs, and require HTTPS for provider HTTP endpoints.

## 2. Inspect

The client sends the opaque token to the provider's `/app/handoff/inspect` endpoint. A successful response identifies the provider/game and returns a bounded list of candidate relays. Each candidate needs an opaque candidate ID, reachable relay endpoint and relay public identity required by the client transport.

The client needs no service-only state beyond the documented response.

## 3. Client probes

The client probes the candidate relay endpoints from the user's machine. It reports one measurement per candidate. A reachable candidate has an RTT value; an unreachable candidate is reported with a null/unavailable result rather than silently removed.

Do not trust open-client RTT values as an authentication or authorization signal. They are routing-quality input only.

## 4. Redeem

The client submits the original token, a client nonce and the complete candidate-measurement set to `/app/handoff/redeem`.

The provider validates the request, issues relay admission material, and returns the strict session-config envelope expected by the client.

The provider should make retries idempotent for the same handoff/client nonce once a ticket has been issued.

## 5. Failure rules

- Unknown/expired token: reject without exposing internal policy details.
- Missing candidate measurement: reject rather than allowing a modified client to shrink the provider's candidate set silently.
- Candidate unreachable: accept the documented null/unavailable measurement and handle it according to service policy.
- Re-validate request state at redeem time.
- Never return service credentials, issuer private keys or undeclared service-only state in the session config.

## 6. Independent branding

A compatible implementation is not automatically an official GLO service. Identify the actual operator and follow [`../BRAND_POLICY.md`](../BRAND_POLICY.md).
