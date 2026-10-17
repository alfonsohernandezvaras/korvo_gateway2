from pathlib import Path
import subprocess, sys, shutil, datetime
ROOT=Path(__file__).resolve().parent
TARGET=Path.cwd()/'main'/'gateway_sip_incoming.c'
EXPECTED='c11d5f2'

def abort(s):
    print('ERROR:',s);sys.exit(1)
def replace_once(text,old,new,label):
    if text.count(old)!=1: abort(f'Patron inesperado [{label}]: {text.count(old)} coincidencias. Sin cambios.')
    return text.replace(old,new,1)
if not TARGET.is_file():abort('Ejecutar desde raiz del proyecto; falta main/gateway_sip_incoming.c')
try:
    head=subprocess.check_output(['git','rev-parse','--short=7','HEAD'],text=True,stderr=subprocess.DEVNULL).strip()
    dirty=subprocess.check_output(['git','status','--porcelain'],text=True,stderr=subprocess.DEVNULL).strip()
except Exception as e:abort('Git no disponible: '+str(e))
if head!=EXPECTED:abort(f'HEAD={head}, esperado {EXPECTED}; no aplico sobre otra version.')
if dirty:abort('Hay cambios sin commit; respaldo/commit antes de aplicar.')
s=TARGET.read_text(encoding='utf-8')
if 'G2-06 R8' in s:abort('R8 ya esta aplicado.')
s=replace_once(s,'#include "lwip/sockets.h"','#include "lwip/sockets.h"\n#include "esp_timer.h"\n#include "esp_heap_caps.h"\n#include "esp_system.h"\n/* Existing NVS Management IP API */\nextern esp_err_t get_config_param_str(char *name, char **param);','includes')
s=replace_once(s,'static bool s_started;','''static bool s_started;
/* G2-06 R8: separate scheduler probe; never operates GPIO or locks. */
static volatile uint32_t s_rx_cycles;
static volatile uint32_t s_rx_packets;
static volatile uint32_t s_reply_calls;
static volatile uint32_t s_reply_errors;
static volatile uint32_t s_last_send_ms;
static volatile uint32_t s_max_send_ms;
static void sip_diag_task(void *arg){
    (void)arg;
    uint32_t sec=0, prev_cycles=0, prev_packets=0;
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t cycles=s_rx_cycles, packets=s_rx_packets;
        ESP_LOGI("g2_r8_diag","TICK=%lu rx_cycles_delta=%lu rx_packets_delta=%lu replies=%lu errors=%lu send_last_ms=%lu send_max_ms=%lu heap=%lu stack_free=%lu",
                 (unsigned long)++sec,(unsigned long)(cycles-prev_cycles),(unsigned long)(packets-prev_packets),
                 (unsigned long)s_reply_calls,(unsigned long)s_reply_errors,
                 (unsigned long)s_last_send_ms,(unsigned long)s_max_send_ms,
                 (unsigned long)esp_get_free_heap_size(),(unsigned long)uxTaskGetStackHighWaterMark(NULL));
        prev_cycles=cycles;prev_packets=packets;
    }
}
static bool management_ip(char *out,size_t cap){
    char *configured=NULL;
    esp_err_t e=get_config_param_str("static_ip",&configured);
    struct in_addr parsed;
    bool ok=e==ESP_OK && configured && inet_pton(AF_INET,configured,&parsed)==1 &&
            parsed.s_addr!=INADDR_ANY && parsed.s_addr!=INADDR_BROADCAST &&
            strlen(configured)<cap;
    if(ok)snprintf(out,cap,"%s",configured);
    free(configured);
    return ok;
}
''','diag globals')
s=replace_once(s,'int sent=sendto(fd,response,(size_t)n,0,(const struct sockaddr*)peer,sizeof(*peer));','''/* R8: time network send; logs before/after to identify the blocking point. */
        int64_t t0=esp_timer_get_time();
        ESP_LOGI(TAG,"R8 BEFORE sendto code=%d bytes=%d",code,n);
        int sent=sendto(fd,response,(size_t)n,0,(const struct sockaddr*)peer,sizeof(*peer));
        uint32_t ms=(uint32_t)((esp_timer_get_time()-t0)/1000);
        s_reply_calls++;s_last_send_ms=ms;
        if(ms>s_max_send_ms)s_max_send_ms=ms;
        if(sent<0)s_reply_errors++;
        ESP_LOGI(TAG,"R8 AFTER sendto code=%d ret=%d duration_ms=%lu",code,sent,(unsigned long)ms);''','send timing')
