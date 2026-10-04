# GLO wire protocols v0.12.2

GLO v0.12.2 deliberately separates secure session/control traffic from the hot gameplay path.

```text
GLH6  handshake / relay identity / signed-ticket admission
GLO6  encrypted established control transport
GLO2  inner control packet encoding carried inside GLO6
GLOD1 plaintext gameplay dataplane
```

Naked GLO2 packets are never accepted from the network.

## GLO2 control packet encoding

GLO2 retains the 32-byte v2 header. In v0.12.2 it is control-only.

```text
1  HELLO
2  WELCOME
3  PING
4  PONG
5  BYE
6  ERROR
7  RESERVED
8  RESERVED
9  RESERVED  (retired FLOW_OPEN)
10 RESERVED  (retired FLOW_OPEN_ACK)
11 FLOW_CLOSE
12 RESERVED  (retired encrypted DATA_C2S)
13 RESERVED  (retired encrypted DATA_S2C)
14 STATS_REQUEST
15 STATS_RESPONSE
16 FINISH
17 FINISH_ACK
```

`ERROR.flags` gameplay admission reasons remain:

```text
1 capacity
2 maintenance
3 temporary/unavailable
```

Control packets are encoded as GLO2 then authenticated/encrypted by GLO6.

## GLOD1 gameplay dataplane

GLOD is a separate UDP wire format and is accepted only for active sessions after FINISH/FINISH_ACK.

Header, 32 bytes:

```text
0..3   "GLOD"
4      version = 1
5      direction: 1=C2S, 2=S2C
6..7   flags = 0
8..15  session_id
16..23 sequence
24..27 flow_id
28..29 raw gameplay payload length
30     route metadata length: 8 for C2S, 0 for S2C
31     reserved = 0
```

C2S body:

```text
8-byte route metadata:
  IPv4 target  4
  target port  2
  client port  2
raw game UDP payload
```

S2C body:

```text
raw game UDP payload
```

Maximum raw game UDP payload remains 1372 bytes. GLOD does not provide payload confidentiality or cryptographic integrity. Game bytes are relayed without application-protocol parsing or transformation. Route metadata is treated as untrusted and must pass userspace session/flow/target policy before a socket is created or reused.


### v0.12.2 data-wire marker
`FINISH` and `FINISH_ACK` require flag `0x0001` (GLOD1 capability flag). This intentionally rejects v0.11.4 peers at session activation instead of attempting a legacy encrypted-DATA fallback.
