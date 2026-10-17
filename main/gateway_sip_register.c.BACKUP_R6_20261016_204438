/* G2-04: SIP UDP registration only. Calls and RTP remain disabled. */
#include "gateway_services.h"
#include "gateway_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char *TAG="g2_sip_reg";
static void md5hex(const char *s,char out[33]) {
    unsigned char d[16]; mbedtls_md5((const unsigned char*)s,strlen(s),d);
    for(int i=0;i<16;i++) sprintf(out+i*2,"%02x",d[i]);
    out[32]=0;
}
static bool param(const char *src,const char *key,char *dst,size_t cap) {
    const char *p=src; size_t n=strlen(key);
    while(*p) {
        if(strncasecmp(p,key,n)!=0){p++;continue;}
        if(p!=src && (isalnum((unsigned char)p[-1])||p[-1]=='_')) {p+=n;continue;}
        p+=n; while(*p==' '||*p=='\t')p++;
        if(*p!='=')continue;
        p++;while(*p==' '||*p=='\t')p++;
        char q=(*p=='"')?'"':0;if(q)p++;
        size_t j=0;
        while(*p && (q?*p!=q:(*p!=','&&*p!='\r'&&*p!='\n'&&*p!=' '))) {
            if (j + 1 < cap) {
                dst[j++] = *p;
            }
            p++;
        }
        dst[j]=0;return j>0;
    }
    return false;
}
static int status_code(const char *msg){int n=0;if(sscanf(msg,"SIP/2.0 %d",&n)!=1)return 0;return n;}
static bool header_value(const char *msg,const char *key,char *dst,size_t cap){
    size_t n=strlen(key);const char *p=msg;
    while(*p){const char *e=strstr(p,"\r\n");if(!e)break;if(e==p)break;
        if((size_t)(e-p)>n && !strncasecmp(p,key,n)&&p[n]==':'){
            const char *v=p+n+1;while(v<e&&(*v==' '||*v=='\t'))v++;
            size_t z=(size_t)(e-v);if(z>=cap)z=cap-1;memcpy(dst,v,z);dst[z]=0;return true;
        }p=e+2;
    }return false;
}
static bool send_register(int fd,const gateway_sip_config_t *c,const char *localip,
                          unsigned seq,const char *callid,const char *challenge){
    char auth[650]="",uri[110],pkt[1700];
    snprintf(uri,sizeof(uri),"sip:%s",c->server);
    if(challenge&&*challenge){
        char realm[128]="",nonce[192]="",qop[80]="",opaque[128]="";
        if(!param(challenge,"realm",realm,sizeof(realm))||!param(challenge,"nonce",nonce,sizeof(nonce)))return false;
        (void)param(challenge,"qop",qop,sizeof(qop));(void)param(challenge,"opaque",opaque,sizeof(opaque));
        if(qop[0]&&strstr(qop,"auth")==NULL)return false;
        char a[400],ha1[33],ha2[33],resp[33];
        snprintf(a,sizeof(a),"%s:%s:%s",c->extension,realm,c->password);md5hex(a,ha1);
        snprintf(a,sizeof(a),"REGISTER:%s",uri);md5hex(a,ha2);
        if(qop[0]){
            snprintf(a,sizeof(a),"%s:%s:00000001:abcdef01:auth:%s",ha1,nonce,ha2);md5hex(a,resp);
            snprintf(auth,sizeof(auth),"Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5, qop=auth, nc=00000001, cnonce=\"abcdef01\"%s%s%s\r\n",
                     c->extension,realm,nonce,uri,resp,opaque[0]?", opaque=\"":"",opaque[0]?opaque:"",opaque[0]?"\"":"");
        }else{
            snprintf(a,sizeof(a),"%s:%s:%s",ha1,nonce,ha2);md5hex(a,resp);
            snprintf(auth,sizeof(auth),"Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5\r\n",
                     c->extension,realm,nonce,uri,resp);
        }
    }
    unsigned branch=esp_random();
    int len=snprintf(pkt,sizeof(pkt),
      "REGISTER %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:5066;branch=z9hG4bK%08lx;rport\r\nMax-Forwards: 70\r\n"
      "From: <sip:%s@%s>;tag=g2reg\r\nTo: <sip:%s@%s>\r\nCall-ID: %s\r\nCSeq: %u REGISTER\r\n"
      "Contact: <sip:%s@%s:5062>\r\nExpires: 120\r\nUser-Agent: Gateway2-G2-04\r\n%sContent-Length: 0\r\n\r\n",
      uri,localip,(unsigned long)branch,c->extension,c->server,c->extension,c->server,
      callid,seq,c->extension,localip,auth);
    return len>0&&len<(int)sizeof(pkt)&&send(fd,pkt,len,0)==len;
}
static bool register_once(const gateway_sip_config_t *c){
    struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    char port[8];snprintf(port,sizeof(port),"%u",c->port?c->port:5060);
    if(getaddrinfo(c->server,port,&hints,&res)!=0||!res){ESP_LOGW(TAG,"SIP DNS unavailable");return false;}
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0){freeaddrinfo(res);return false;}
    struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_port=htons(5066);local.sin_addr.s_addr=INADDR_ANY;
    if(bind(fd,(struct sockaddr*)&local,sizeof(local))!=0){ESP_LOGW(TAG,"SIP port 5062 busy");close(fd);freeaddrinfo(res);return false;}
    struct timeval tv={.tv_sec=3,.tv_usec=0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    if(connect(fd,res->ai_addr,res->ai_addrlen)!=0){close(fd);freeaddrinfo(res);return false;}
    freeaddrinfo(res);
    struct sockaddr_in own={0};socklen_t olen=sizeof(own);
    if(getsockname(fd,(struct sockaddr*)&own,&olen)!=0){close(fd);return false;}
    char localip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&own.sin_addr,localip,sizeof(localip));
    char callid[64];snprintf(callid,sizeof(callid),"g2-%08lx@%s",(unsigned long)esp_random(),localip);
    char reply[2200],challenge[700]="";bool ok=false;
    for(int step=0;step<2;step++){
        if(!send_register(fd,c,localip,(unsigned)(step+1),callid,step?challenge:NULL))break;
        int n=recv(fd,reply,sizeof(reply)-1,0);if(n<=0)break;reply[n]=0;
        int code=status_code(reply);
        if(code==200){ok=true;break;}
        if(step==0&&code==401&&header_value(reply,"WWW-Authenticate",challenge,sizeof(challenge))){continue;}
        ESP_LOGW(TAG,"REGISTER rejected SIP %d",code);break;
    }
    close(fd);return ok;
}
static void reg_task(void *arg){
    (void)arg;gateway_sip_config_t cfg,previous={0};bool was=false;
    ESP_LOGI(TAG,"G2-04 R4 SIP MEMORY DIAGNOSTIC started: heap=%u, largest=%u, stack_free=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    for(;;){
        gateway_sip_get_config(&cfg);
        bool ready=cfg.server[0]&&cfg.extension[0]&&cfg.password[0]&&cfg.call_mode!=GW_CALL_DISABLED;
        if(!ready){if(was){gateway_sip_set_registered(false,false);was=false;}
            memset(&previous,0,sizeof(previous));vTaskDelay(pdMS_TO_TICKS(3000));continue;}
        bool changed=memcmp(&cfg,&previous,sizeof(cfg))!=0;
        if(changed&&was){gateway_sip_set_registered(false,false);was=false;}
        previous=cfg;
        ESP_LOGI(TAG,"SIP before REGISTER: heap=%u largest=%u stack_free=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        bool ok=register_once(&cfg);
        if(ok!=was){gateway_sip_set_registered(ok,false);was=ok;}
        ESP_LOGI(TAG,"SIP REGISTER %s (%s:%u, ext %s) heap=%u largest=%u stack_free=%u",
                 ok?"OK":"FAILED",cfg.server,cfg.port,cfg.extension,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(pdMS_TO_TICKS(ok?90000:10000));
    }
}
void gateway_sip_register_start(void){
    /* R4: ESP-IDF ESP32 stack size is in BYTES; increase headroom for
     * SIP packet buffers, DNS, MD5 and lwIP call chain. */
    ESP_LOGI(TAG,"SIP R4 start request: heap=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16384) {
        ESP_LOGE(TAG,"SIP R4 refused: insufficient contiguous RAM");
        return;
    }
    BaseType_t ok=xTaskCreate(reg_task,"g2_sip_reg",12288,NULL,3,NULL);
    if(ok!=pdPASS)ESP_LOGE(TAG,"Cannot start SIP REGISTER task");
}
