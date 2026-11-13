    httpd_resp_sendstr_chunk(req, R"UI02(
<style>
/* UI02: mantener placa original, comprimir margenes, tarjetas y filas */
.gp-layout{grid-template-columns:minmax(260px,1fr) 165px minmax(260px,1fr)!important;gap:7px!important}
.gp-photo img{width:165px!important;height:390px!important}
.gp-side{grid-template-rows:repeat(13,30px)!important}
.gp-row{height:30px!important;gap:3px!important;font-size:11px!important}
.gp-pin{min-width:48px!important;padding:4px 2px!important;font-size:10px!important}
.gp-row select{font-size:10px!important;min-width:55px!important;padding:3px!important}
.gp-row button{font-size:9px!important;padding:3px!important}
.gp-row .gp-enable{width:13px!important;min-width:13px!important}
.gp-actions{margin:7px 0!important;gap:6px!important}
.gp-bottom{gap:7px!important}.gp-test{padding:8px!important}
.gp-status{padding:6px!important;margin:5px 0!important}
.gp-note{margin-top:5px!important}
.ui02-stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;margin:8px 0}
.ui02-stat,.ui02-rfid{background:#242018;border:1px solid #66512b;border-radius:7px;padding:9px;color:#e8e5df}
.ui02-stat b{display:block;font-size:11px;color:#ffbe23}.ui02-stat strong{display:block;margin-top:5px;font-size:16px}
.ui02-stat small{font-size:10px;color:#aaa}.ui02-rfid{margin:8px 0;display:flex;align-items:center;flex-wrap:wrap;gap:8px}
.ui02-rfid input,.ui02-rfid select{background:#17140f;color:white;border:1px solid #66512b;padding:6px;border-radius:4px}
.ui02-rfid button{background:#ffb300;color:#17140f;border:0;border-radius:5px;padding:8px;font-weight:bold}
.ui02-rfid .ui02-head{flex-basis:100%;font-weight:bold;color:#ffb300}
@media(max-width:700px){.ui02-stats{grid-template-columns:repeat(2,minmax(0,1fr))}}
</style>
<script>
(()=>{'use strict';
const $=id=>document.getElementById(id);
const host=document.querySelector('.gp-layout');
if(!host)return;
const stats=document.createElement('div');stats.className='ui02-stats';
stats.innerHTML='<div class="ui02-stat"><b>CPU</b><strong id="ui02cpu">—</strong><small>Frecuencia; carga por núcleo pendiente</small></div><div class="ui02-stat"><b>HEAP LIBRE / MÍNIMO</b><strong id="ui02ram">—</strong><small>Memoria dinámica</small></div><div class="ui02-stat"><b>FLASH FÍSICA</b><strong id="ui02flash">—</strong><small>Capacidad instalada</small></div><div class="ui02-stat"><b>TIEMPO ACTIVO</b><strong id="ui02up">—</strong><small>Desde último arranque</small></div>';
host.before(stats);
const rfid=document.createElement('div');rfid.className='ui02-rfid';
rfid.innerHTML='<div class="ui02-head">SIMULADOR RFID UNIVERSAL · solo evento lógico, NO abre puerta</div><label>Lector <select id="ui02reader"><option value="1">1</option><option value="2">2</option></select></label><label>ID <input id="ui02id" maxlength="40" value="000123456789" autocomplete="off"></label><button type="button" id="ui02send">SIMULAR LECTURA</button><span id="ui02result"></span>';
const bottom=document.querySelector('.gp-bottom');if(bottom){bottom.before(rfid);const old=bottom.querySelector('.gp-test');if(old)old.style.display='none'}else host.after(rfid);
// Old Wiegand mapping is shown as legacy words; pins are no longer Wiegand inputs.
const hint=document.createElement('div');hint.style.cssText='font-size:11px;color:#b8b8b8;margin:5px 0';hint.textContent='GPIO35 / 36 / 5 / 17: liberados de captura Wiegand. Etiquetas de diccionario antiguo aún reservadas; UART RFID sin asignar.';rfid.after(hint);
async function poll(){try{const r=await fetch('/api/gw/rfid01/resources',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const j=await r.json();$('ui02cpu').textContent=j.cpu_mhz+' MHz';$('ui02ram').textContent=(j.heap_free/1024).toFixed(0)+' / '+(j.heap_min/1024).toFixed(0)+' KB';$('ui02flash').textContent=(j.flash_bytes/1048576).toFixed(2)+' MB';let s=Math.floor(j.uptime_s);$('ui02up').textContent=Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s'}catch(e){$('ui02cpu').textContent='Sin datos';}}
$('ui02send').onclick=async()=>{const id=$('ui02id').value.trim(),reader=$('ui02reader').value;if(!/^[A-Za-z0-9:_-]{1,40}$/.test(id)){$('ui02result').textContent='ID inválido';return}try{const r=await fetch('/api/gw/diag',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({action:'rfid01',reader,id})});const j=await r.json();if(!r.ok||!j.ok)throw Error(j.error||'Rechazado');$('ui02result').textContent='Evento generado';const log=$('gpLog');if(log){const line=document.createElement('div');line.textContent=new Date().toLocaleTimeString()+' | RFID SIM lector '+reader+' | '+id;log.prepend(line)}}catch(e){$('ui02result').textContent='ERROR: '+e.message}};
poll();setInterval(poll,3000);
})();
</script>
)UI02");
