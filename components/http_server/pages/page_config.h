/* Configuration page template - ESP32 Ethernet-WiFi Bridge */
#include "wifi_config.h"

/* Config Page - Chunked for streaming */
#define CONFIG_CHUNK_HEAD "<html>\
<head>\
<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>\
<meta charset='UTF-8'>\
<title>Configuration</title>\
<link rel='icon' href='favicon.png'>\
</head>\
<style>\
* { box-sizing: border-box; margin: 0; padding: 0; }\
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif; background: linear-gradient(135deg, #1a1510 0%, #1e1a12 100%); color: #e0e0e0; padding: 1rem; min-height: 100vh; line-height: 1.6; }\
h1 { font-size: 1.5rem; font-weight: 600; color: #ffb300; margin-bottom: 1rem; text-shadow: 0 0 20px rgba(255, 179, 0, 0.3); }\
h2 { font-size: 1.15rem; font-weight: 500; color: #ffb300; margin: 1.5rem 0 0.75rem 0; padding-bottom: 0.5rem; border-bottom: 1px solid rgba(255, 179, 0, 0.2); }\
#container { max-width: 1240px; margin: 0 auto; padding: 1.5rem; background: rgba(36, 30, 20, 0.9); border-radius: 16px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4); backdrop-filter: blur(10px); }\
form { margin-bottom: 1.5rem; }\
table { width: 100%; border-collapse: collapse; }\
td { padding: 0.5rem 0; vertical-align: top; }\
td:first-child { color: #888; font-size: 0.9rem; padding-right: 0.75rem; width: 35%; text-align: right; }\
input[type='text'], input[type='password'], input[type='number'] { width: 100%; background: rgba(40, 30, 15, 0.6); border: 1px solid rgba(255, 179, 0, 0.2); border-radius: 8px; color: #e0e0e0; padding: 0.75rem; font-size: 0.95rem; }\
input[type='text']:focus, input[type='password']:focus, input[type='number']:focus, select:focus { outline: none; border-color: #ffb300; box-shadow: 0 0 0 3px rgba(255, 179, 0, 0.1); background: rgba(40, 30, 15, 0.8); }\
input::placeholder { color: #666; }\
select { width: 100%; background: rgba(40, 30, 15, 0.6); border: 1px solid rgba(255, 179, 0, 0.2); border-radius: 8px; color: #e0e0e0; padding: 0.75rem; font-size: 0.95rem; cursor: pointer; -webkit-appearance: none; -moz-appearance: none; appearance: none; background-image: url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%23ffb300' stroke-width='1.5' fill='none'/%3E%3C/svg%3E\"); background-repeat: no-repeat; background-position: right 0.75rem center; padding-right: 2rem; }\
select option { background: #1e1a12; color: #e0e0e0; }\
input[type='checkbox'], input[type='radio'] { -webkit-appearance: none; -moz-appearance: none; appearance: none; width: 18px; height: 18px; border: 2px solid rgba(255, 179, 0, 0.3); border-radius: 4px; background: rgba(40, 30, 15, 0.6); cursor: pointer; vertical-align: middle; position: relative; flex-shrink: 0; }\
input[type='radio'] { border-radius: 50%; }\
input[type='checkbox']:checked, input[type='radio']:checked { background: #ffb300; border-color: #ffb300; }\
input[type='checkbox']:checked::after { content: ''; position: absolute; left: 4px; top: 1px; width: 6px; height: 10px; border: solid #1a1510; border-width: 0 2px 2px 0; transform: rotate(45deg); }\
input[type='radio']:checked::after { content: ''; position: absolute; left: 3px; top: 3px; width: 8px; height: 8px; border-radius: 50%; background: #1a1510; }\
input[type='checkbox']:focus, input[type='radio']:focus { outline: none; border-color: #ffb300; box-shadow: 0 0 0 3px rgba(255, 179, 0, 0.1); }\
.ok-button, .red-button { border: none; border-radius: 8px; padding: 0.75rem 1.5rem; font-size: 0.95rem; font-weight: 600; cursor: pointer; width: 100%; margin-top: 0.5rem; }\
.ok-button { background: linear-gradient(135deg, #f7971e 0%, #ffd200 100%); color: #1a1510; box-shadow: 0 4px 15px rgba(247, 151, 30, 0.4); }\
.red-button { background: linear-gradient(135deg, #e65100 0%, #bf360c 100%); color: #fff; box-shadow: 0 4px 15px rgba(230, 81, 0, 0.4); }\
small { display: block; color: #888; font-size: 0.85rem; margin-top: 0.5rem; line-height: 1.4; }\
.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:1rem 0 1.25rem}.tabs a{padding:.65rem 1rem;border-radius:8px;border:1px solid rgba(255,179,0,.25);background:rgba(40,30,15,.6);color:#ddd;text-decoration:none;font-weight:600}.tabs a.active{background:#ffb300;color:#1a1510}.gw-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}.gw-panel{background:rgba(40,30,15,.45);border:1px solid rgba(255,179,0,.15);border-radius:12px;padding:1rem}.gw-status{font-size:.9rem;color:#aaa;margin-top:.75rem}@media(max-width:800px){.gw-grid{grid-template-columns:1fr}}@media (max-width: 600px) { body { padding: 0.5rem; } #container { padding: 1rem; } h1 { font-size: 1.25rem; } h2 { font-size: 1rem; } td:first-child { font-size: 0.8rem; width: 40%; } input[type='text'], input[type='password'], input[type='number'], select { font-size: 0.9rem; padding: 0.65rem; } select { padding-right: 1.75rem; } .ok-button, .red-button { font-size: 0.9rem; padding: 0.65rem 1.25rem; } }\
</style>\
<body>\
<div id='container'>\
<div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.5rem;'>\
<div style='display: flex; align-items: center;'>\
<a href='/' style='display: inline-block; margin-right: 1rem;'><img src='/favicon.png' alt='Home' style='width: 64px; height: 64px; border: none;'></a>\
<h1 style='margin: 0;'>Gateway</h1>\
</div>"

/* After logout section */
#define CONFIG_CHUNK_SCRIPT "\
</div>\
<div class='tabs'><a href='/'>STATUS</a><a class='active' href='/config'>GATEWAY</a><a href='/sip'>SIP / VOIP</a><a href='/diagnostic'>DIAGNOSTICO</a></div>\
<script>\
var qs = window.location.search.substr(1);\
var formSubmitted = qs.indexOf('ap_ssid=') !== -1 || qs.indexOf('staticip=') !== -1 || qs.indexOf('reset=') !== -1 || qs.indexOf('disable_interface=') !== -1 || qs.indexOf('hostname=') !== -1 || qs.indexOf('pcap_save=') !== -1 || qs.indexOf('dhcps_save=') !== -1;\
if (formSubmitted) {\
document.getElementById('container').style.display = 'none';\
document.body.innerHTML ='<div id=\"container\"><h1>Configuration</h1><p style=\"text-align:center; margin: 2rem 0; color: #ffb300;\">Settings saved! Rebooting...</p></div>';\
setTimeout(\"location.href = '/'\", 10000);\
}\
</script>"

/* AP Settings section - uses: ap_ssid, ap_channel, auth_sel0/1/2, ap_en_checked, open_checked, hidden_checked */
#define CONFIG_CHUNK_AP "\
<h2>Access Point Settings</h2>\
<form action='' method='GET'>\
<table>\
<tr><td>SSID</td><td><input type='text' name='ap_ssid' value='%s' placeholder='Network name'/></td></tr>\
<tr><td>Password</td><td><input type='text' id='ap_pw' name='ap_password' placeholder='unchanged' oninput=\"document.getElementById('ap_op').checked=false;\"/></td></tr>\
<tr><td>Channel</td><td><input type='number' name='ap_channel' min='0' max='%d' value='%d' style='width:4em'/> <span style='color:#888;font-size:0.85rem;'>(0 = auto)</span></td></tr>\
<tr><td>Country</td><td><input type='text' name='wifi_cc' maxlength='2' value='%s' style='width:4em'/> <span style='color:#888;font-size:0.85rem;'>ISO 3166 (e.g. US, DE, 01=world)</span></td></tr>\
<tr><td>Security</td><td><select name='ap_auth'><option value='0' %s>WPA2/WPA3</option><option value='1' %s>WPA2 only</option><option value='2' %s>WPA3 only</option></select></td></tr>\
<tr><td>Enabled</td><td><input type='checkbox' name='ap_enable' value='1' %s></td></tr>\
<tr><td>Options</td><td><input type='checkbox' id='ap_op' name='ap_open' value='1' %s onchange=\"if(this.checked)document.getElementById('ap_pw').value='';\"> <span style='color:#888;font-size:0.85rem;'>Open (no password)</span> &nbsp; <input type='checkbox' name='ap_hidden' value='1' %s> <span style='color:#888;font-size:0.85rem;'>Hidden SSID</span></td></tr>\
<tr><td></td><td><input type='submit' value='Save &amp; Reboot' class='ok-button'/></td></tr>\
</table>\
</form>"

/* Management IP section - uses: static_ip, subnet_mask, gateway */
#define CONFIG_CHUNK_MGMT_IP "\
<h2>Management IP</h2>\
<form action='' method='GET'>\
<table>\
<tr><td>Static IP</td><td><input type='text' name='staticip' value='%s' placeholder='192.168.4.1'/></td></tr>\
<tr><td>Subnet Mask</td><td><input type='text' name='subnetmask' value='%s' placeholder='255.255.255.0'/></td></tr>\
<tr><td>Gateway</td><td><input type='text' name='gateway' value='%s' placeholder='(optional)'/></td></tr>\
<tr><td></td><td><input type='submit' value='Save &amp; Reboot' class='ok-button'/></td></tr>\
</table>\
<small>Management IP for accessing this web interface on the bridged network. Leave empty for DHCP.</small>\
</form>"

/* DHCP Server section - uses: dhcps_en_chk, dhcps_dis_chk, dhcps_start, dhcps_end, dhcps_lease, dhcps_dns */
#define CONFIG_CHUNK_DHCPS "\
<h2>DHCP Server</h2>\
<form action='' method='GET'>\
<input type='hidden' name='dhcps_save' value='1'/>\
<table>\
<tr><td>Service</td><td>\
<label style='margin-right: 0.8rem;'><input type='radio' name='dhcps_enabled' value='1' %s> Enabled</label>\
<label><input type='radio' name='dhcps_enabled' value='0' %s> Disabled</label>\
</td></tr>\
<tr><td>Pool Start</td><td><input type='text' name='dhcps_start' value='%s' placeholder='192.168.4.10'/></td></tr>\
<tr><td>Pool End</td><td><input type='text' name='dhcps_end' value='%s' placeholder='192.168.4.110'/></td></tr>\
<tr><td>Lease (min)</td><td><input type='number' name='dhcps_lease' value='%lu' min='1' max='14400' style='width:6em'/></td></tr>\
<tr><td>DNS</td><td><input type='text' name='dhcps_dns' value='%s' placeholder='(use bridge IP)'/></td></tr>\
<tr><td></td><td><input type='submit' value='Save &amp; Reboot' class='ok-button'/></td></tr>\
</table>\
<small>Requires static management IP. Disable your upstream DHCP server to avoid conflicts.</small>\
</form>"

/* Hostname section - uses: hostname */
#define CONFIG_CHUNK_HOSTNAME "\
<h2>Hostname</h2>\
<form action='' method='GET'>\
<table>\
<tr><td>Hostname</td><td><input type='text' name='hostname' value='%s' placeholder='%s'/></td></tr>\
<tr><td></td><td><input type='submit' value='Save &amp; Reboot' class='ok-button'/></td></tr>\
</table>\
</form>"

/* PCAP Capture section - uses: pcap_off_chk, pcap_on_chk, pcap_snaplen, bridge_ip */
#define CONFIG_CHUNK_PCAP "\
<h2>Packet Capture (PCAP)</h2>\
<form action='' method='GET'>\
<input type='hidden' name='pcap_save' value='1'/>\
<table>\
<tr><td>Capture</td><td>\
<label style='margin-right: 0.8rem;'><input type='radio' name='pcap_enabled' value='1' %s> Enabled</label>\
<label><input type='radio' name='pcap_enabled' value='0' %s> Disabled</label>\
</td></tr>\
<tr><td>Snaplen</td><td><input type='number' name='pcap_snaplen' value='%d' min='64' max='1600' style='width: 100px;'/> bytes</td></tr>\
<tr><td></td><td><input type='submit' value='Save' class='ok-button'/></td></tr>\
</table>\
<small style='font-family:monospace;'>use with: nc %s 19000 | wireshark -k -i -</small>\
</form>"

/* Remote Console section - uses: rc_en_chk, rc_dis_chk, rc_color, rc_status, rc_kick, rc_port, rc_ap_chk, rc_eth_chk, rc_timeout */
#define CONFIG_CHUNK_RC "\
<h2>Remote Console</h2>\
<form action='' method='GET'>\
<input type='hidden' name='rc_save' value='1'/>\
<table>\
<tr><td>Service</td><td>\
<label style='margin-right: 1rem;'><input type='radio' name='rc_enabled' value='1' %s> Enabled</label>\
<label><input type='radio' name='rc_enabled' value='0' %s> Disabled</label>\
</td></tr>\
<tr><td>Status</td><td><strong style='color: %s;'>%s</strong>%s</td></tr>\
<tr><td>Port</td><td><input type='number' name='rc_port' value='%d' min='1' max='65535' style='width: 100px;'/></td></tr>\
<tr><td>Bind</td><td>\
<label style='margin-right: 0.8rem;'><input type='checkbox' name='rc_bind_ap' value='1' %s> WiFi</label>\
<label><input type='checkbox' name='rc_bind_eth' value='1' %s> ETH</label>\
</td></tr>\
<tr><td>Idle Timeout</td><td><input type='number' name='rc_timeout' value='%lu' min='0' max='86400' style='width: 100px;'/> sec (0 = no)</td></tr>\
<tr><td></td><td><input type='submit' value='Save' class='ok-button'/></td></tr>\
</table>\
</form>"

/* Device management and footer */
#define CONFIG_CHUNK_TAIL "\
<h2>Gateway V2.5 / Simple IP Pairing</h2>\
<div class='gw-grid'>\
<div class='gw-panel'><h3 style='color:#ffb300;margin-bottom:.75rem'>Local Identity</h3>\
<table><tr><td>Gateway Name</td><td><input id='gwLocalName' type='text'></td></tr><tr><td>Location</td><td><input id='gwLocation' type='text'></td></tr><tr><td>Host / Sala Tag</td><td><span id='gwLocalHost'></span></td></tr><tr><td>IP</td><td><span id='gwLocalIp'></span></td></tr><tr><td>MAC</td><td><span id='gwLocalMac'></span></td></tr></table>\
<div style='margin-top:.75rem'><button type='button' class='ok-button' style='width:auto' onclick='gwSaveLocal()'>Save Local Identity</button></div></div>\
<div class='gw-panel'><h3 style='color:#ffb300;margin-bottom:.75rem'>KORVO Pairing</h3>\
<table><tr><td>KORVO IP</td><td><input id='gwPeerIp' type='text' value='192.168.10.50'></td></tr><tr><td>Detected Name</td><td><span id='gwPeerName'>-</span></td></tr><tr><td>Hostname / Sala</td><td><span id='gwPeerHost'>-</span></td></tr><tr><td>Location</td><td><span id='gwPeerLocation'>-</span></td></tr><tr><td>Live source IP</td><td><span id='gwObservedIp'>-</span></td></tr></table>\
<div style='display:flex;gap:.5rem;flex-wrap:wrap;margin:.75rem 0'><button type='button' class='ok-button' style='width:auto' onclick='gwSaveIp()'>SAVE KORVO IP</button><button type='button' class='ok-button' style='width:auto' onclick='gwVerify()'>VERIFY</button><button type='button' class='red-button' style='width:auto' onclick='gwClearEnrollment()'>REMOVE PAIRING</button></div>\
<div class='gw-status' id='gwEnrollState'>Waiting pairing status...</div>\
<small>V2.5 uses only reciprocal IP configuration for control. MAC, hostname and location are diagnostic only. CONTROL becomes OK when a WebSocket arrives from the configured KORVO IP.</small></div>\
</div>\
<script>\
var gwEditableIds=['gwLocalName','gwLocation','gwPeerIp'];\
function gwSetInput(id,v){var el=document.getElementById(id);if(!el)return;if(document.activeElement===el||el.dataset.dirty==='1')return;el.value=v||'';}\
function gwClearDirty(ids){ids.forEach(function(id){var el=document.getElementById(id);if(el)delete el.dataset.dirty;});}\
function gwInitEditable(){gwEditableIds.forEach(function(id){var el=document.getElementById(id);if(el)el.addEventListener('input',function(){this.dataset.dirty='1';});});}\
async function gwLoadEnrollment(){try{var r=await fetch('/api/gw/status',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);var j=await r.json(),d=j.data||{},e=d.enrollment||{},l=e.local||{},p=e.expected_peer||{},o=e.observed_peer||{};gwSetInput('gwLocalName',l.name||'');gwSetInput('gwLocation',l.location||'');document.getElementById('gwLocalHost').textContent=l.hostname||'-';document.getElementById('gwLocalIp').textContent=l.ip||'-';document.getElementById('gwLocalMac').textContent=l.mac||'-';if(p.ip)gwSetInput('gwPeerIp',p.ip);document.getElementById('gwPeerName').textContent=o.name||p.name||'-';document.getElementById('gwPeerHost').textContent=o.hostname||p.hostname||'-';document.getElementById('gwPeerLocation').textContent=o.location||p.location||'-';document.getElementById('gwObservedIp').textContent=o.ip||'-';var paired=e.paired?'PAIRED':'NOT PAIRED';var pc=e.control_ok?'CONTROL OK':'CONTROL BLOCKED';document.getElementById('gwEnrollState').innerHTML='Configured KORVO: <b>'+(p.ip||'-')+'</b> · State: <b style=color:'+(e.gateway_ok?'#4caf50':'#ffb300')+'>'+paired+'</b><br>Live IP match: <b style=color:'+(e.session_ip_ok?'#4caf50':'#ff5252')+'>'+(e.session_ip_ok?'YES':'NO')+'</b> · <b style=color:'+(e.control_ok?'#4caf50':'#ff5252')+'>'+pc+'</b>';}catch(x){document.getElementById('gwEnrollState').textContent='Pairing status unavailable: '+x.message;}}\
async function gwSaveLocal(){var p=new URLSearchParams({action:'local',local_name:document.getElementById('gwLocalName').value,location:document.getElementById('gwLocation').value});var r=await fetch('/api/gw/enrollment',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});var j=await r.json();if(!j.ok){alert('Error: '+(j.error||'unknown'));return;}gwClearDirty(['gwLocalName','gwLocation']);gwLoadEnrollment();}\
async function gwSaveIp(){var ip=document.getElementById('gwPeerIp').value.trim();var p=new URLSearchParams({action:'save_ip',peer_ip:ip});var r=await fetch('/api/gw/enrollment',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});var j=await r.json();if(!j.ok){alert('Error: '+(j.error||'unknown'));return;}gwClearDirty(['gwPeerIp']);gwLoadEnrollment();}\
async function gwVerify(){var box=document.getElementById('gwEnrollState');box.textContent='Verifying configured KORVO IP...';try{var r=await fetch('/api/gw/verify',{method:'POST'});var j=await r.json();if(!j.ok&&j.error){box.innerHTML='<b style=color:#ff5252>'+j.error+'</b>';return;}var q=j.probe||{},id=q.identity||{};box.innerHTML='KORVO <b>'+(j.target_ip||'-')+'</b> · '+((q.state||'').toUpperCase())+' · '+(id.name||id.hostname||'')+'<br>CONTROL becomes OK automatically when that same IP opens the KORVO WebSocket.';gwLoadEnrollment();}catch(x){box.textContent='Verify error: '+x.message;}}\
async function gwClearEnrollment(){if(!confirm('Remove configured KORVO IP and block remote GPIO/control?'))return;var p=new URLSearchParams({action:'clear'});var r=await fetch('/api/gw/enrollment',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});var j=await r.json();if(j.ok){gwClearDirty(['gwPeerIp']);document.getElementById('gwPeerIp').value='192.168.10.50';gwLoadEnrollment();}}gwInitEditable();gwLoadEnrollment();setInterval(gwLoadEnrollment,3000);\
</script>\
<h2>Device Management</h2>\
<div id='mainContent'>\
<h3 style='font-size:1rem;color:#aaa;margin:1rem 0 0.5rem;'>Firmware Update (OTA)</h3>"

/* OTA info (running partition, version) is streamed dynamically here */
#define CONFIG_CHUNK_TAIL2 "\
<table>\
<tr><td>Upload</td><td>\
<label style='display:inline-block;padding:0.6rem 1rem;background:rgba(22,33,62,0.6);border:1px solid rgba(0,217,255,0.2);border-radius:8px;color:#e0e0e0;font-size:0.9rem;cursor:pointer;transition:all 0.3s;margin-bottom:0.5rem;'>\
<input type='file' id='otaFile' accept='.bin' style='display:none;'/>\
<span id='otaFileName'>Choose .bin file...</span>\
</label><br/>\
<button type='button' onclick='uploadOTA()' class='ok-button'>Upload Firmware</button>\
<div id='otaProgress' style='margin-top:0.5rem;'>\
<div id='otaBar' style='display:none;height:6px;background:rgba(255,179,0,0.2);border-radius:3px;overflow:hidden;'>\
<div id='otaBarFill' style='height:100%;width:0;background:#ffb300;transition:width 0.3s;'></div>\
</div>\
</div>\
<div id='otaStatus' style='margin-top:0.5rem;font-size:0.9rem;'></div>\
</td></tr>\
</table>\
<h3 style='font-size:1rem;color:#aaa;margin:1.5rem 0 0.5rem;'>Reboot</h3>\
<form action='' method='GET'>\
<table>\
<tr><td>Device</td><td><input type='submit' name='reset' value='Reboot Now' class='red-button'/></td></tr>\
</table>\
</form>\
</div>\
<div id='rebootScreen' style='display:none;text-align:center;padding:2rem 0;'>\
<h2 style='color:#4caf50;margin-bottom:1rem;' id='rebootTitle'>Success</h2>\
<p style='font-size:1.1rem;margin-bottom:0.5rem;' id='rebootMsg'>The device is rebooting...</p>\
<p style='font-size:1.5rem;font-weight:bold;color:#ffb300;' id='countdown'></p>\
<p style='color:#888;font-size:0.9rem;'>Redirecting to home page...</p>\
</div>\
<script>\
document.getElementById('otaFile').addEventListener('change',function(){document.getElementById('otaFileName').textContent=this.files[0]?this.files[0].name:'Choose .bin file...';});\
function uploadOTA(){\
var f=document.getElementById('otaFile').files[0];\
if(!f){document.getElementById('otaStatus').textContent='Select a firmware file first.';return;}\
if(!f.name.endsWith('.bin')){document.getElementById('otaStatus').innerHTML='<span style=\"color:#ff5252;\">Please select a .bin file</span>';return;}\
document.getElementById('otaStatus').textContent='Uploading...';\
document.getElementById('otaBar').style.display='block';\
var xhr=new XMLHttpRequest();\
xhr.open('POST','/api/ota-upload',true);\
xhr.upload.onprogress=function(e){if(e.lengthComputable){var pct=Math.round(e.loaded/e.total*100);document.getElementById('otaBarFill').style.width=pct+'%';document.getElementById('otaStatus').textContent='Uploading... '+pct+'%';}};\
xhr.onload=function(){\
try{var d=JSON.parse(xhr.responseText);\
if(d.ok){\
document.getElementById('mainContent').style.display='none';\
document.getElementById('rebootScreen').style.display='block';\
document.getElementById('rebootTitle').textContent='Firmware Updated';\
document.getElementById('rebootMsg').textContent='The device is rebooting with new firmware.';\
var c=10;var el=document.getElementById('countdown');\
var t=setInterval(function(){c--;el.textContent=c;if(c<=0){clearInterval(t);window.location.href='/';}},1000);\
}else{\
document.getElementById('otaBarFill').style.width='0';document.getElementById('otaBar').style.display='none';\
document.getElementById('otaStatus').innerHTML='<span style=\"color:#ff5252;\">'+d.msg+'</span>';\
}}catch(e){document.getElementById('otaStatus').innerHTML='<span style=\"color:#ff5252;\">Upload failed</span>';}\
};\
xhr.onerror=function(){document.getElementById('otaStatus').innerHTML='<span style=\"color:#ff5252;\">Connection error</span>';};\
xhr.send(f);\
}\
</script>\
<div style='margin-top: 2rem; padding: 1rem; background: #fff3cd; border: 2px solid #ff9800; border-radius: 8px;'>\
<h2 style='color: #ff6b00; margin-bottom: 0.5rem;'>Danger Zone</h2>\
<p style='margin-bottom: 1rem; color: #666; font-size: 0.9rem;'>This will disable the web interface completely. You can only re-enable it via the console using the 'web_ui enable' command.</p>\
<form action='' method='GET'>\
<table>\
<tr><td style='color: #d32f2f; font-weight: bold;'>Disable Interface</td>\
<td><input type='submit' name='disable_interface' value='Disable' class='red-button' onclick='return confirm(\"Are you sure? The web interface will be disabled and can only be re-enabled via the console.\");'/></td></tr>\
</table>\
</form>\
</div>\
<div style='margin-top: 2rem; text-align: center;'>\
<a href='/' style='padding: 0.75rem 2rem; background: linear-gradient(135deg, #f7971e 0%, #ffd200 100%); color: #1a1510; border: none; border-radius: 8px; text-decoration: none; font-size: 0.95rem; font-weight: 600;'>Home</a>\
</div>\
</div>\
</body>\
</html>"

