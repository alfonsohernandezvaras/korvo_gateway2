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
#include "esp_random.h"
#include "freertos/queue.h"
#include "gateway_services.h"

#define RTP_PORT 40000
#define FIFO_CAP 8192
static const char *TAG="g2_rtp";
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t fifo[FIFO_CAP];
static size_t head, tail, used;
static bool active, started;
static uint32_t packets, lost, dropped, last_seq;
static bool have_seq;
static bool auth(httpd_req_t *req);
static struct sockaddr_in tx_peer;
static bool tx_peer_valid;
static QueueHandle_t tx_queue;
/* RX y TX comparten el puerto anunciado en SDP: UDP 40000. */
static int rtp_socket_fd = -1;
static uint32_t tx_packets,tx_errors,tx_overruns;
typedef struct {uint8_t bytes[160];} tx_audio_t;
void gateway_rtp_monitor_remote(const char *ip,uint16_t port){
    struct sockaddr_in peer={0};peer.sin_family=AF_INET;peer.sin_port=htons(port);
    bool valid=ip&&port>=1024&&inet_pton(AF_INET,ip,&peer.sin_addr)==1;
    portENTER_CRITICAL(&lock);tx_peer=peer;tx_peer_valid=valid;portEXIT_CRITICAL(&lock);
}
static void tx_task(void *unused){
    (void)unused;
    /* No crear socket efimero TX: MicroSIP puede responder a ese puerto. */
    uint16_t seq=(uint16_t)esp_random();uint32_t ts=esp_random(),ssrc=esp_random();
    tx_audio_t frame;TickType_t tick=xTaskGetTickCount();
    for(;;){
        if(xQueueReceive(tx_queue,&frame,pdMS_TO_TICKS(100))!=pdTRUE){tick=xTaskGetTickCount();continue;}
        vTaskDelayUntil(&tick,pdMS_TO_TICKS(20));
        struct sockaddr_in dest;bool ok;
        portENTER_CRITICAL(&lock);dest=tx_peer;ok=active&&tx_peer_valid;portEXIT_CRITICAL(&lock);
        gateway_sip_status_t st;gateway_sip_get_status(&st);
        if(!ok||st.state!=GW_SIP_CONNECTED)continue;
        uint8_t pkt[172]={0x80,0x00};
        pkt[2]=seq>>8;pkt[3]=seq;pkt[4]=ts>>24;pkt[5]=ts>>16;pkt[6]=ts>>8;pkt[7]=ts;
        pkt[8]=ssrc>>24;pkt[9]=ssrc>>16;pkt[10]=ssrc>>8;pkt[11]=ssrc;
        memcpy(pkt+12,frame.bytes,160);
        int fd;
        portENTER_CRITICAL(&lock);fd=rtp_socket_fd;portEXIT_CRITICAL(&lock);
        int n=(fd>=0)?sendto(fd,pkt,sizeof(pkt),0,(struct sockaddr*)&dest,sizeof(dest)):-1;
        portENTER_CRITICAL(&lock);if(n==(int)sizeof(pkt))tx_packets++;else tx_errors++;portEXIT_CRITICAL(&lock);
        seq++;ts+=160;
    }
}
static esp_err_t tx_ws(httpd_req_t *req){
    if(req->method==HTTP_GET)return auth(req)?ESP_OK:ESP_FAIL;
    if(!auth(req))return ESP_FAIL;
    httpd_ws_frame_t f={0};esp_err_t e=httpd_ws_recv_frame(req,&f,0);
    if(e!=ESP_OK)return e;
    if(f.type!=HTTPD_WS_TYPE_BINARY||f.len!=160)return ESP_OK;
    tx_audio_t data;f.payload=data.bytes;
    e=httpd_ws_recv_frame(req,&f,160);if(e!=ESP_OK)return e;
    gateway_sip_status_t st;gateway_sip_get_status(&st);
    bool ok;portENTER_CRITICAL(&lock);ok=active&&tx_peer_valid;portEXIT_CRITICAL(&lock);
    if(ok&&st.state==GW_SIP_CONNECTED&&tx_queue&&xQueueSend(tx_queue,&data,0)!=pdTRUE){
        portENTER_CRITICAL(&lock);tx_overruns++;portEXIT_CRITICAL(&lock);
    }
    return ESP_OK;
}


