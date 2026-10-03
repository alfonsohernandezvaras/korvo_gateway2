# Gateway Webserver V2 - UI contract

This is the target UI for the external `http_server` component once its source is available.

## Layout
Desktop-first; use screen width instead of the existing narrow phone-like column.

Tabs:
`STATUS | GATEWAY | SIP / VOIP | DIAGNÓSTICO`

## STATUS cards

### Gateway/System
Name, hostname, location, firmware/layer version, uptime, heap.

### Ethernet
Link/IP/bridge information already available in current firmware.

### Wi-Fi Link
Compact radio card:
- SSID
- BSSID
- KORVO peer MAC
- channel / frequency
- RSSI dBm
- horizontal signal bar
- signal band: EXCELLENT/GOOD/FAIR/WEAK/POOR
- client count

### KORVO / Enrollment
Expected vs observed:
- Name
- Hostname
- Location
- IP
- MAC
- Pair authentication

Large final indicators:
- `GATEWAY OK` / `MISMATCH` / `OFFLINE`
- `CONTROL OK` / `BLOCKED`

### Access
Reader, REX, door, relay, beacon.

### SIP
Registered, extension/name, current call state, remote extension/name.

### Bluetooth
Device/profile/audio ready.

## GATEWAY tab
Preserve all current bridge/network settings, then add:
- Gateway Name
- Hostname
- Location/Sala tag
- Expected KORVO Name
- Expected KORVO Hostname
- Expected KORVO Location
- Expected KORVO IP
- Expected KORVO MAC
- Pair ID
- Enroll / Remove Pairing workflow

MAC should normally be captured/validated rather than casually edited during normal operation.

## SIP / VOIP tab
- PBX server / port
- extension / extension name
- password
- central extension / central name
- CALL mode
- call timeout
- beacon behavior
- Bluetooth scan / pair / connect / disconnect / forget
- speaker/mic test when HFP engine exists

## DIAGNÓSTICO tab
Expose all gateway_diag functions, including:
- card/Wiegand simulation
- REX
- door
- relay/beacon
- CALL short/long
- all SIP states
- BT/audio
- Wi-Fi RSSI/channel simulation
- enrollment peer identity + pair auth OK/FAIL
- peer offline
- fault injection
- event console

## V2.4 addressing rule

The Gateway page must never imply that one KORVO IP is built into the product.
`192.168.10.50` is only the current laboratory commissioning default displayed when there is no enrollment.

After enrollment:

- Status displays `expected_peer.ip` and `expected_peer.mac`.
- `VERIFY ENROLLED KORVO` uses only `expected_peer.ip`.
- WiFi signal is shown as the operational peer only when an associated station matches `expected_peer.mac`.
- A different associated station may be shown as diagnostic/candidate information but cannot produce CONTROL OK.
- No peer identity field is silently rewritten from live network observations.
