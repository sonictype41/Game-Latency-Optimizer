# Wintun build input

GLO pins the official Wintun 0.14.1 AMD64 runtime by upstream URL, archive SHA-256 and DLL SHA-256 in `INFO.json`.

- Upstream archive: `wintun-0.14.1.zip`
- Archive SHA-256: `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`
- Canonical member: `wintun/bin/amd64/wintun.dll`
- DLL SHA-256: `e5da8447dc2c320edc0fc52fa01885c103de8c118481f683643cacc3220dafce`

The public source repository does **not** track the DLL. Fetch and verify the pinned runtime before a Windows build:

```bash
python ./tools/fetch_wintun.py
python ./tools/verify_windows_resources.py
```

Windows CI performs these steps automatically. Full release bundles may already contain the same verified runtime for offline builds. `LICENSE.txt` is kept with the metadata in all distributions.