s=replace_once(s,'char callid[160]="",from_ext[24]="",localip[32]="192.168.10.51";','''char callid[160]="",from_ext[24]="",localip[32]="";
        if(!management_ip(localip,sizeof(localip))){
            ESP_LOGE(TAG,"R8 Management IP static_ip invalid; SIP reply identity unavailable");
            close(fd);vTaskDelay(pdMS_TO_TICKS(3000));continue;
        }
        ESP_LOGI(TAG,"R8 inbound Management IP=%s",localip);''','management ip')
s=replace_once(s,'int n=recvfrom(fd,rx,4095,0,(struct sockaddr*)&peer,&plen);','''int n=recvfrom(fd,rx,4095,0,(struct sockaddr*)&peer,&plen);
            s_rx_cycles++;
            if(n>0)s_rx_packets++;''','recv cycles')
s=replace_once(s,'''                    struct sockaddr_in probe={0};socklen_t probe_len=sizeof(probe);
                    int route=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
                    if(route>=0){connect(route,(struct sockaddr*)&peer,sizeof(peer));if(getsockname(route,(struct sockaddr*)&probe,&probe_len)==0){inet_ntop(AF_INET,&probe.sin_addr,localip,sizeof(localip));}close(route);}
''','''                    /* R8: localip is Management static_ip, not inferred via a route socket. */
''','remove probe')
s=replace_once(s,'"Contact: <sip:200@%s:5062>\\r\\nUser-Agent: Gateway2-G2-06\\r\\n%sContent-Length: %u\\r\\n\\r\\n%s",','"Contact: <sip:%s@%s:5062>\\r\\nUser-Agent: Gateway2-G2-06\\r\\n%sContent-Length: %u\\r\\n\\r\\n%s",','contact format')
s=replace_once(s,'code,reason,via,from,to,cid,cseq,local_ip,sdp?"Content-Type: application/sdp\\r\\n":"",','code,reason,via,from,to,cid,cseq,ext,local_ip,sdp?"Content-Type: application/sdp\\r\\n":"",','contact args')
s=replace_once(s,'    char via[512]="",from[512]="",to[512]="",cid[160]="",cseq[100]="";','''    char via[512]="",from[512]="",to[512]="",cid[160]="",cseq[100]="";
    gateway_sip_config_t cfg;gateway_sip_get_config(&cfg);
    const char *ext=cfg.extension[0]?cfg.extension:"200";''','contact ext')
s=replace_once(s,'ESP_LOGI(TAG,"INCOMING %s -> 200 call-id %s",from_ext,callid);','''ESP_LOGI(TAG,"INCOMING %s -> %s call-id %s",from_ext,cfg.extension,callid);''','incoming log')
s=replace_once(s,'    s_started=true;\n}','''    if(xTaskCreate(sip_diag_task,"g2_r8_diag",4096,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"R8 diagnostic heartbeat task could not start");
    s_started=true;
}''','start diag')
backup=TARGET.with_name(TARGET.name+'.BACKUP_PRE_R8_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
shutil.copy2(TARGET,backup)
try:TARGET.write_text(s,encoding='utf-8')
except Exception as e:shutil.copy2(backup,TARGET);abort('Fallo al escribir; restaurado: '+str(e))
print('OK: R8 aplicado en',TARGET)
print('BACKUP:',backup)
print('Verificar: idf.py build; monitor en COM9; comparar TICK durante INVITE.')
