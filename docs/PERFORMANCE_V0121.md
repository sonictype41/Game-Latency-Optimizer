# GLO v0.12.1 dataplane performance note

The GLOD1 dataplane introduced before v0.12 replaces encrypted gameplay `GLO2 -> GLO6 AEAD` with plaintext `GLOD1` while keeping handshake/control security unchanged.

The main performance changes are:

- no ChaCha20-Poly1305 work for gameplay packets;
- no gameplay `GLO2` intermediate allocation;
- zero-copy GLOD decoding on the Go relay C2S path;
- pooled/reused GLOD S2C output buffers;
- direct span-based Windows gameplay path instead of a `protocol::Packet` payload vector;
- C++ GLOD framing clears only the fixed header instead of touching the payload region before copying it;
- 40 bytes of GLO6+AEAD overhead removed per gameplay datagram.

The userspace security/filter layer remains authoritative. A service deployment may add additional infrastructure-side protections outside this repository; relay correctness does not depend on those protections.

Performance claims should be reproduced with the current build and workload; generated validation logs are intentionally not committed to the source tree.
