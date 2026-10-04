# v0.12.2 integration boundary

The retired plaintext GLO2 `DATA_C2S/DATA_S2C` harnesses were removed with the v0.12.2 wire break.
Gameplay integration is covered by `session_ticket_interop.py`, which establish the secure GLH6/GLO6 control session first and then exercise plaintext GLOD1. Relay admission, shaper, concurrency, TTL, target-policy and sequence accounting remain covered by Go tests in `relay/core/glo_core` and `relay/dataplane`.
