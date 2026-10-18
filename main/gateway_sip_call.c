/* G2-05: outbound SIP call signalling. RTP/audio deliberately not implemented. */
#include "gateway_services.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>

static const char *TAG = "g2_sip_call";
static QueueHandle_t s_actions;
static bool s_started;

extern esp_err_t gateway_sip_incoming_action(gateway_sip_action_t action);
static esp_err_t action_cb(gateway_sip_action_t action, void *ctx) {
    (void)ctx;
    if (action == GW_SIP_ACTION_ANSWER || action == GW_SIP_ACTION_REJECT) return gateway_sip_incoming_action(action);
    if (action == GW_SIP_ACTION_HANGUP) { gateway_sip_status_t st; gateway_sip_get_status(&st); if (!strcmp(st.direction,"incoming")) return gateway_sip_incoming_action(action); }
    if (action != GW_SIP_ACTION_CALL && action != GW_SIP_ACTION_HANGUP) return ESP_ERR_NOT_SUPPORTED;
    if (!s_actions) return ESP_ERR_INVALID_STATE;
    if (action == GW_SIP_ACTION_CALL) {
        gateway_sip_status_t st; gateway_sip_get_status(&st);
        if (st.state == GW_SIP_INCOMING || st.state == GW_SIP_CALLING ||
            st.state == GW_SIP_RINGING || st.state == GW_SIP_CONNECTED)
            return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_actions, &action, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
static int sip_code(const char *p) { int c=0; return sscanf(p,"SIP/2.0 %d",&c)==1?c:0; }
static bool sip_send(int fd,const char *msg) { size_t n=strlen(msg);return send(fd,msg,n,0)==(int)n; }
static void update(gateway_sip_state_t st,const gateway_sip_config_t *c,const char *cid) {
    gateway_sip_set_state(st,"outgoing",c->central_extension,c->central_name,cid,false);
}
static void call_task(void *arg) {
    (void)arg;
    gateway_sip_action_t action;
    char *rx=malloc(4096), *tx=malloc(4096);
    if(!rx || !tx){ ESP_LOGE(TAG,"Call buffers unavailable");free(rx);free(tx);vTaskDelete(NULL);return; }
    for(;;) {
        if(xQueueReceive(s_actions,&action,portMAX_DELAY)!=pdTRUE) continue;
        if(action!=GW_SIP_ACTION_CALL) continue;
        gateway_sip_config_t c; gateway_sip_get_config(&c);
        gateway_sip_status_t st; gateway_sip_get_status(&st);
        if(!st.registered || st.state==GW_SIP_INCOMING || st.state==GW_SIP_CONNECTED ||
           st.state==GW_SIP_CALLING || st.state==GW_SIP_RINGING ||
           !c.server[0] || !c.extension[0] || !c.central_extension[0] ||
           c.call_mode==GW_CALL_DISABLED || c.call_mode==GW_CALL_RECEIVE_ONLY) {
            ESP_LOGW(TAG,"Call rejected: registration/configuration not ready");continue;
        }
        struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
        char port[8];snprintf(port,sizeof(port),"%u",c.port?c.port:5060);
        if(getaddrinfo(c.server,port,&hints,&res)!=0 || !res){ESP_LOGW(TAG,"Call DNS failed");update(GW_SIP_FAILED,&c,"");continue;}
        int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0){freeaddrinfo(res);update(GW_SIP_FAILED,&c,"");continue;}
        struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_port=htons(5064);local.sin_addr.s_addr=INADDR_ANY;
        if(bind(fd,(struct sockaddr*)&local,sizeof(local))!=0 || connect(fd,res->ai_addr,res->ai_addrlen)!=0){
            ESP_LOGE(TAG,"Call socket bind/connect failed errno=%d",errno);close(fd);freeaddrinfo(res);update(GW_SIP_FAILED,&c,"");continue;
        }
        freeaddrinfo(res);
        struct sockaddr_in own={0};socklen_t olen=sizeof(own);
        getsockname(fd,(struct sockaddr*)&own,&olen);
        char ip[INET_ADDRSTRLEN]="192.168.10.51";
        if(own.sin_addr.s_addr!=INADDR_ANY) inet_ntop(AF_INET,&own.sin_addr,ip,sizeof(ip));
        unsigned id=esp_random(),branch=esp_random();
        char cid[40];snprintf(cid,sizeof(cid),"g2-%08lx",(unsigned long)id);
        char uri[128];snprintf(uri,sizeof(uri),"sip:%s@%s",c.central_extension,c.server);
        char from[160],to[160];
        snprintf(from,sizeof(from),"<sip:%s@%s>;tag=g2%08lx",c.extension,c.server,(unsigned long)id);
        snprintf(to,sizeof(to),"<sip:%s@%s>",c.central_extension,c.server);
        char sdp[512];int sn=snprintf(sdp,sizeof(sdp),
            "v=0\r\no=Gateway2 %lu 1 IN IP4 %s\r\ns=Gateway2 test call\r\nc=IN IP4 %s\r\nt=0 0\r\nm=audio 40000 RTP/AVP 0 8\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\na=inactive\r\n",
            (unsigned long)id,ip,ip);
        if(sn<=0 || sn>=(int)sizeof(sdp)){close(fd);continue;}
        int n=snprintf(tx,4096,
            "INVITE %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:5064;branch=z9hG4bK%08lx;rport\r\nMax-Forwards: 70\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: 1 INVITE\r\nContact: <sip:%s@%s:5064>\r\nUser-Agent: Gateway2-G2-05\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
            uri,ip,(unsigned long)branch,from,to,cid,c.extension,ip,sn,sdp);
        if(n<=0 || n>=4096 || !sip_send(fd,tx)){close(fd);update(GW_SIP_FAILED,&c,cid);continue;}
        ESP_LOGI(TAG,"G2-10 INVITE sent %s -> %s Call-ID=%s via=5064 SDP=inactive",c.extension,c.central_extension,cid);
        update(GW_SIP_CALLING,&c,cid);
        uint32_t timeout_ms=(c.call_timeout_s>=5 && c.call_timeout_s<=180?c.call_timeout_s:30)*1000;
        uint32_t elapsed=0;bool connected=false,finished=false,cancelled=false;
        char remote_to[256];snprintf(remote_to,sizeof(remote_to),"%s",to);
        struct timeval tv={.tv_sec=0,.tv_usec=250000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        while(!finished && elapsed<timeout_ms){
            gateway_sip_action_t pending;
            if(xQueueReceive(s_actions,&pending,0)==pdTRUE && pending==GW_SIP_ACTION_HANGUP){
                const char *method=connected?"BYE":"CANCEL";
                unsigned seq=connected?2:1;
                n=snprintf(tx,4096,"%s %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:5064;branch=z9hG4bK%08lx;rport\r\nMax-Forwards: 70\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %u %s\r\nContent-Length: 0\r\n\r\n",method,uri,ip,(unsigned long)(connected?esp_random():branch),from,remote_to,cid,seq,method);
                if(n>0 && n<4096) sip_send(fd,tx);
                cancelled=true;finished=true;update(GW_SIP_ENDED,&c,cid);break;
            }
            int got=recv(fd,rx,4095,0);
            if(got>0){
                rx[got]=0;int code=sip_code(rx);
                if(code==0)continue;
                ESP_LOGI(TAG,"G2-10 RX SIP response=%d Call-ID=%s bytes=%d",code,cid,got);
                if(code==200){
                    const char *body=strstr(rx,"\r\n\r\n");
                    ESP_LOGI(TAG,"G2-10 RX 200 SDP: %s",body?body+4:"(sin cuerpo)");
                }
                char *tohdr=strstr(rx,"\r\nTo:");
                if(tohdr){tohdr+=5;while(*tohdr==' ')tohdr++;char *end=strstr(tohdr,"\r\n");if(end && end-tohdr<(ptrdiff_t)sizeof(remote_to)){memcpy(remote_to,tohdr,(size_t)(end-tohdr));remote_to[end-tohdr]=0;}}
                if(code==180 || code==183)update(GW_SIP_RINGING,&c,cid);
                else if(code>=200){
                    if(code==200){
                        n=snprintf(tx,4096,"ACK %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:5064;branch=z9hG4bK%08lx;rport\r\nMax-Forwards: 70\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: 1 ACK\r\nContent-Length: 0\r\n\r\n",uri,ip,(unsigned long)esp_random(),from,remote_to,cid);
                        if(n>0 && n<4096){
                            bool ack_ok=sip_send(fd,tx);
                            ESP_LOGI(TAG,"G2-10 TX ACK Call-ID=%s sent=%d",cid,ack_ok?1:0);
                            if(!ack_ok){update(GW_SIP_FAILED,&c,cid);finished=true;break;}
                        }else{ESP_LOGE(TAG,"G2-10 ACK formatting failed");update(GW_SIP_FAILED,&c,cid);finished=true;break;}
                        connected=true;update(GW_SIP_CONNECTED,&c,cid);
                    }else{update(code==486?GW_SIP_BUSY:code==487?GW_SIP_ENDED:GW_SIP_REJECTED,&c,cid);finished=true;}
                }
            }
            elapsed+=250;
        }
        if(!finished && !connected)update(GW_SIP_NO_ANSWER,&c,cid);
        else if(!finished && connected)update(GW_SIP_ENDED,&c,cid);
        (void)cancelled;
        close(fd);
    }
}
void gateway_sip_call_start(void){
    if(s_started)return;
    s_actions=xQueueCreate(4,sizeof(gateway_sip_action_t));
    if(!s_actions){ESP_LOGE(TAG,"Call queue allocation failed");return;}
    if(xTaskCreate(call_task,"g2_sip_call",12288,NULL,3,NULL)!=pdPASS){vQueueDelete(s_actions);s_actions=NULL;ESP_LOGE(TAG,"Call task creation failed");return;}
    gateway_sip_register_action_handler(action_cb,NULL);
    s_started=true;
    ESP_LOGI(TAG,"G2-05 call signalling ready (no RTP)");
}
