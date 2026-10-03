# Gateway V2.2 - Update 2 (2026-09-19)

## Alcance
Esta versión reemplaza directamente al Update 1 no probado. Mantiene la base Ethernet/Wi-Fi bridge del firmware recibido y consolida la capa Gateway de acceso antes de integrar el componente HTML externo `http_server`.

## GPIO congelado

| GPIO | Dirección | Función |
|---:|:---:|---|
| 35 | IN | Wiegand D0 desde lector |
| 36 | IN | Wiegand D1 desde lector |
| 5 | OUT open-drain | Wiegand D0 hacia controlador |
| 17 | OUT open-drain | Wiegand D1 hacia controlador |
| 4 | IN | REX |
| 32 | IN | Sensor puerta |
| 39 | IN | CALL / ANSWER / long-press HANGUP |
| 33 | OUT | Relé puerta |
| 14 | OUT | Baliza |
| 2 | reserva | Reserva |
| 15 | reserva | Reserva |
| 12 | reserva | Último recurso |

RED/GREEN del lector no consume GPIO: se resuelve por contacto auxiliar físico del relé.

**Hardware:** GPIO35/36/39 no tienen pull-up interno. Wiegand IN y CALL requieren polarización/acondicionamiento externo. Wiegand OUT trabaja open-drain y necesita interfaz/pull-up externo. GPIO5 es strapping y el circuito externo no debe forzarlo durante boot.

## Nuevos módulos V2.2

- `gateway_enrollment.*`: identidad local, peer esperado/observado, persistencia NVS y autorización de control.
- `gateway_wifi.*`: telemetría del AP/enlace KORVO: SSID, BSSID, canal, frecuencia, clientes, MAC peer, RSSI y banda de señal.
- `gateway_events.*`: bus común de eventos.
- `gateway_io.*`: GPIO/Wiegand/REX/puerta/CALL/relé/baliza.
- `gateway_services.*`: estados SIP/Bluetooth y lógica contextual del botón CALL.
- `gateway_diag.*`: simulación integral, ahora también enrollment y Wi-Fi/RSSI.
- `gateway_protocol.*`: contrato JSON V1 + `sys.hello`, `sys.status` ampliado y bloqueo de comandos si el peer no está enrolado.

## Enrolamiento mutuo KORVO <-> Gateway

No se confía sólo en IP. El Gateway conserva:

### Identidad local
- Gateway Name (tag propio)
- Hostname
- Location / Sala tag
- IP actual
- MAC/BSSID Wi-Fi AP

### KORVO esperada
- KORVO Name
- Hostname
- Location / Sala tag
- IP
- MAC Wi-Fi
- Pair ID
- Pair Key (secreto, nunca se expone por JSON)

### KORVO observada
La futura capa WebSocket debe llenar la identidad recibida en `sys.hello` y marcar si el challenge de Pair Key fue válido.

### Estados
- `unconfigured`
- `offline`
- `authenticating`
- `ok`
- `mismatch`
- `auth_failed`

`CONTROL OK` sólo existe cuando coinciden nombre, hostname, ubicación, IP y MAC, y además la autenticación de Pair Key fue válida.

## Regla de seguridad de control

`sys.ping`, `sys.status` y `sip.status` siguen disponibles aunque exista mismatch para permitir diagnóstico.

Los siguientes comandos remotos son rechazados con `peer_not_enrolled` mientras `CONTROL OK == false`:

- `door.*`
- `beacon.*`
- `wiegand.*`
- `sip.call`
- `sip.answer`
- `sip.reject`
- `sip.hangup`

Las pruebas físicas lanzadas localmente desde la futura pestaña DIAGNÓSTICO usan las funciones de diagnóstico y no dependen del enlace KORVO.

## `sys.hello`

El Gateway ya puede construir un mensaje de identidad con:
- role = gateway
- versión de capa Gateway
- device_name
- hostname
- location
- IP
- MAC
- pair_id
- enrollment state
- control_ok

El Pair Key no se transmite.

## `sys.status` ampliado

Incluye ahora:
- I/O
- Wiegand
- SIP
- Bluetooth/audio
- Wi-Fi link
- enrollment/control

La sección `enrollment` entrega identidad local, peer esperado, peer observado y flags individuales `name/hostname/location/ip/mac/pair_auth`.

## Card Wi-Fi prevista para STATUS

Datos disponibles desde `gateway_wifi.*`:
- SSID
- BSSID
- Channel
- Frequency MHz
- Client count
- Peer MAC
- RSSI dBm
- Signal band

Bandas:
- >= -55 dBm: `excellent`
- -56..-65: `good`
- -66..-75: `fair`
- -76..-85: `weak`
- < -85: `poor`

La UI acordada será una card compacta tipo Ubiquiti, con barra de señal simple, no un panel grande.

## Webserver V2 acordado

Desktop-first, ancho, 4 pestañas:

1. **STATUS**
   - Gateway/System
   - Ethernet
   - Wi-Fi Link
   - KORVO / Enrollment
   - Access
   - SIP
   - Bluetooth
   - últimos eventos

