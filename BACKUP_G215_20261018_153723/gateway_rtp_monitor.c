/* G2-14 R1: receive-only RTP PCMU (G.711 u-law) + browser monitor.
   No microphone, I2S, DAC or RTP transmission. */
#include "gateway_rtp_monitor.h"
#include "http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define RTP_PORT 40000
#define FIFO_CAP 8192
static const char *TAG="g2_rtp";
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t fifo[FIFO_CAP];
static size_t head, tail, used;
static bool active, started;
static uint32_t packets, lost, dropped, last_seq;
static bool have_seq;

void gateway_rtp_monitor_active(bool enable) {
    portENTER_CRITICAL(&lock);
    active=enable;
    head=tail=used=0;
    have_seq=false;
    if(enable){packets=lost=dropped=0;}
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG,"G2-14 RTP RX %s UDP %u",enable?"ACTIVE":"OFF",RTP_PORT);
}
static void receiver(void *unused) {
    (void)unused;
    uint8_t buf[1600];
    for(;;){
        int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        struct sockaddr_in addr={0};
        addr.sin_family=AF_INET;addr.sin_port=htons(RTP_PORT);addr.sin_addr.s_addr=INADDR_ANY;
        if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))!=0){
            ESP_LOGE(TAG,"RTP bind UDP %u errno=%d",RTP_PORT,errno);
            close(fd);vTaskDelay(pdMS_TO_TICKS(2000));continue;
        }
        ESP_LOGI(TAG,"G2-14 RTP PCMU listening UDP %u",RTP_PORT);
        for(;;){
            int n=recvfrom(fd,buf,sizeof(buf),0,NULL,NULL);
            if(n<12)continue;
            if((buf[0]>>6)!=2 || (buf[1]&0x7f)!=0)continue;
            int cc=buf[0]&15;
            int off=12+4*cc;
            if(n<off)continue;
            if(buf[0]&0x10){
                if(n<off+4)continue;
                int words=(buf[off+2]<<8)|buf[off+3];
                off+=4+words*4;
            }
            int end=n;
            if(buf[0]&0x20){int pad=buf[n-1];if(pad<1||pad>n-off)continue;end-=pad;}
            if(off>=end)continue;
            uint16_t seq=((uint16_t)buf[2]<<8)|buf[3];
            portENTER_CRITICAL(&lock);
            if(active){
                if(have_seq){
                    uint16_t delta=(uint16_t)(seq-(uint16_t)last_seq);
                    if(delta>1 && delta<1000)lost+=(uint32_t)(delta-1);
                }
                last_seq=seq;have_seq=true;packets++;
                for(int i=off;i<end;i++){
                    if(used==FIFO_CAP){tail=(tail+1)%FIFO_CAP;used--;dropped++;}
                    fifo[head]=buf[i];head=(head+1)%FIFO_CAP;used++;
                }
            }
            portEXIT_CRITICAL(&lock);
        }
    }
}
void gateway_rtp_monitor_start(void){
    if(started)return;
    if(xTaskCreate(receiver,"g2_rtp_rx",4096,NULL,4,NULL)==pdPASS)started=true;
}
static bool auth(httpd_req_t *req){
    if(http_server_request_authenticated(req))return true;
    httpd_resp_set_status(req,"401 Unauthorized");
    httpd_resp_sendstr(req,"Authentication required");
    return false;
}
static esp_err_t status_get(httpd_req_t *req){
    if(!auth(req))return ESP_FAIL;
    uint32_t p,l,d;size_t q;bool a;
    portENTER_CRITICAL(&lock);
    p=packets;l=lost;d=dropped;q=used;a=active;
    portEXIT_CRITICAL(&lock);
    char s[192];
    snprintf(s,sizeof(s),"{\"active\":%s,\"packets\":%lu,\"lost\":%lu,\"dropped_bytes\":%lu,\"queued_bytes\":%u,\"codec\":\"PCMU\",\"port\":40000}",
             a?"true":"false",(unsigned long)p,(unsigned long)l,(unsigned long)d,(unsigned)q);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,s);
}
static esp_err_t audio_get(httpd_req_t *req){
    if(!auth(req))return ESP_FAIL;
    uint8_t out[960];size_t n=0;
    portENTER_CRITICAL(&lock);
    while(n<sizeof(out) && used){out[n++]=fifo[tail];tail=(tail+1)%FIFO_CAP;used--;}
    portEXIT_CRITICAL(&lock);
    httpd_resp_set_type(req,"application/octet-stream");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char*)out,n);
}
static const char PAGE[] =
"<!doctype html><html lang='es'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>G2-14 RTP Monitor</title><style>body{background:#17140f;color:#eee;font:16px Segoe UI,Arial;margin:28px;max-width:720px}h1{color:#ffb300}"
"button{background:#ffb300;color:#211900;border:0;border-radius:8px;padding:12px 20px;font-weight:bold;cursor:pointer}"
".box{background:#28231b;border:1px solid #5a4720;padding:18px;border-radius:10px;margin-top:18px}a{color:#ffb300}"
"</style><a href='/sip'>Volver a SIP</a><h1>G2-14 · AUDIO SIP / RTP RX</h1>"
"<div class='box'><p>Linphone 300 → Gateway2 200 → navegador → Windows → Bose.</p>"
"<button id='toggle'>ESCUCHAR</button> <span id='message'>Detenido</span>"
"<p id='stats'>Esperando datos...</p><p>Volumen PC: <input id='vol' type='range' min='0' max='2' value='1' step='0.05'></p>"
"<p>Requiere llamada CONNECTED y PCMU / RTP sin cifrar. Audio solo de Linphone al PC.</p></div>"
"<script>"
"let ctx=null,gain=null,playing=false,nextTime=0,timer=null,busy=false;"
"const $=id=>document.getElementById(id);"
"function ulaw(u){u=(~u)&255;let t=((u&15)<<3)+132;t<<=(u>>4)&7;return ((u&128)?(132-t):(t-132))/32768;}"
"async function poll(){"
"try{let s=await(await fetch('/api/gw/rtp/status',{cache:'no-store'})).json();"
"$('stats').textContent='RTP: '+(s.active?'ACTIVO':'INACTIVO')+' | paquetes: '+s.packets+' | perdidos: '+s.lost+' | cola: '+s.queued_bytes+' bytes | descartados: '+s.dropped_bytes;"
"if(!playing||busy||!s.active)return;busy=true;"
"let r=await fetch('/api/gw/rtp/audio',{cache:'no-store'});if(!r.ok)return;"
"let b=new Uint8Array(await r.arrayBuffer());if(!b.length)return;"
"let a=ctx.createBuffer(1,b.length,8000),v=a.getChannelData(0);for(let i=0;i<b.length;i++)v[i]=ulaw(b[i]);"
"let src=ctx.createBufferSource();src.buffer=a;src.connect(gain);"
"if(nextTime<ctx.currentTime+0.12||nextTime>ctx.currentTime+0.55)nextTime=ctx.currentTime+0.12;"
"src.start(nextTime);nextTime+=b.length/8000;"
"}catch(e){$('message').textContent='Error: '+e.message;}finally{busy=false;}}"
"$('toggle').onclick=async()=>{if(playing){playing=false;clearInterval(timer);timer=null;if(ctx){await ctx.close();ctx=null;}nextTime=0;$('toggle').textContent='ESCUCHAR';$('message').textContent='Detenido';return;}"
"ctx=new (window.AudioContext||window.webkitAudioContext)();await ctx.resume();gain=ctx.createGain();gain.gain.value=Number($('vol').value);gain.connect(ctx.destination);"
"playing=true;nextTime=0;$('toggle').textContent='DETENER';$('message').textContent='Monitor activo';timer=setInterval(poll,80);poll();};"
"$('vol').oninput=()=>{if(gain)gain.gain.value=Number($('vol').value);};"
"setInterval(()=>{if(!playing)poll();},1000);poll();"
"</script></html>";
static esp_err_t page_get(httpd_req_t *req){
    if(!auth(req))return ESP_FAIL;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,PAGE,HTTPD_RESP_USE_STRLEN);
}
esp_err_t gateway_rtp_monitor_register(httpd_handle_t server){
    const httpd_uri_t u1={.uri="/audio-monitor",.method=HTTP_GET,.handler=page_get};
    const httpd_uri_t u2={.uri="/api/gw/rtp/status",.method=HTTP_GET,.handler=status_get};
    const httpd_uri_t u3={.uri="/api/gw/rtp/audio",.method=HTTP_GET,.handler=audio_get};
    esp_err_t e=httpd_register_uri_handler(server,&u1);
    if(e!=ESP_OK)return e;
    e=httpd_register_uri_handler(server,&u2);if(e!=ESP_OK)return e;
    return httpd_register_uri_handler(server,&u3);
}
