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

static const char *TAG="g2_sip_in";
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
    char via[512]="",from[512]="",to[512]="",cid[160]="",cseq[100]="";
    if(!header(request,"Via",via,sizeof(via)) || !header(request,"From",from,sizeof(from)) ||
       !header(request,"To",to,sizeof(to)) || !header(request,"Call-ID",cid,sizeof(cid)) ||
       !header(request,"CSeq",cseq,sizeof(cseq)))return;
    if(!strstr(to,";tag="))strncat(to,";tag=g2in",sizeof(to)-strlen(to)-1);
    char body[512]="";
    if(sdp)snprintf(body,sizeof(body),
        "v=0\r\no=Gateway2 1 1 IN IP4 %s\r\ns=Gateway2 signalling only\r\nc=IN IP4 %s\r\nt=0 0\r\nm=audio 0 RTP/AVP 0 8\r\na=inactive\r\n",local_ip,local_ip);
    char response[2300];int n=snprintf(response,sizeof(response),
        "SIP/2.0 %d %s\r\nVia: %s\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %s\r\nContact: <sip:200@%s:5062>\r\nUser-Agent: Gateway2-G2-06\r\n%sContent-Length: %u\r\n\r\n%s",
        code,reason,via,from,to,cid,cseq,local_ip,sdp?"Content-Type: application/sdp\r\n":"",
        (unsigned)strlen(body),body);
    if(n>0 && n<(int)sizeof(response))sendto(fd,response,n,0,(const struct sockaddr*)peer,sizeof(*peer));
}
static void in_task(void *arg){
    (void)arg;
    char *rx=malloc(4096),*invite=malloc(4096);
    if(!rx||!invite){ESP_LOGE(TAG,"No memory");free(rx);free(invite);vTaskDelete(NULL);return;}
    for(;;){
        int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0){vTaskDelay(pdMS_TO_TICKS(2000));continue;}
        struct sockaddr_in bindaddr={0};bindaddr.sin_family=AF_INET;bindaddr.sin_port=htons(5062);bindaddr.sin_addr.s_addr=INADDR_ANY;
        if(bind(fd,(struct sockaddr*)&bindaddr,sizeof(bindaddr))!=0){
            ESP_LOGW(TAG,"SIP inbound 5062 unavailable; retrying");close(fd);vTaskDelay(pdMS_TO_TICKS(3000));continue;
        }
        struct timeval tv={.tv_sec=0,.tv_usec=200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        ESP_LOGI(TAG,"SIP inbound listening UDP 5062");
        bool active=false,answered=false,waiting_ack=false;
        struct sockaddr_in caller={0};char callid[160]="",from_ext[24]="",localip[32]="192.168.10.51";
        TickType_t start=0,last200=0;
        for(;;){
            gateway_sip_action_t action;
            while(xQueueReceive(s_commands,&action,0)==pdTRUE){
                if(!active)continue;
                if(action==GW_SIP_ACTION_ANSWER && !answered){
                    send_reply(fd,&caller,invite,200,"OK",localip,true);
                    answered=true;waiting_ack=true;last200=xTaskGetTickCount();
                    ESP_LOGI(TAG,"Incoming 200 OK sent; waiting ACK");
                }else if(action==GW_SIP_ACTION_REJECT || action==GW_SIP_ACTION_HANGUP){
                    if(!answered){send_reply(fd,&caller,invite,486,"Busy Here",localip,false);}
                    else {ESP_LOGW(TAG,"Local BYE not implemented yet; remote must hang up");continue;}
                    active=false;answered=false;waiting_ack=false;
                    gateway_sip_set_state(GW_SIP_ENDED,"incoming",from_ext,"",callid,false);
                }
            }
            struct sockaddr_in peer={0};socklen_t plen=sizeof(peer);
            int n=recvfrom(fd,rx,4095,0,(struct sockaddr*)&peer,&plen);
            if(n>0){
                rx[n]=0;
                char cid[160]="";header(rx,"Call-ID",cid,sizeof(cid));
                bool same=active && !strcmp(cid,callid) && peer.sin_addr.s_addr==caller.sin_addr.s_addr;
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
                    struct sockaddr_in probe={0};socklen_t probe_len=sizeof(probe);
                    int route=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
                    if(route>=0){connect(route,(struct sockaddr*)&peer,sizeof(peer));if(getsockname(route,(struct sockaddr*)&probe,&probe_len)==0){inet_ntop(AF_INET,&probe.sin_addr,localip,sizeof(localip));}close(route);}
                    send_reply(fd,&peer,rx,100,"Trying",localip,false);
                    send_reply(fd,&peer,rx,180,"Ringing",localip,false);
                    gateway_sip_set_state(GW_SIP_INCOMING,"incoming",from_ext,"",callid,false);
                    ESP_LOGI(TAG,"INCOMING %s -> 200 call-id %s",from_ext,callid);
                }else if(same && !strncmp(rx,"ACK ",4)){
                    waiting_ack=false;gateway_sip_set_state(GW_SIP_CONNECTED,"incoming",from_ext,"",callid,false);
                    ESP_LOGI(TAG,"Incoming ACK received");
                }else if(same && !strncmp(rx,"CANCEL ",7) && !answered){
                    send_reply(fd,&peer,rx,200,"OK",localip,false);
                    send_reply(fd,&caller,invite,487,"Request Terminated",localip,false);
                    active=false;gateway_sip_set_state(GW_SIP_ENDED,"incoming",from_ext,"",callid,false);
                }else if(same && !strncmp(rx,"BYE ",4)){
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
                }else if(waiting_ack && now-last200>pdMS_TO_TICKS(1000)){
                    send_reply(fd,&caller,invite,200,"OK",localip,true);last200=now;
                }
            }
        }
    }
}
void gateway_sip_incoming_start(void){
    if(s_started)return;
    s_commands=xQueueCreate(4,sizeof(gateway_sip_action_t));
    if(!s_commands)return;
    if(xTaskCreate(in_task,"g2_sip_in",8192,NULL,3,NULL)!=pdPASS){vQueueDelete(s_commands);s_commands=NULL;return;}
    s_started=true;
}