2. **GATEWAY**
   - red/bridge actual
   - Wiegand
   - puerta/REX/I/O
   - identidad: Gateway Name + Hostname + Location
   - KORVO esperada: IP/HOST/MAC/Name/Location
   - Pair ID / enrolamiento

3. **SIP / VOIP**
   - PBX/port/user/password
   - nombre de anexo propio
   - central destino + nombre
   - SEND+RECEIVE / SEND ONLY / RECEIVE ONLY / DISABLED
   - Bluetooth scan/pair/connect/forget
   - audio/HFP

4. **DIAGNÓSTICO**
   - Wiegand
   - REX
   - puerta/forced/held-open
   - relay
   - baliza
   - CALL short/long
   - SIP completo
   - BT/audio
   - Wi-Fi/RSSI simulado
   - Enrollment OK/MISMATCH/AUTH FAILED/OFFLINE
   - fault injection
   - log

El HTML todavía no puede modificarse porque el `main.zip` recibido no contiene el componente externo `http_server` que implementa `start_webserver()`.

## Simulador

La regla permanece: todo estado visible por KORVO/HMI debe poder reproducirse, siempre que sea técnicamente posible. El Update 2 agrega simulación de peer/enrollment y del enlace Wi-Fi/RSSI además de lo existente.

Los eventos simulados conservan los mismos nombres que los reales y sólo agregan `simulated:true`.

## SIP / CALL

Modo configurable:
- `send_receive`
- `send_only`
- `receive_only`
- `disabled`

Botón GPIO39:
- short + IDLE -> CALL (si puede enviar)
- short + INCOMING -> ANSWER (si puede recibir)
- long + INCOMING -> REJECT
- long + CALLING/RINGING/CONNECTED -> HANGUP

La llamada saliente usa exclusivamente la central/anexo configurado en el webserver del Gateway. KORVO no envía un número de destino arbitrario.

Los eventos SIP hacia KORVO incluyen dirección, número/anexo remoto, nombre y call_id para permitir mostrar `LLAMANDO A CENTRAL`, `LLAMADA ENTRANTE 312`, etc.

## Notas para KORVO

El footer de KORVO podrá derivar directamente de estos datos:
- `GATEWAY OK / MISMATCH / OFFLINE`
- `CONTROL OK / BLOCKED`
- `SIP REGISTERED / ...`

El nombre/tag de sala y nombre/tag del Gateway también quedan disponibles. La disposición visual definitiva se implementa en KORVO, respetando su ROI central de detección y sus banderas existentes de SD y movimiento.

## Fuera del Gateway

QR, barcode, cédula/documentos, cara viva y lógica de evitar reconocimiento facial sobre retratos impresos pertenecen al motor de autenticación/visión de KORVO, no al Gateway. No requieren GPIO nuevos en esta versión.

## Estado del paquete

Este ZIP sigue siendo una actualización de `main/` porque el proyecto completo ESP-IDF y los componentes externos no fueron entregados. No fue posible realizar una compilación final aquí. Debe integrarse con el proyecto original antes de flashear. El próximo paso, cuando esté disponible `components/http_server/`, es implementar físicamente las cuatro pestañas y conectar sus formularios/controles a estas APIs.

## Complete-project packaging update

The complete source package now includes the supplied project root files, `components/` and `include/`, plus the V2.2 `main/` layer. The actual device web UI has been integrated into the supplied `http_server` component and extended from `main/gateway_web.c`. No build was performed in this delivery.

---

# V2.3 follow-up - Legacy KORVO discovery and live STATUS

The laboratory KORVO is currently connected to the Gateway AP using its older firmware. Therefore the Gateway must distinguish radio connectivity from application enrollment.

V2.3 adds:

- live Wi-Fi station visibility regardless of KORVO application version;
- `legacy_reachable` discovery when the old KORVO webserver answers but has no identity API;
- automatic filling of identity fields once KORVO publishes `/api/device/identity`;
- future reverse enrollment from KORVO to Gateway via `/api/gw/enroll/request`;
- immediate refresh of the Expected KORVO panel every 3 seconds;
- no permanent `Loading...` status cards;
- explicit `CONFIG / SIMULATION` indication for SIP and Bluetooth until their real engines are integrated.

The old KORVO can therefore remain connected during Gateway development. It should already appear as a Wi-Fi client (MAC/RSSI), while full Name/Hostname/Location enrollment becomes available after the KORVO firmware update.

# V2.4 - Enrolled-node addressing and KORVO control channel

- Operational peer IP is always `expected_peer.ip` from NVS.
- Operational peer WiFi MAC is always `expected_peer.mac` from NVS.
- Laboratory `.50/.51` addresses are not control constants.
- Generic discovery removed from normal operation.
- Added explicit enrollment-candidate check and `VERIFY ENROLLED KORVO`.
- WiFi status now distinguishes enrolled peer from a commissioning candidate.
- `GATEWAY OK` and `CONTROL OK` are separate states.
- Added `/ws/korvo` authenticated WebSocket.
- Pair authentication: HMAC-SHA256(pair_key, nonce|pair_id).
- Status/events can flow before field actions; field actions require CONTROL OK.

- Added 120-second authenticated enrollment window before `/api/gw/enroll/request` is accepted.
