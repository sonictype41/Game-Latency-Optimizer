# Self-host GLO — Linux relay to Windows client

This guide is for an **independently operated** relay, not for joining the GLO Official Network. You control the relay, your own issuer signing key and the session configs you give to trusted clients. The official service's account, advertisement, grant and coordination code is intentionally **not** included.

## What you need

- An Ubuntu 24.04 (or compatible x86-64 Linux) VPS with a reachable public UDP port, a firewall you can configure and a monotonic clock synchronized via NTP.
- Go >= the version declared in `go.mod` (1.23), or the toolchains documented in the build scripts. Run on a trusted machine; do not expose issuer secrets to the Internet.
- A Windows 10/11 x64 client with administrator permission for Wintun, and the signed Wintun runtime packaged through the project's build instructions.
- Know that GLO **only** routes supported gameplay: self-hosting does not turn it into a general VPN, proxy or IP-hiding product.

## Step 1 — build the relay on Linux

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates git golang-go
# Clone from the official public repository, or unpack a verified release source.
git clone https://github.com/sonictype41/Game-Latency-Optimizer.git
cd Game-Latency-Optimizer
bash tools/build_relay.sh all
```

If distro `golang-go` is older than `go.mod`, install a supported Go toolchain from the official Go distribution instead. The build uses pinned vendored dependencies. `bin/server/` contains the relay executable. Build/test artifacts must not be confused with keys or signed grants.

```bash
find bin/server -maxdepth 1 -type f -executable -print
# Below, replace <relay-bin> with the executable listed above.
./bin/server/<relay-bin> --help
```

## Step 2 — generate the relay identity

```bash
mkdir -p ~/glo-relay && chmod 700 ~/glo-relay
cd ~/glo-relay
/path/to/relay-binary --keygen relay.key
chmod 600 relay.key
```

The `--keygen` command prints a **64-character hex X25519 public key**. Save it as `RELAY_PUBLIC_HEX`, without spaces. `relay.key` is the relay's private transport identity and must stay on the relay. **Never use the relay key as the Ed25519 issuer key.**

## Step 3 — create a trusted issuer (offline)

On a separate trusted Linux workstation (recommended), from the GLO source directory:

```bash
umask 077
go run -mod=vendor ./tools/selfhost_issuer gen-issuer -key issuer.key -public issuer.pub
```

This produces a private `issuer.key` (mode 0600) and a public `issuer.pub` containing a single hex-encoded Ed25519 public key. Copy **only `issuer.pub`** to `~/glo-relay/issuer.pub` on your VPS (e.g. via `scp`). Keep `issuer.key` offline. Issuer public-key files can contain multiple trusted public keys, one per line.

## Step 4 — listen on the relay and open the firewall

```bash
cd ~/glo-relay
/path/to/relay-binary \
  --listen :43170 \
  --key-file ./relay.key \
  --issuer-key-file ./issuer.pub \
  --redeemed-ticket-journal ./redeemed-tickets.log \
  --check-config

/path/to/relay-binary \
  --listen :43170 \
  --key-file ./relay.key \
  --issuer-key-file ./issuer.pub \
  --redeemed-ticket-journal ./redeemed-tickets.log
```

Allow inbound UDP/43170 (or your chosen port) in **both provider firewall and host firewall**. `--check-config` only checks arguments; it does not verify the key files or send packets. Run your service under a dedicated unprivileged user with protected key/journal paths; use a systemd unit and restart policy for unattended operation. The issuer's private key must never reside on the relay. The relay's default destination allowlist limits where gameplay may be forwarded; do not remove target restrictions for convenience.

## Step 5 — issue a one-time manual session config

On the trusted issuer machine, while still in the source tree and with `issuer.key` available:

```bash
umask 077
go run -mod=vendor ./tools/selfhost_issuer issue \
  -issuer-key ./issuer.key \
  -relay 'YOUR_PUBLIC_RELAY_IP:43170' \
  -relay-pub 'RELAY_PUBLIC_HEX' \
  -game roblox \
  -session-minutes 30 \
  -redeem-seconds 120 \
  -out ./my-session.json
```

Replace the uppercase placeholders with real values. `my-session.json` contains an **unredeemed one-time bearer grant**. Keep it private; give it only to the intended client shortly before use. Admission expires after `redeem-seconds` even if the intended session lifetime is longer. Issuing a new config requires the signing key, but not an official GLO account/API.

## Step 6 — connect from Windows

1. Install/start the OSS Windows client. Use **Paste JSON config** or **Import JSON config from file** for your independent provider. Alternatively, when using the supported CLI, pass `--config` with the path to the JSON file.
2. Confirm the relay endpoint, operator identity, game and any third-party/self-host warning.
3. Start the supported game and enter gameplay; GLO installs only an exact game-server `/32` route after endpoint detection. Other applications and web traffic continue normally.
4. Check `RELAY003` for control handshake, `ROUTE007` for profile /32 installation, then `ROUTE008` and `ROUTE009` for successfully verified gameplay. See [DIAGNOSTICS.md](DIAGNOSTICS.md).

A successful control handshake is **not** proof of active gameplay. When the route verification fails, GLO removes the route and falls back to the normal connection; never disable that safety behavior simply to hide an error.

## Step 7 — operational hardening

- Restrict management via SSH/firewall; do not open internal/admin interfaces publicly.
- Rotate and back up issuer and relay identity keys securely; document the implications for existing signed grants.
- Preserve the redeemed-ticket journal on normal restarts to prevent replay; protect it as operational state.
- Monitor `SESS001`, `SESS005`, `ENG001` and `DATA001` separately: control sessions and gameplay flows are different metrics.
- Put reasonable caps on session/flow capacity, egress and allocation; enable maintenance during upgrades.
- Do not log URI handoff tokens, signed grants, secrets, or raw gameplay payloads.
- Follow [RELAY_OPERATOR.md](RELAY_OPERATOR.md), [SESSION_CONFIG.md](SESSION_CONFIG.md), [HANDOFF_PROVIDER.md](HANDOFF_PROVIDER.md), [TROUBLESHOOTING.md](TROUBLESHOOTING.md), and [BRAND_POLICY.md](../BRAND_POLICY.md).

## Important scope limits

The `selfhost_issuer` CLI is an **offline sample issuer** to validate a basic independent relay setup. It does **not** implement accounts, usage-balance banking, advertisements, abuse controls, public registration, handoff HTTPS endpoints, billing, or automatic relay scheduling. Production third-party services must provide their own admission policies and safe key management. No service credentials/private keys belong in the portable client config.
