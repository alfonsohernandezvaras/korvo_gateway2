/* G2_R3_01_SIM_PAIR */
/* R3 diagnostic staging. No physical transport, authorization, GPIO or relay. */
static char g2r3_ip[16] = "";
static bool g2r3_loaded = false;
static uint32_t g2r3_count[2] = {0,0};
static char g2r3_last[48] = "";
static void g2r3_load(void) {
    if (g2r3_loaded) return;
    g2r3_loaded=true;
    nvs_handle_t h;
    if (nvs_open("g2r3cfg",NVS_READONLY,&h)==ESP_OK) {
        size_t len=sizeof(g2r3_ip);
        if(nvs_get_str(h,"peer_ip",g2r3_ip,&len)!=ESP_OK)g2r3_ip[0]=0;
        nvs_close(h);
    }
}
static esp_err_t g2r3_get(httpd_req_t *req) {
    if(require_auth(req)!=ESP_OK)return ESP_OK;
    g2r3_load();
    char b[320];
    snprintf(b,sizeof(b),"{\"ok\":true,\"configured_ip\":\"%s\",\"paired\":false,\"transport\":\"NOT_IMPLEMENTED\",\"mode\":\"SIMULATION_ONLY\",\"readers\":[%lu,%lu],\"last\":\"%s\"}",g2r3_ip,(unsigned long)g2r3_count[0],(unsigned long)g2r3_count[1],g2r3_last);
    return send_json(req,b);
}
static esp_err_t g2r3_post(httpd_req_t *req) {
    if(require_auth(req)!=ESP_OK)return ESP_OK;
    char body[240],action[20]={0};
    if(!recv_form(req,body,sizeof(body)) || !form_value(body,"action",action,sizeof(action)))return send_json(req,"{\"ok\":false,\"error\":\"invalid_body\"}");
    g2r3_load();
    if(!strcmp(action,"save_ip")) {
        char ip[32]={0};
        if(!form_value(body,"ip",ip,sizeof(ip))||!valid_ipv4_text(ip)||strlen(ip)>=sizeof(g2r3_ip))return send_json(req,"{\"ok\":false,\"error\":\"invalid_ip\"}");
        nvs_handle_t h;esp_err_t e=nvs_open("g2r3cfg",NVS_READWRITE,&h);
        if(e!=ESP_OK)return send_json(req,"{\"ok\":false,\"error\":\"nvs_open\"}");
        e=nvs_set_str(h,"peer_ip",ip);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
        if(e!=ESP_OK)return send_json(req,"{\"ok\":false,\"error\":\"nvs_save\"}");
        snprintf(g2r3_ip,sizeof(g2r3_ip),"%s",ip);
        return g2r3_get(req);
    }
    if(!strcmp(action,"simulate")) {
        char reader[8]={0},format[8]={0},card[40]={0};
        if(!form_value(body,"reader",reader,sizeof(reader))||!form_value(body,"format",format,sizeof(format))||!form_value(body,"card",card,sizeof(card)))return send_json(req,"{\"ok\":false,\"error\":\"missing_fields\"}");
        if((strcmp(reader,"1")&&strcmp(reader,"2"))||(strcmp(format,"26")&&strcmp(format,"34")))return send_json(req,"{\"ok\":false,\"error\":\"invalid_reader_or_format\"}");
        size_t n=strlen(card);if(!n||n>30)return send_json(req,"{\"ok\":false,\"error\":\"invalid_card\"}");
        for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)card[i])&&card[i]!='-'&&card[i]!='_')return send_json(req,"{\"ok\":false,\"error\":\"invalid_card\"}");
        unsigned idx=(unsigned)(reader[0]-'1');g2r3_count[idx]++;
        snprintf(g2r3_last,sizeof(g2r3_last),"R%s W%s %s",reader,format,card);
        ESP_LOGI(TAG,"R3 SIMULATED reader=%s format=%s card=%s (NO GPIO/RELAY)",reader,format,card);
        return g2r3_get(req);
    }
    return send_json(req,"{\"ok\":false,\"error\":\"unsupported_action\"}");
}
static esp_err_t g2r3_page(httpd_req_t *req) {
    if(require_auth(req)!=ESP_OK)return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
"<!doctype html><html lang='es'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Gateway2 R3</title><style>body{font:15px Arial;background:#17140f;color:#eee;margin:22px}.wrap{max-width:960px;margin:auto}.card{background:#242018;border:1px solid #63502a;border-radius:9px;padding:18px;margin:12px 0}h2{color:#ffc857}input,select{padding:9px;background:#15120f;color:white;border:1px solid #75613c;border-radius:5px}button{background:#ffc857;border:0;border-radius:5px;padding:10px;cursor:pointer}a{color:#ffc857}.warn{color:#ffd47a}</style><div class='wrap'><a href='/'>INICIO</a> · <a href='/dictionary'>DICCIONARIO</a> · <a href='/gpio-map'>GPIO</a><h2>R3 · PAIRING Y SIMULADOR</h2><p class='warn'>ETAPA PREPARATORIA: configurar IP no establece pairing real. El R3 fisico no esta conectado. SIMULAR no abre puertas ni activa GPIO.</p><div class='card'><h3>R3 / Red</h3><label>IP esperada <input id='ip' placeholder='192.168.10.60'></label> <button id='save'>GUARDAR IP</button><p id='pair'>Cargando...</p></div><div class='card'><h3>Lectores Wiegand (simulados)</h3><label>Lector <select id='reader'><option value='1'>Lector 1 · D2/D3</option><option value='2'>Lector 2 · D4/D5</option></select></label> <label>Formato <select id='format'><option value='26'>26 bits</option><option value='34'>34 bits</option></select></label> <label>Tarjeta <input id='card' maxlength='30' value='123456'></label> <button id='sim'>SIMULAR TARJETA</button><p id='counts'></p></div><div class='card'><h3>Registro de prueba</h3><pre id='log'>Sin eventos</pre></div></div><script>const E=id=>document.getElementById(id);async function api(body){const r=await fetch('/api/gw/r3',{method:body?'POST':'GET',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body?new URLSearchParams(body):undefined,cache:'no-store'});const d=await r.json();if(!d.ok)throw Error(d.error||'HTTP '+r.status);return d}function render(d){E('ip').value=d.configured_ip;E('pair').textContent='Pairing: NO CONECTADO · Transporte pendiente · IP: '+(d.configured_ip||'SIN CONFIGURAR');E('counts').textContent='Lector 1: '+d.readers[0]+' simulaciones · Lector 2: '+d.readers[1]+' simulaciones'}E('save').onclick=async()=>{try{render(await api({action:'save_ip',ip:E('ip').value.trim()}));E('log').textContent='IP guardada en NVS\n'+E('log').textContent}catch(e){E('log').textContent=e.message}};E('sim').onclick=async()=>{try{let d=await api({action:'simulate',reader:E('reader').value,format:E('format').value,card:E('card').value.trim()});render(d);E('log').textContent=new Date().toLocaleTimeString()+' · SIMULADO · '+d.last+' · SIN ACCION FISICA\n'+E('log').textContent}catch(e){E('log').textContent=e.message}};api().then(render).catch(e=>E('pair').textContent=e.message)</script></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}
static const httpd_uri_t URI_R3_PAGE={.uri="/r3",.method=HTTP_GET,.handler=g2r3_page};
static const httpd_uri_t URI_R3_GET={.uri="/api/gw/r3",.method=HTTP_GET,.handler=g2r3_get};
static const httpd_uri_t URI_R3_POST={.uri="/api/gw/r3",.method=HTTP_POST,.handler=g2r3_post};
