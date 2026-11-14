    httpd_resp_sendstr_chunk(req, R"RFID03(
<style>
/* RFID02_UI03: compactacion solo de la pantalla /gpio-map */
.gp-wrap{margin:0!important;padding:0!important;max-width:none!important}
.gp-layout{grid-template-columns: minmax(220px,330px) 165px minmax(300px,1fr)!important;justify-content:start!important;gap:5px!important}
.gp-photo img{width:165px!important;height:390px!important}
.gp-row{font-size:10px!important;gap:3px!important}
.gp-row .gp-label{font-size:10px!important;min-width:70px!important}
.gp-row select{max-width:185px!important}
.gp-side{grid-template-rows:repeat(13,30px)!important}
.gp-row{height:30px!important}
.ui02-stats{grid-template-columns:repeat(5,minmax(0,1fr))!important;gap:5px!important;margin:4px 0!important}
.ui02-stat{padding:5px 8px!important;min-height:57px!important}
.ui02-stat strong{font-size:13px!important;margin-top:2px!important}
.ui02-rfid{padding:6px!important;margin:5px 0!important}
.v65-extras{gap:6px!important}
.v65-card,.card,.gp-test{padding:7px!important}
.v65-card h2,.card h2{font-size:13px!important;margin:0 0 6px!important}
.v65-buttons{margin:5px 0!important;gap:4px!important}
.v65-card button{padding:5px 7px!important;font-size:11px!important}
.gp-bottom{gap:5px!important}
.gp-row.rfid-freed .gp-pin{background:#465a4b!important;color:#d6f5df!important}
.gp-row.rfid-freed .gp-label{color:#8ed5a3!important}
.rfid03-storage{background:#242018;border:1px solid #66512b;border-radius:7px;padding:7px;margin:5px 0;font-size:11px}
.rfid03-storage b{color:#ffbe23}
.rfid03-storage .rfid03-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-top:5px}
.rfid03-storage strong{display:block;font-size:14px;margin-top:3px}
@media(max-width:950px){.gp-layout{overflow-x:auto!important}.ui02-stats{grid-template-columns:repeat(2,minmax(0,1fr))!important}.rfid03-storage .rfid03-grid{grid-template-columns:repeat(2,1fr)}}
</style>
<script>
(function(){
'use strict';
const $=id=>document.getElementById(id);
function title(el){const h=el.querySelector('h2,h3');return h?(h.textContent||'').trim().toUpperCase():'';}
function clean(){
  // Eliminar cards duplicadas de lectores Wiegand y BT/WiFi solo en esta pagina.
  document.querySelectorAll('.v65-card').forEach(el=>{
    const t=title(el);
    if(t.includes('LECTORES WIEGAND')||t.includes('BLUETOOTH / AUDIO')||t.includes('EMPAREJAMIENTO / WIFI'))el.remove();
  });
  // Remover cualquier contenedor vacio que quedo tras retirar las cards.
  document.querySelectorAll('.v65-extras').forEach(el=>{if(!el.querySelector('.v65-card'))el.remove();});
  // Pines Wiegand antiguos: libres, no se muestran como lector ni simulador.
  const freed={'RXD':'GPIO5 · LIBRE','TXD':'GPIO17 · LIBRE','IO35':'GPIO35 · LIBRE','IO36':'GPIO36 · LIBRE'};
  document.querySelectorAll('.gp-row').forEach(row=>{
    const pin=row.querySelector('.gp-pin');
    if(!pin)return;
    const key=pin.textContent.trim();
    if(!Object.prototype.hasOwnProperty.call(freed,key))return;
    // TXD/RXD de UART0 consola no tienen controles y NO deben ocultarse.
    if(!row.querySelector('.gp-enable'))return;
    row.classList.add('rfid-freed');
    row.querySelectorAll('select,button,.gp-enable').forEach(el=>{el.style.display='none';});
    const label=row.querySelector('.gp-label');if(label)label.textContent=freed[key]+' · UART RX por definir';
  });
  // Etiquetas de alimentación duplicadas que el usuario solicito retirar.
  document.querySelectorAll('.gp-row.inactive .gp-label').forEach(el=>{el.style.display='none';});
  // Quitar referencia a pulsos Wiegand en el texto informativo de UI02.
  document.querySelectorAll('.ui02-rfid + div').forEach(el=>{
    if(el.textContent.includes('liberados de captura Wiegand'))el.textContent='GPIO5/17/35/36 libres. UART RX del controlador de lectoras pendiente de asignacion y prueba electrica.';
  });
}
function storage(){
  const stats=document.querySelector('.ui02-stats');
  if(!stats||$('rfid03-storage'))return;
  const box=document.createElement('div');box.className='rfid03-storage';box.id='rfid03-storage';
  box.innerHTML='<b>ALMACENAMIENTO DE MAQUETAS · GATEWAY2 / RESPALDO KORVO SD</b><div class="rfid03-grid"><div>Partición NVS: entradas libres<strong id="r03free">—</strong></div><div>Entradas NVS utilizadas<strong id="r03used">—</strong></div><div>Archivos de credenciales<strong>NO CREADOS</strong></div><div>Respaldo SD KORVO<strong>PENDIENTE</strong></div></div><small>La NVS no es un sistema de archivos: las entradas no equivalen a bytes libres. Tamaño de archivos y copia SD se habilitarán al implementar almacenamiento y sincronización.</small>';
  stats.after(box);
  async function update(){try{
    const r=await fetch('/api/gw/rfid01/resources',{cache:'no-store'});if(!r.ok)return;
    const d=await r.json();
    $('r03free').textContent=d.nvs_free_entries===null?'No disponible':String(d.nvs_free_entries);
    $('r03used').textContent=d.nvs_used_entries===null?'No disponible':String(d.nvs_used_entries);
  }catch(e){}}
  update();setInterval(update,5000);
}
let tries=0;
const timer=setInterval(()=>{clean();storage();if(++tries>=6)clearInterval(timer);},600);
clean();storage();
})();
</script>
)RFID03");
