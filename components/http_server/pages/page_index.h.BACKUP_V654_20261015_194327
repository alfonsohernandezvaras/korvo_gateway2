/* Gateway V2.5 status page - wide desktop-first layout */
#define INDEX_TITLE "KORVO Gateway"

#define INDEX_CHUNK_HEAD "<html>\
<head>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
<meta charset='UTF-8'>\
<title>" INDEX_TITLE "</title>\
<link rel='icon' href='/favicon.png'>\
<style>\
*{box-sizing:border-box;margin:0;padding:0}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;background:#17140f;color:#e7e3dc;padding:18px;min-height:100vh;line-height:1.45}#container{max-width:1240px;margin:0 auto}.head{display:flex;justify-content:space-between;align-items:center;gap:16px}.brand{display:flex;align-items:center;gap:12px}.brand img{width:54px;height:54px}.brand h1{font-size:1.55rem;color:#ffb300}.brand p{color:#8f8a82;font-size:.82rem}.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:16px 0}.tabs a{padding:10px 16px;border-radius:8px;border:1px solid #554520;background:#231e15;color:#ded8ce;text-decoration:none;font-weight:600}.tabs a.active{background:#ffb300;color:#1a1510}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card,.status-table{background:#242018;border:1px solid #493a1d;border-radius:12px;padding:15px}.card h2,.section-title{font-size:1rem;color:#ffb300;margin:0 0 10px}.kv{display:grid;grid-template-columns:150px minmax(0,1fr);gap:8px;padding:5px 0;border-bottom:1px solid #312a1f}.kv:last-child{border:0}.k{color:#969087}.v{font-weight:600;overflow-wrap:anywhere}.ok{color:#69d27b}.bad{color:#ff6961}.warn{color:#ffc857}.muted{color:#918a80}.signal{height:10px;background:#332b21;border-radius:8px;overflow:hidden;margin:8px 0}.signal>div{height:100%;background:#ffb300;width:0}.status-table{margin-top:14px}.status-table table{width:100%;border-collapse:collapse}.status-table td{padding:.55rem .4rem;border-bottom:1px solid rgba(255,255,255,.05)}.status-table tr:last-child td{border:0}.status-table td:first-child{color:#8f8a82;width:42%;text-align:right;padding-right:1rem}.button-container{display:none}.authbox{margin-top:14px}.footer{margin-top:18px;text-align:center;color:#666;font-size:.75rem}.wide{grid-column:1/-1}@media(max-width:760px){body{padding:8px}.grid{grid-template-columns:1fr}.kv{grid-template-columns:1fr}.wide{grid-column:auto}.head{align-items:flex-start}.brand h1{font-size:1.25rem}}\
</style>\
</head><body><div id='container'>\
<div class='head'><div class='brand'><a href='/'><img src='/favicon.png' alt='Home'></a><div><h1>KORVO Gateway V2.5</h1><p>WT32-ETH01 · Access / SIP Gateway</p></div></div></div>\
<div class='tabs'><a class='active' href='/'>STATUS</a><a href='/config'>GATEWAY</a><a href='/sip'>SIP / VOIP</a><a href='/diagnostic'>DIAGNOSTICO</a></div>"

#define INDEX_CHUNK_STATUS_OPEN "\
<div id='gwGrid' class='grid'>\
<div class='card'><h2>GATEWAY</h2><div id='gwIdentity'><span class='muted'>Waiting status...</span></div></div>\
<div class='card'><h2>WIFI LINK</h2><div id='gwWifi'><span class='muted'>Waiting status...</span></div></div>\
<div class='card'><h2>KORVO / IP PAIRING</h2><div id='gwEnroll'><span class='muted'>Waiting status...</span></div></div>\
<div class='card'><h2>ACCESS I/O</h2><div id='gwAccess'><span class='muted'>Waiting status...</span></div></div>\
<div class='card'><h2>SIP</h2><div id='gwSip'><span class='muted'>Waiting status...</span></div></div>\
<div class='card'><h2>BLUETOOTH / AUDIO</h2><div id='gwBt'><span class='muted'>Waiting status...</span></div></div>\
</div>\
<h2 class='section-title' style='margin-top:16px'>Bridge / Network</h2>\
<div class='status-table'><table>"

#define INDEX_CHUNK_STATUS_CLOSE "</table></div>"
#define INDEX_CHUNK_BUTTONS ""

/* Sent immediately after the Bridge table, before the login/password section.
 * This keeps the dynamic cards alive even if a later optional UI chunk fails. */
