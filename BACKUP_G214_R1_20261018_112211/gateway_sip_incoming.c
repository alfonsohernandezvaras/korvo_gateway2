/* G2-06: inbound SIP signalling only; no RTP/audio. */
#include "gateway_services.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

/* G2-06 R3 RX DIAG: monitor de datagramas SIP entrantes */
/* G2-06 R5 STACK FIX: respuestas SIP con buffer dinamico y pila ampliada */
static const char *TAG="g2_sip_in";
/* G2-12 CONTACT IP from NVS: same Management IP as REGISTER */
extern esp_err_t get_config_param_str(char *name, char **param);
static QueueHandle_t s_commands;
static bool s_started;

esp_err_t gateway_sip_incoming_action(gateway_sip_action_t a){
    if(!s_commands)return ESP_ERR_INVALID_STATE;
    return xQueueSend(s_commands,&a,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
static bool header(const char *p,const char *name,char *out,size_t cap){
    size_t len=strlen(name);
    while(p && *p){const char *end=strstr(p,"\r\n");if(!end)break;
        if(end==p)break;
        if((size_t)(end-p)>len && !strncasecmp(p,name,len) && p[len]==':'){
            const char *v=p+len+1;while(v<end && (*v==' '||*v=='\t'))v++;
            size_t n=(size_t)(end-v);if(n>=cap)n=cap-1;
            memcpy(out,v,n);out[n]=0;return true;
        }p=end+2;
    }return false;
}
static void send_reply(int fd,const struct sockaddr_in *peer,const char *request,
                       int code,const char *reason,const char *local_ip,bool sdp){
    char via[1200]="",from[512]="",to[512]="",cid[160]="",cseq[100]="";
    const char *line=request;
    while(line && *line){
        const char *end=strstr(line,"\r\n");
        if(!end || end==line)break;
        if(!strncasecmp(line,"Via:",4) || !strncasecmp(line,"v:",2)){
            size_t n=(size_t)(end-line), used=strlen(via);
            if(used+n+2>=sizeof(via)){ESP_LOGE(TAG,"Via chain too long");return;}
            memcpy(via+used,line,n);via[used+n]='\r';via[used+n+1]='\n';via[used+n+2]=0;
        }
        line=end+2;
    }
    if(!via[0] || !header(request,"From",from,sizeof(from)) ||
       !header(request,"To",to,sizeof(to)) || !header(request,"Call-ID",cid,sizeof(cid)) ||
       !header(request,"CSeq",cseq,sizeof(cseq)))return;
    if(!strstr(to,";tag="))strncat(to,";tag=g2in",sizeof(to)-strlen(to)-1);
    char body[512]="";
    if(sdp)snprintf(body,sizeof(body),
        "v=0\r\no=Gateway2 1 1 IN IP4 %s\r\ns=Gateway2 signalling only\r\nc=IN IP4 %s\r\nt=0 0\r\nm=audio 40000 RTP/AVP 0 8\r\na=inactive\r\n",local_ip,local_ip);
    char *response=malloc(2300);
    if(!response){ESP_LOGE(TAG,"SIP response malloc failed");return;}
    int n=snprintf(response,2300,
        "SIP/2.0 %d %s\r\n%sFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %s\r\nContact: <sip:200@%s:5062>\r\nUser-Agent: Gateway2-G2-06\r\n%sContent-Length: %u\r\n\r\n%s",
        code,reason,via,from,to,cid,cseq,local_ip,sdp?"Content-Type: application/sdp\r\n":"",
        (unsigned)strlen(body),body);
    if(n>0 && n<2300){
        int sent=sendto(fd,response,(size_t)n,0,(const struct sockaddr*)peer,sizeof(*peer));
        if(sent<0)ESP_LOGE(TAG,"SIP reply %d sendto failed errno=%d",code,errno);
        else ESP_LOGI(TAG,"SIP reply %d sent=%d stack_free=%u",code,sent,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }else ESP_LOGE(TAG,"SIP reply %d formatting failed n=%d",code,n);
    free(response);
}
static void in_task(void *arg){
    (void)arg;
    char *rx=malloc(4096),*invite=malloc(4096);
    if(!rx||!invite){ESP_LOGE(TAG,"No memory");free(rx);free(invite);vTaskDelete(NULL);return;}
    for(;;){
        int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0){ESP_LOGE(TAG,"UDP socket 5062 failed errno=%d",errno);vTaskDelay(pdMS_TO_TICKS(2000));continue;}
        struct sockaddr_in bindaddr={0};bindaddr.sin_family=AF_INET;bindaddr.sin_port=htons(5062);bindaddr.sin_addr.s_addr=INADDR_ANY;
        if(bind(fd,(struct sockaddr*)&bindaddr,sizeof(bindaddr))!=0){
            ESP_LOGW(TAG,"SIP inbound 5062 bind failed errno=%d; retrying",errno);close(fd);vTaskDelay(pdMS_TO_TICKS(3000));continue;
        }
        struct timeval tv={.tv_sec=0,.tv_usec=200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        ESP_LOGI(TAG,"SIP inbound listening UDP 5062");
        bool active=false,answered=false,waiting_ack=false;
        unsigned bye_seq=2;
        struct sockaddr_in caller={0};char callid[160]="",from_ext[24]="",localip[32]="192.168.10.51";
        TickType_t start=0,last200=0,answer_start=0; unsigned retry200=0;
        for(;;){
            gateway_sip_action_t action;
            while(xQueueReceive(s_commands,&action,0)==pdTRUE){
                if(!active)continue;
                if(action==GW_SIP_ACTION_ANSWER && !answered){
                    send_reply(fd,&caller,invite,200,"OK",localip,true);
                    answered=true;waiting_ack=true;last200=xTaskGetTickCount();answer_start=last200;retry200=0;
                    ESP_LOGI(TAG,"G2-09 200 OK SDP: audio=40000 PCMU/PCMA inactive (NO RTP); waiting ACK");
                }else if(action==GW_SIP_ACTION_REJECT || action==GW_SIP_ACTION_HANGUP){
                    if(!answered){send_reply(fd,&caller,invite,486,"Busy Here",localip,false);}
                    else {
                         char via[180],from[512],to[512],bye[1500];
                         char remote_from[512]="",remote_to[512]="";
                         header(invite,"From",remote_from,sizeof(remote_from));
                         header(invite,"To",remote_to,sizeof(remote_to));
                         snprintf(from,sizeof(from),"%s%s",remote_to,strstr(remote_to,";tag=")?"":";tag=g2in");
                         snprintf(to,sizeof(to),"%s",remote_from);
                         snprintf(via,sizeof(via),"SIP/2.0/UDP %s:5062;branch=z9hG4bKg2bye%lu;rport",localip,(unsigned long)bye_seq);
                         int blen=snprintf(bye,sizeof(bye),"BYE sip:%s@%s SIP/2.0\r\nVia: %s\r\nMax-Forwards: 70\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %u BYE\r\nContent-Length: 0\r\n\r\n",from_ext,inet_ntoa(caller.sin_addr),via,from,to,callid,bye_seq++);
                         if(blen>0 && blen<(int)sizeof(bye))sendto(fd,bye,blen,0,(struct sockaddr*)&caller,sizeof(caller));
                         ESP_LOGI(TAG,"Incoming call local BYE sent");
                     }
                    active=false;answered=false;waiting_ack=false;
                    gateway_sip_set_state(GW_SIP_ENDED,"incoming",from_ext,"",callid,false);
                }
            }
            struct sockaddr_in peer={0};socklen_t plen=sizeof(peer);
            int n=recvfrom(fd,rx,4095,0,(struct sockaddr*)&peer,&plen);
            if(n>0){
                rx[n]=0;
                /* G2-06 R4 TRACE: registro de primera linea y cabeceras SIP */
                ESP_LOGI(TAG,"RX UDP 5062 from %s:%u bytes=%d",inet_ntoa(peer.sin_addr),(unsigned)ntohs(peer.sin_port),n);
                ESP_LOGI(TAG,"SIP FIRST LINE %.*s",(int)strcspn(rx,"\r\n"),rx);
                const char *diag_call=strstr(rx,"Call-ID:");
                const char *diag_cseq=strstr(rx,"CSeq:");
                if(diag_call)ESP_LOGI(TAG,"SIP Call-ID %.*s",(int)strcspn(diag_call+8,"\r\n"),diag_call+8);
                else ESP_LOGW(TAG,"SIP Call-ID ausente");
                if(diag_cseq)ESP_LOGI(TAG,"SIP CSeq %.*s",(int)strcspn(diag_cseq+5,"\r\n"),diag_cseq+5);
                else ESP_LOGW(TAG,"SIP CSeq ausente");
                char cid[160]="";header(rx,"Call-ID",cid,sizeof(cid));
                bool same=active && !strcmp(cid,callid) && peer.sin_addr.s_addr==caller.sin_addr.s_addr;
                /* G2-13 dialog match: ACK/BYE can arrive directly from Linphone,
                   while INVITE was forwarded by Kiosk. Match active Call-ID,
                   not the original UDP source address. */
                bool same_dialog=active && cid[0] && callid[0] && !strcmp(cid,callid);
                if(!strncmp(rx,"INVITE ",7)){
                    gateway_sip_config_t cfg;gateway_sip_get_config(&cfg);
                    gateway_sip_status_t status;gateway_sip_get_status(&status);
                    if(cfg.call_mode==GW_CALL_DISABLED || cfg.call_mode==GW_CALL_SEND_ONLY ||
                       (active&&!same) || (!active && (status.state==GW_SIP_CALLING || status.state==GW_SIP_RINGING || status.state==GW_SIP_CONNECTED))){
                        send_reply(fd,&peer,rx,486,"Busy Here",localip,false);continue;
                    }
                    if(same){send_reply(fd,&peer,rx,answered?200:180,answered?"OK":"Ringing",localip,answered);continue;}
                    if(!header(rx,"Call-ID",callid,sizeof(callid))){send_reply(fd,&peer,rx,400,"Bad Request",localip,false);continue;}
                    memcpy(invite,rx,(size_t)n+1);caller=peer;active=true;answered=false;waiting_ack=false;
                    start=xTaskGetTickCount();
                    char from[512]="";header(rx,"From",from,sizeof(from));
                    const char *sip=strstr(from,"sip:");from_ext[0]=0;
                    if(sip){sip+=4;size_t k=0;while(sip[k]&&sip[k]!='@'&&sip[k]!=';'&&sip[k]!='>'&&k<sizeof(from_ext)-1){from_ext[k]=sip[k];k++;}from_ext[k]=0;}
                    /* G2-12: el bridge puede devolver 0.0.0.0 en getsockname().
                       Usar Management IP configurada, igual que SIP REGISTER. */
                    char *configured_ip=NULL;
                    struct in_addr parsed_ip;
                    esp_err_t ip_err=get_config_param_str("static_ip",&configured_ip);
                    bool valid_ip=(ip_err==ESP_OK && configured_ip &&
                        inet_pton(AF_INET,configured_ip,&parsed_ip)==1 &&
                        parsed_ip.s_addr!=INADDR_ANY && parsed_ip.s_addr!=INADDR_BROADCAST);
                    if(!valid_ip){
                        ESP_LOGE(TAG,"G2-12 invalid static_ip: cannot answer INVITE");
                        free(configured_ip);
                        active=false;answered=false;waiting_ack=false;
                        gateway_sip_set_state(GW_SIP_FAILED,"incoming",from_ext,"",callid,false);
                        continue;
                    }
                    snprintf(localip,sizeof(localip),"%s",configured_ip);
                    free(configured_ip);
                    ESP_LOGI(TAG,"G2-12 CONTACT IP=%s:5062 (NVS static_ip)",localip);
                    send_reply(fd,&peer,rx,100,"Trying",localip,false);
                    send_reply(fd,&peer,rx,180,"Ringing",localip,false);
                    gateway_sip_set_state(GW_SIP_INCOMING,"incoming",from_ext,"",callid,false);
                    ESP_LOGI(TAG,"STATE incoming caller=%s id=%s",from_ext,callid);
                    ESP_LOGI(TAG,"INCOMING %s -> 200 call-id %s",from_ext,callid);
                }else if(same_dialog && !strncmp(rx,"ACK ",4)){
                    if(answered && waiting_ack){
                        waiting_ack=false;
                        gateway_sip_set_state(GW_SIP_CONNECTED,"incoming",from_ext,"",callid,false);
                        ESP_LOGI(TAG,"G2-07 ACK received; CONNECTED");
                    }else ESP_LOGW(TAG,"G2-07 ACK ignored: no pending answer");
                }else if(same && !strncmp(rx,"CANCEL ",7)){
                    send_reply(fd,&peer,rx,200,"OK",localip,false);
                    if(!answered){
                        send_reply(fd,&caller,invite,487,"Request Terminated",localip,false);
                        active=false;waiting_ack=false;
                        gateway_sip_set_state(GW_SIP_ENDED,"incoming",from_ext,"",callid,false);
                    }else ESP_LOGW(TAG,"G2-08 late CANCEL after 200 OK; awaiting ACK/BYE or timeout");
                }else if(same_dialog && !strncmp(rx,"BYE ",4)){
                    send_reply(fd,&peer,rx,200,"OK",localip,false);
                    active=false;answered=false;waiting_ack=false;
                    gateway_sip_set_state(GW_SIP_ENDED,"incoming",from_ext,"",callid,false);
                }
            }
            if(active){
                TickType_t now=xTaskGetTickCount();
                if(!answered && (now-start)>pdMS_TO_TICKS(30000)){
                    send_reply(fd,&caller,invite,480,"Temporarily Unavailable",localip,false);
                    active=false;gateway_sip_set_state(GW_SIP_NO_ANSWER,"incoming",from_ext,"",callid,false);
                }else if(waiting_ack && now-answer_start>=pdMS_TO_TICKS(32000)){
                    ESP_LOGW(TAG,"G2-08 ACK TIMEOUT after 32s; 200 retries=%u",retry200);
                    active=false;answered=false;waiting_ack=false;
                    gateway_sip_set_state(GW_SIP_FAILED,"incoming",from_ext,"",callid,false);
                }else if(waiting_ack && now-last200>=pdMS_TO_TICKS(1000)){
                    send_reply(fd,&caller,invite,200,"OK",localip,true);
                    last200=now;retry200++;
                }
            }
        }
    }
}
void gateway_sip_incoming_start(void){
    if(s_started)return;
    s_commands=xQueueCreate(4,sizeof(gateway_sip_action_t));
    if(!s_commands)return;
    if(xTaskCreate(in_task,"g2_sip_in",16384,NULL,3,NULL)!=pdPASS){vQueueDelete(s_commands);s_commands=NULL;return;}
    s_started=true;
}