void gateway_rtp_monitor_active(bool enable) {
    portENTER_CRITICAL(&lock);
    active=enable;
    head=tail=used=0;
    have_seq=false;
    if(!enable)tx_peer_valid=false;
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
        portENTER_CRITICAL(&lock);rtp_socket_fd=fd;portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG,"G2-15 R3 RTP RX/TX shared UDP %u",RTP_PORT);
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
    if(!tx_queue)tx_queue=xQueueCreate(12,sizeof(tx_audio_t));
    if(tx_queue)xTaskCreate(tx_task,"g2_rtp_tx",4096,NULL,4,NULL);
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
    char s[320];
    uint32_t tp,te,to;portENTER_CRITICAL(&lock);tp=tx_packets;te=tx_errors;to=tx_overruns;portEXIT_CRITICAL(&lock);
    snprintf(s,sizeof(s),"{\"active\":%s,\"packets\":%lu,\"lost\":%lu,\"dropped_bytes\":%lu,\"queued_bytes\":%u,\"codec\":\"PCMU\",\"port\":40000,\"tx_packets\":%lu,\"tx_errors\":%lu,\"tx_overruns\":%lu}",
             a?"true":"false",(unsigned long)p,(unsigned long)l,(unsigned long)d,(unsigned)q,(unsigned long)tp,(unsigned long)te,(unsigned long)to);
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
"<!doctype html><html lang=\"es\"><meta charset=\"utf-8\"><title>G2-17 Audio Monitor</title>\n"
"<style>body{background:#17140f;color:#eee;font:16px Segoe UI,Arial;margin:28px;max-width:720px}h1{color:#ffb300}button{background:#ffb300;color:#211900;border:0;border-radius:8px;padding:12px 20px;font-weight:bold;cursor:pointer;margin:5px}.box{background:#28231b;border:1px solid #5a4720;padding:18px;border-radius:10px;margin-top:18px}a{color:#ffb300}</style>\n"
"<a href=\"/sip\">Volver a SIP</a><h1>G2-17 \u00b7 AUDIO SIP / RTP DIAGNOSTICO</h1>\n"
"<div class=\"box\"><p>MicroSIP \u2194 Gateway2 200 \u2194 Chrome Windows.</p><button id=\"listen\">ESCUCHAR</button><span id=\"msg\">Detenido</span><p id=\"stats\">Esperando RTP...</p><p>Volumen PC: <input id=\"vol\" type=\"range\" min=\"0\" max=\"2\" value=\"1\" step=\"0.05\"></p><hr><button id=\"mic\">ACTIVAR MICROFONO</button><span id=\"micmsg\">Detenido</span><p>Nivel mic: <meter id=\"level\" min=\"0\" max=\"1\" value=\"0\"></meter></p><p>Requiere llamada CONNECTED y permiso de microfono de Chrome (HTTPS o contexto seguro).</p></div>\n"
"<script>\n"
"let ctx=null,gain=null,playing=false,nextTime=0,rxTimer=null,statusTimer=null,rxBusy=false,statusBusy=false,rxFails=0;\n"
"let micCtx=null,stream=null,srcMic=null,proc=null,ws=null,micOn=false,acc=[],pos=0,wsRetry=null;\n"
"const $=id=>document.getElementById(id);\n"
"function ulaw(u){u=(~u)&255;let t=((u&15)<<3)+132;t<<=(u>>4)&7;return ((u&128)?(132-t):(t-132))/32768;}\n"
"function encode(v){let x=Math.min(32635,Math.floor(Math.abs(v)*32768)),sign=v<0?0x80:0;x+=132;let exp=7;for(let mask=0x4000;exp>0&&!(x&mask);mask>>=1)exp--;return (~(sign|(exp<<4)|((x>>(exp+3))&15)))&255;}\n"
"async function fetchTimeout(path,ms){let c=new AbortController(),t=setTimeout(()=>c.abort(),ms);try{return await fetch(path,{cache:'no-store',signal:c.signal});}finally{clearTimeout(t);}}\n"
"async function statusPoll(){if(statusBusy)return;statusBusy=true;try{let r=await fetchTimeout('/api/gw/rtp/status',1800);if(!r.ok)throw Error('HTTP '+r.status);let s=await r.json();$('stats').textContent='RTP '+(s.active?'ACTIVO':'INACTIVO')+' | RX: '+s.packets+' | perdidos: '+s.lost+' | cola: '+s.queued_bytes+' | TX: '+s.tx_packets+' | errores: '+s.tx_errors+' | descartes TX: '+s.tx_overruns;}catch(e){$('stats').textContent='Servidor HTTP sin respuesta; reintentando automaticamente';}finally{statusBusy=false;}}\n"
"async function rxPoll(){if(!playing||rxBusy||!ctx)return;rxBusy=true;try{let r=await fetchTimeout('/api/gw/rtp/audio',1500);if(!r.ok)throw Error('HTTP '+r.status);let b=new Uint8Array(await r.arrayBuffer());rxFails=0;if(!b.length)return;let a=ctx.createBuffer(1,b.length,8000),v=a.getChannelData(0);for(let i=0;i<b.length;i++)v[i]=ulaw(b[i]);let src=ctx.createBufferSource();src.buffer=a;src.connect(gain);if(nextTime<ctx.currentTime+0.10||nextTime>ctx.currentTime+0.65)nextTime=ctx.currentTime+0.13;src.start(nextTime);nextTime+=b.length/8000;$('msg').textContent='Monitor activo';}catch(e){rxFails++;$('msg').textContent='Audio RX: reconectando ('+rxFails+')';nextTime=0;}finally{rxBusy=false;}}\n"
"$('listen').onclick=async()=>{if(playing){playing=false;clearInterval(rxTimer);rxTimer=null;if(ctx)await ctx.close();ctx=null;nextTime=0;$('listen').textContent='ESCUCHAR';$('msg').textContent='Detenido';return;}ctx=new AudioContext();await ctx.resume();gain=ctx.createGain();gain.gain.value=Number($('vol').value);gain.connect(ctx.destination);playing=true;$('listen').textContent='DETENER';$('msg').textContent='Monitor activo';rxTimer=setInterval(rxPoll,120);rxPoll();};\n"
"$('vol').oninput=()=>{if(gain)gain.gain.value=Number($('vol').value);};\n"
"function openTx(){if(!micOn)return;let socket=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws/audio-tx');ws=socket;socket.binaryType='arraybuffer';socket.onopen=()=>{if(ws===socket)$('micmsg').textContent='Audio TX activo';};socket.onerror=()=>{if(ws===socket)$('micmsg').textContent='WebSocket TX: reconectando';};socket.onclose=()=>{if(!micOn||ws!==socket)return;ws=null;$('micmsg').textContent='WebSocket TX: reconectando';clearTimeout(wsRetry);wsRetry=setTimeout(openTx,2500);};}\n"
"async function stopMic(){micOn=false;clearTimeout(wsRetry);if(proc){proc.onaudioprocess=null;proc.disconnect();}if(srcMic)srcMic.disconnect();if(stream)stream.getTracks().forEach(t=>t.stop());if(ws)ws.close();if(micCtx)await micCtx.close();proc=srcMic=stream=ws=micCtx=null;acc=[];pos=0;$('mic').textContent='ACTIVAR MICROFONO';$('micmsg').textContent='Detenido';$('level').value=0;}\n"
"$('mic').onclick=async()=>{if(micOn){await stopMic();return;}try{if(!navigator.mediaDevices?.getUserMedia)throw Error('Chrome exige HTTPS o localhost para el microfono');stream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true}});micCtx=new AudioContext();await micCtx.resume();micOn=true;openTx();srcMic=micCtx.createMediaStreamSource(stream);proc=micCtx.createScriptProcessor(2048,1,1);pos=0;proc.onaudioprocess=e=>{let x=e.inputBuffer.getChannelData(0),step=micCtx.sampleRate/8000,sum=0;for(let v of x)sum+=v*v;$('level').value=Math.min(1,Math.sqrt(sum/x.length)*4);while(pos<x.length){let k=Math.floor(pos),frac=pos-k;acc.push(encode(x[k]*(1-frac)+x[Math.min(k+1,x.length-1)]*frac));pos+=step;if(acc.length===160){if(ws&&ws.readyState===1&&ws.bufferedAmount<1600)ws.send(new Uint8Array(acc));acc=[];}}pos-=x.length;};srcMic.connect(proc);let silent=micCtx.createGain();silent.gain.value=0;proc.connect(silent);silent.connect(micCtx.destination);$('mic').textContent='DETENER MICROFONO';$('micmsg').textContent='Conectando WebSocket TX';}catch(e){let message=e.message;await stopMic();$('micmsg').textContent='Error: '+message;}};\n"
"window.addEventListener('pagehide',()=>{if(micOn)stopMic();});statusTimer=setInterval(statusPoll,1500);statusPoll();\n"
"</script></html>\n"
;
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
    e=httpd_register_uri_handler(server,&u3);if(e!=ESP_OK)return e;
    const httpd_uri_t ws={.uri="/ws/audio-tx",.method=HTTP_GET,.handler=tx_ws,.is_websocket=true};
    return httpd_register_uri_handler(server,&ws);
}
