# OSS v0.0.5-r2-beta — Port rules and responsive handoff

## UDP Port Rule Expressions

Schema 2 sessions with `port_min` and `port_max` still work. Schema 3 sessions additionally include a normalized `udp_port_rules` string, e.g. `7000-8000,8181,9000-9100`. Exact port/range matching is enforced in the privileged worker and Wintun, not just by the UI. The Linux relay must receive matching policy entries for each destination host and interval. No Direct bypass is added by this release; unlisted ports intercepted by host routes continue to be dropped.

Supported grammar: one to 32 comma-separated ports or inclusive ranges; ports 1–65535, no negatives, missing endpoints or reversed ranges. Overlapping/adjacent ranges normalize to their union. The self-host issuer has `issue -udp-port-rules "7000-8000,8181"` (configure its relay policy separately).

## Non-blocking URI handoff

WinHTTP inspection, relay RTT measurement and ticket redeeming now run outside the Win32 message loop. `WM_COPYDATA` copies the link and returns promptly. The modal shows progress with an animated indicator; Cancel/ESC mark the task cancelled and ensure late responses cannot connect. An HTTP request already in progress may finish after cancellation. No progress percentage is fabricated.

## Deployment order

Ship/install OSS r2 before publishing schema 3 multi-range profiles in a provider. Publish/update matching relay policy *before* allowing port 8181 (or any additional port) in a game profile. Public server lists may still depend on other traffic; enabling port 8181 is a test, not a confirmed fix for the Riot server list issue.

## Verification

Portable C++ parser/session tests, Go issuer/relay tests and Python source/architecture tests are included. Full Windows GUI/Wintun end-to-end verification requires a Windows test machine and is not reproduced on Linux.
