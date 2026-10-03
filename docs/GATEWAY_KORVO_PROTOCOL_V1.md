# Gateway <-> KORVO Protocol V1 (V2.4 transport freeze)

## Addressing
Neither side uses a compiled peer IP. Each device stores the peer IP/MAC/identity obtained during explicit enrollment.

## Transport
- Ethernet/WiFi bridged LAN
- WebSocket: `/ws/korvo`
- JSON V1

## Session authentication
1. KORVO connects to the Gateway IP stored in its enrollment.
2. Gateway verifies TCP source IP equals its enrolled KORVO IP.
3. Gateway verifies the enrolled KORVO MAC is the associated WiFi station.
4. Gateway sends `sys.auth` challenge with Pair ID + nonce.
5. KORVO returns HMAC-SHA256(pair_key, `nonce|pair_id`) and its identity.
6. Gateway compares Name, Host/Sala, Location, IP, MAC and HMAC.
7. Successful result: `GATEWAY OK`, `CONTROL OK`.

## KORVO -> Gateway commands

Read/status:
- `sys.ping`
- `sys.status`
- `sip.status`

Field-changing (CONTROL OK required):
- `door.unlock` `{ "ms": 5000 }`
- `door.lock`
- `beacon.on`
- `beacon.off`
- `beacon.flash` `{ "on_ms":500, "off_ms":500, "cycles":5 }`
- `wiegand.tx` `{ "format":26, "facility":101, "card":38724 }`
- `sip.call`
- `sip.answer`
- `sip.reject`
- `sip.hangup`

## Gateway -> KORVO events

System/enrollment:
- `sys.hello`
- `sys.pong`
- `enrollment.status`
- `control.ok`
- `control.blocked`

Access/I/O:
- `reader.card`
- `rex.active`
- `rex.inactive`
- `door.open`
- `door.closed`
- `door.relay_on`
- `door.relay_off`
- `door.forced`
- `door.held_open`
- `call.short_press`
- `call.long_press`

SIP:
- `sip.idle`
- `sip.calling`
- `sip.ringing`
- `sip.incoming`
- `sip.connected`
- `sip.busy`
- `sip.no_answer`
- `sip.rejected`
- `sip.failed`
- `sip.ended`

Bluetooth/audio:
- `bt.connected`
- `bt.disconnected`
- `bt.failed`
- `audio.ready`
- `audio.not_ready`

## Status semantics

`GATEWAY OK`: enrolled Name + Host + Location + IP + MAC match the observed peer.

`CONTROL OK`: GATEWAY OK plus valid Pair Key HMAC for the active WebSocket session.

A peer mismatch never auto-rewrites enrollment.
