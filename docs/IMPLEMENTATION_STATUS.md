# KORVO Gateway V2.4 - Integration freeze

## Purpose
V2.4 freezes the Gateway side of the KORVO <-> Gateway interface before KORVO1 is updated.
The bridge management address used in the current laboratory (Gateway 192.168.10.51) remains an NVS/network configuration value. It is **not** a peer-routing constant in the firmware.

The KORVO laboratory address `192.168.10.50` appears only as the initial value of the **new enrollment target** field when no KORVO has been enrolled. Once enrollment exists, every verification/control decision uses the peer IP/MAC stored in NVS for that node.

## V2.4 rules

- No operational KORVO IP is hard-coded.
- No normal-operation network scan exists.
- `VERIFY ENROLLED KORVO` probes only `expected_peer.ip` from NVS.
- WiFi RSSI/status is attributed to the operational peer only when the associated station MAC matches `expected_peer.mac`.
- Other WiFi stations are diagnostic/enrollment candidates only and never grant field control.
- IP, MAC, Host/Sala, Name and Location are compared with the enrolled peer.
- `GATEWAY OK` means the complete peer identity matches.
- `CONTROL OK` additionally requires Pair Key authentication.
- A changed IP/MAC/host does not update enrollment automatically. It produces mismatch/block until explicit re-enrollment.

## Pairing from KORVO

A new KORVO may POST its identity to:

`POST /api/gw/enroll/request`

The Gateway only accepts this when:

1. an authenticated operator has armed a 120-second enrollment window from the Gateway webserver;
2. no peer is already enrolled;
3. the HTTP source IPv4 equals the IP declared by KORVO;
4. exactly one WiFi station is connected to the Gateway AP;
5. the station MAC equals the MAC declared by KORVO.

The Gateway stores the KORVO identity and returns Gateway identity + Pair ID + Pair Key.
This does **not** enable field control by itself. KORVO must then authenticate the WebSocket session.

## WebSocket control channel

Endpoint:

`ws://<gateway-management-ip>/ws/korvo`

The Gateway accepts the session only when:

- the TCP source IPv4 equals the enrolled KORVO IP;
- the enrolled KORVO MAC is currently associated to the Gateway AP.

Gateway sends:

```json
{"v":1,"type":"challenge","name":"sys.auth","data":{"pair_id":"PAIR-...","nonce":"..."}}
```

KORVO computes:

`HMAC-SHA256(pair_key, nonce + "|" + pair_id)`

and replies:

```json
{
  "v":1,
  "type":"auth",
  "name":"sys.auth",
  "data":{
    "pair_id":"PAIR-...",
    "hmac":"64-hex-characters",
    "name":"KORVO-01",
    "hostname":"SALA-01",
    "location":"SALA-01",
    "ip":"192.168.10.50",
    "mac":"AA:BB:CC:DD:EE:FF"
  }
}
```

Only after IP + WiFi MAC + identity fields + HMAC all match does the Gateway set `CONTROL OK` and accept field-changing commands.

## Read-only vs field-changing traffic

Status remains readable for diagnosis. Field commands remain blocked until CONTROL OK:

- `door.unlock`
- `door.lock`
- `beacon.*`
- `wiegand.tx`
- `sip.call`
- `sip.answer`
- `sip.reject`
- `sip.hangup`

## Web UI changes

Gateway tab now separates:

- Local Identity
- Enrollment Target (commissioning only)
- Enrolled KORVO
- Verify Enrolled KORVO
- Remove Pairing

The old generic `Find Suggested IP` / normal network search behavior is removed.

The Status WiFi card shows:

- SSID / BSSID
- channel / frequency
- enrolled KORVO IP
- enrolled KORVO MAC
- actual radio MAC
- RSSI band
- client count

When no KORVO is enrolled, one connected station can be shown as an **ENROLLMENT CANDIDATE**, never as the operational peer.

## Still intentionally pending

- Real SIP/RTP engine
- Bluetooth HFP engine
- KORVO1 mirror implementation of enrollment/HMAC/WebSocket

SIP and Bluetooth configuration/status simulators remain available so KORVO1/HMI work can continue before those engines are attached.

## Build
No ESP-IDF build was run in this workspace. Build/flash is to be done on the local ESP-IDF 5.3.1 environment.