#define INDEX_CHUNK_STATUS_SCRIPT_A "\
<script>\
function kv(k,v,c){var val=(v===null||v===undefined||v==='')?'-':v;return `<div class='kv'><span class='k'>${k}</span><span class='v ${c||''}'>${val}</span></div>`;}\
function esc(v){var val=(v===null||v===undefined)?'':v;return String(val).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}\
function sigpct(r){if(r<=-90)return 3;if(r>=-35)return 100;return Math.max(3,Math.min(100,(r+90)*100/55));}\
function cardError(t){return `<span class='bad'>${t}</span>`;}\
async function gwRefresh(){try{let r=await fetch('/api/gw/status',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);let x=await r.json(),d=x.data||{},e=d.enrollment||{},w=d.wifi||{},s=d.sip||{},b=d.bluetooth||{},io=d.io||{};let l=e.local||{},o=e.observed_peer||{};\
document.getElementById('gwIdentity').innerHTML=kv('Name',esc(l.name))+kv('Host / Sala',esc(l.hostname))+kv('Location',esc(l.location))+kv('IP',esc(l.ip))+kv('MAC',esc(l.mac))+kv('Layer',esc(d.layer_version));\
let ep=e.expected_peer||{},peerConfigured=!!ep.ip,radioVisible=!!w.candidate_visible;let shownMac=w.candidate_mac||'-';let shownRssi=w.candidate_rssi_dbm||0;let shownBand=w.candidate_signal_band||'';let signalText=radioVisible?`${shownRssi} dBm · ${esc(shownBand)}`:'NO WIFI CLIENT';document.getElementById('gwWifi').innerHTML=kv('SSID',esc(w.ssid))+kv('BSSID Gateway',esc(w.bssid))+kv('Channel',`${w.channel||'-'} / ${w.frequency_mhz||'-'} MHz`)+kv('KORVO IP configured',peerConfigured?esc(ep.ip):'NOT CONFIGURED',peerConfigured?'ok':'warn')+kv('Observed client MAC',esc(shownMac),'muted')+kv('Signal',signalText,radioVisible?(shownBand==='excellent'||shownBand==='good'?'ok':shownBand==='poor'||shownBand==='weak'?'bad':'warn'):'warn')+`<div class='signal'><div style='width:${radioVisible?sigpct(shownRssi):0}%'></div></div>`+kv('Clients',w.clients);\
"

#define INDEX_CHUNK_STATUS_SCRIPT_B "\
let configured=!!(e.expected_peer&&e.expected_peer.ip);let live=!!e.session_ip_ok;let observed=(o.name||o.hostname||o.ip);let ps=(e.gateway_ok&&live)?'PAIRED':(configured?'WAITING KORVO':'NOT CONFIGURED');let cs=e.control_ok?'CONTROL OK':'CONTROL BLOCKED';document.getElementById('gwEnroll').innerHTML=kv('KORVO IP',configured?esc(e.expected_peer.ip):'NOT CONFIGURED',configured?'ok':'warn')+kv('Detected',observed?esc(observed):'-')+kv('Live source IP',o.ip?esc(o.ip):'-',live?'ok':'')+kv('State',ps,(e.gateway_ok&&live)?'ok':configured?'warn':'bad')+kv('Control',cs,e.control_ok?'ok':'bad')+kv('Authorization','RECIPROCAL IP + LIVE WS','muted');\
document.getElementById('gwAccess').innerHTML=kv('Door',esc(io.door))+kv('REX',io.rex?'ACTIVE':'OFF',io.rex?'warn':'')+kv('Relay',io.relay?'ON':'OFF',io.relay?'warn':'')+kv('Beacon',io.beacon?'ON':'OFF');\
document.getElementById('gwSip').innerHTML=kv('Engine',s.engine==='real'?'ACTIVE':'CONFIG / SIMULATION',s.engine==='real'?'ok':'warn')+kv('Registered',s.registered?'YES':'NO',s.registered?'ok':'warn')+kv('State',esc(s.state))+kv('Extension',esc(s.extension))+kv('Name',esc(s.extension_name))+kv('Central',`${esc(s.central_name)} ${esc(s.central_extension)}`)+kv('CALL mode',esc(s.call_mode));\
document.getElementById('gwBt').innerHTML=kv('Engine',b.engine==='real'?'ACTIVE':'CONFIG / SIMULATION',b.engine==='real'?'ok':'warn')+kv('Connected',b.connected?'YES':'NO',b.connected?'ok':'warn')+kv('Device',esc(b.device||'-'))+kv('Profile',esc(b.profile||'-'))+kv('Audio',b.audio_ready?'READY':'NOT READY',b.audio_ready?'ok':'warn');\
}catch(err){let m=cardError('Status API unavailable · '+esc(err.message||err));['gwIdentity','gwWifi','gwEnroll','gwAccess','gwSip','gwBt'].forEach(id=>{let el=document.getElementById(id);if(el)el.innerHTML=m;});}}\
gwRefresh();setInterval(gwRefresh,3000);\
</script>"

#define INDEX_CHUNK_TAIL "</div></body></html>"
