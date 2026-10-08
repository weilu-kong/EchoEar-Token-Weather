#include "sh_auth.h"
#include "sh_http.h"
#include "sh_network.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *names[]={"codex","antigravity","claude","cursor","google"};
static SemaphoreHandle_t lock;
static uint32_t revisions[SH_ACCOUNT_COUNT];
static char *copy(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(p)memcpy(p,s,n);return p;}
static const char *string(cJSON *json,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool field(cJSON *json,const char *key,size_t max,bool required,bool spaces){
    cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);if(!v)return !required;
    if(!cJSON_IsString(v)||!v->valuestring||(required&&!v->valuestring[0])||strlen(v->valuestring)>max)return false;
    for(const unsigned char *p=(const unsigned char*)v->valuestring;*p;p++)if(*p<32||*p>126||(!spaces&&isspace(*p)))return false;
    return true;
}
static bool unique_fields(const cJSON *item){
    for(const cJSON *a=item->child;a;a=a->next){
        if(cJSON_IsObject(item))for(const cJSON *b=a->next;b;b=b->next)
            if(a->string&&b->string&&!strcmp(a->string,b->string))return false;
        if(!unique_fields(a))return false;
    }
    return true;
}
static cJSON *parse(const char *json,size_t length){
    if(!json||!length||length>SH_AUTH_JSON_MAX||memchr(json,0,length))return NULL;
    unsigned depth=0;bool quoted=false;
    for(size_t i=0;i<length;i++){
        unsigned char c=(unsigned char)json[i];
        if(quoted){
            if(c<32)return NULL;
            if(c=='\\'){
                if(++i>=length)return NULL;
                if(json[i]=='u'&&i+4<length&&!memcmp(json+i+1,"0000",4))return NULL;
            }else if(c=='"')quoted=false;
        }else if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>8)return NULL;}
        else if(c=='}'||c==']'){if(!depth)return NULL;--depth;}
    }
    if(quoted||depth)return NULL;
    /* The USB caller supplies a byte span, not necessarily a terminated string. */
    char *terminated=heap_caps_malloc(length+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!terminated)return NULL;
    memcpy(terminated,json,length);terminated[length]=0;
    const char *end=NULL;cJSON *v=cJSON_ParseWithLengthOpts(terminated,length+1,&end,false);
    bool complete=cJSON_IsObject(v)&&end;
    if(complete){
        while(end<terminated+length&&isspace((unsigned char)*end))end++;
        complete=end==terminated+length;
    }
    free(terminated);
    if(!complete||!unique_fields(v)){cJSON_Delete(v);return NULL;}
    return v;
}
static int account(cJSON *json){const char *name=string(json,"provider");if(!name)return -1;for(int i=0;i<SH_ACCOUNT_COUNT;i++)if(!strcmp(name,names[i]))return i;return -1;}
static bool valid(cJSON *j,int id){
    if(id<0||id>=SH_ACCOUNT_COUNT||!field(j,"access_token",SH_AUTH_TOKEN_MAX,true,false)||!field(j,"refresh_token",SH_AUTH_TOKEN_MAX,false,false)||!field(j,"client_id",256,false,false)||!field(j,"client_secret",512,false,false)||!field(j,"account_id",256,false,false)||!field(j,"project",256,false,false)||!field(j,"scope",1024,false,true)||!field(j,"team_id",256,false,false))return false;
    cJSON *expiry=cJSON_GetObjectItemCaseSensitive(j,"expires_at");
    if(expiry&&(!cJSON_IsNumber(expiry)||!isfinite(expiry->valuedouble)||expiry->valuedouble<0||expiry->valuedouble>4102444800.0||floor(expiry->valuedouble)!=expiry->valuedouble))return false;
    if(id==SH_GOOGLE&&(!field(j,"refresh_token",SH_AUTH_TOKEN_MAX,true,false)||!field(j,"client_id",256,true,false)||!field(j,"client_secret",512,true,false)))return false;
    if(id==SH_ANTIGRAVITY&&!field(j,"project",256,true,false))return false;
    return true;
}
static esp_err_t read_locked(sh_account_id_t id,char **out){
    nvs_handle_t handle;esp_err_t err=nvs_open("sh_auth",NVS_READONLY,&handle);if(err!=ESP_OK)return err;
    size_t length=0;err=nvs_get_str(handle,names[id],NULL,&length);
    if(err==ESP_OK&&(length<2||length>SH_AUTH_JSON_MAX+1))err=ESP_ERR_INVALID_SIZE;
    char *raw=NULL;if(err==ESP_OK){raw=heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!raw)err=ESP_ERR_NO_MEM;}
    if(err==ESP_OK)err=nvs_get_str(handle,names[id],raw,&length);
    nvs_close(handle);if(err!=ESP_OK){free(raw);return err;}*out=raw;return ESP_OK;
}
static esp_err_t write_locked(sh_account_id_t id,const char *json){
    if(strlen(json)>SH_AUTH_JSON_MAX)return ESP_ERR_INVALID_SIZE;
    nvs_handle_t handle;esp_err_t err=nvs_open("sh_auth",NVS_READWRITE,&handle);if(err!=ESP_OK)return err;
    err=nvs_set_str(handle,names[id],json);if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);
    if(err==ESP_OK)++revisions[id];
    return err;
}
esp_err_t sh_auth_init(void){if(lock)return ESP_ERR_INVALID_STATE;lock=xSemaphoreCreateMutex();return lock?ESP_OK:ESP_ERR_NO_MEM;}
uint32_t sh_auth_revision(sh_account_id_t id){
    if(!lock||id<0||id>=SH_ACCOUNT_COUNT)return 0;
    xSemaphoreTake(lock,portMAX_DELAY);uint32_t revision=revisions[id];xSemaphoreGive(lock);return revision;
}
void sh_auth_release(sh_account_credentials_t *c){if(!c)return;free(c->access_token);free(c->refresh_token);free(c->client_id);free(c->client_secret);free(c->account_id);free(c->project);free(c->scope);free(c->team_id);memset(c,0,sizeof(*c));}
esp_err_t sh_auth_load(sh_account_id_t id,sh_account_credentials_t *out){
    if(!lock||!out||id<0||id>=SH_ACCOUNT_COUNT)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));char *raw=NULL;xSemaphoreTake(lock,portMAX_DELAY);esp_err_t err=read_locked(id,&raw);out->revision=revisions[id];xSemaphoreGive(lock);if(err!=ESP_OK)return err;
    cJSON *j=parse(raw,strlen(raw));free(raw);if(!j||!valid(j,id)){cJSON_Delete(j);return ESP_ERR_INVALID_RESPONSE;}
    char **targets[]={&out->access_token,&out->refresh_token,&out->client_id,&out->client_secret,&out->account_id,&out->project,&out->scope,&out->team_id};
    const char *keys[]={"access_token","refresh_token","client_id","client_secret","account_id","project","scope","team_id"};
    for(size_t i=0;i<8;i++){const char *s=string(j,keys[i]);if(s){*targets[i]=copy(s);if(!*targets[i]){err=ESP_ERR_NO_MEM;break;}}}
    cJSON *expiry=cJSON_GetObjectItemCaseSensitive(j,"expires_at");out->expires_at=expiry?(time_t)expiry->valuedouble:0;cJSON_Delete(j);
    if(err!=ESP_OK)sh_auth_release(out);
    return err;
}
esp_err_t sh_auth_import(const char *json,size_t length){
    if(!lock)return ESP_ERR_INVALID_STATE;
    cJSON *j=parse(json,length);int id=account(j);
    if(!j||!valid(j,id)){cJSON_Delete(j);return ESP_ERR_INVALID_ARG;}
    /* Persist only the credential fields, never a complete desktop auth database. */
    cJSON *clean=cJSON_CreateObject();const char *keys[]={"provider","access_token","refresh_token","client_id","client_secret","account_id","project","scope","team_id"};
    bool ok=clean!=NULL;for(size_t i=0;ok&&i<9;i++){const char *s=string(j,keys[i]);if(s)ok=cJSON_AddStringToObject(clean,keys[i],s)!=NULL;}
    cJSON *expires=cJSON_GetObjectItemCaseSensitive(j,"expires_at");if(ok&&expires)ok=cJSON_AddNumberToObject(clean,"expires_at",expires->valuedouble)!=NULL;
    char *serialized=ok?cJSON_PrintUnformatted(clean):NULL;cJSON_Delete(clean);cJSON_Delete(j);if(!serialized)return ESP_ERR_NO_MEM;
    xSemaphoreTake(lock,portMAX_DELAY);esp_err_t err=write_locked((sh_account_id_t)id,serialized);xSemaphoreGive(lock);free(serialized);return err;
}
esp_err_t sh_auth_forget(sh_account_id_t id){
    if(!lock||id<0||id>=SH_ACCOUNT_COUNT)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock,portMAX_DELAY);nvs_handle_t handle;esp_err_t err=nvs_open("sh_auth",NVS_READWRITE,&handle);
    if(err==ESP_OK){err=nvs_erase_key(handle,names[id]);if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}
    if(err==ESP_OK)++revisions[id];
    xSemaphoreGive(lock);return err;
}
static char *encoded(const char *s){
    if(!s)return NULL;
    size_t n=strlen(s);char *out=heap_caps_malloc(n*3+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!out)return NULL;
    char *p=out;
    for(const unsigned char *q=(const unsigned char*)s;*q;q++){if(isalnum(*q)||strchr("-._~",*q))*p++=*q;else{snprintf(p,4,"%%%02X",*q);p+=3;}}*p=0;return out;
}
static bool network_current(uint32_t generation){
    sh_net_snapshot_t snapshot;sh_network_get_snapshot(&snapshot);
    return snapshot.wifi_connected&&snapshot.generation==generation;
}
esp_err_t sh_auth_refresh(sh_account_id_t id,sh_account_credentials_t *c,uint32_t generation){
    if(!lock||id<0||id>=SH_ACCOUNT_COUNT)return ESP_ERR_INVALID_ARG;
    if(!c||!c->refresh_token||!c->refresh_token[0])return SH_CLOUD_ERR_AUTH;
    if(!network_current(generation)||sh_auth_revision(id)!=c->revision)return ESP_ERR_INVALID_STATE;
    const char *url=NULL,*client_id=c->client_id,*type="application/json";char *body=NULL;
    if(id==SH_CODEX){url="https://auth.openai.com/oauth/token";if(!client_id)client_id="app_EMoamEEZ73f0CkXaXp7hrann";}
    else if(id==SH_CLAUDE){url="https://platform.claude.com/v1/oauth/token";if(!client_id)client_id="9d1c250a-e61b-44d9-88ed-5944d1962f5e";}
    else if(id==SH_CURSOR){url="https://api2.cursor.sh/oauth/token";if(!client_id)client_id="KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB";}
    else if(id==SH_GOOGLE||id==SH_ANTIGRAVITY){url="https://oauth2.googleapis.com/token";type="application/x-www-form-urlencoded";}
    else return SH_CLOUD_ERR_UNSUPPORTED;
    if(!client_id||!client_id[0])return SH_CLOUD_ERR_AUTH;
    if(id==SH_GOOGLE||id==SH_ANTIGRAVITY){
        if(!c->client_secret)return SH_CLOUD_ERR_AUTH;
        char *a=encoded(client_id),*b=encoded(c->client_secret),*r=encoded(c->refresh_token);
        if(a&&b&&r){size_t n=strlen(a)+strlen(b)+strlen(r)+80;body=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(body)snprintf(body,n,"grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s",a,b,r);}free(a);free(b);free(r);
    }else{
        cJSON *j=cJSON_CreateObject();bool ok=j&&cJSON_AddStringToObject(j,"grant_type","refresh_token")&&cJSON_AddStringToObject(j,"client_id",client_id)&&cJSON_AddStringToObject(j,"refresh_token",c->refresh_token);
        if(ok&&id==SH_CLAUDE&&c->scope&&c->scope[0])ok=cJSON_AddStringToObject(j,"scope",c->scope)!=NULL;
        if(ok)body=cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
    }
    if(!body)return ESP_ERR_NO_MEM;
    char *response=heap_caps_malloc(SH_CLOUD_BODY_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!response){free(body);return ESP_ERR_NO_MEM;}
    sh_http_header_t headers[]={{"Content-Type",type},{"x-cursor-client-type","ide"}};int status=0;
    esp_err_t err=sh_http_request("POST",url,headers,id==SH_CURSOR?2:1,body,response,SH_CLOUD_BODY_MAX+1,&status,generation);free(body);
    if(err==ESP_OK&&status!=200)err=status==429?SH_CLOUD_ERR_RATE_LIMIT:((status==400||status==401||status==403)?SH_CLOUD_ERR_AUTH:ESP_ERR_INVALID_RESPONSE);
    cJSON *j=err==ESP_OK?parse(response,strlen(response)):NULL;free(response);
    if(err!=ESP_OK)return err;
    if(j&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"shouldLogout"))){cJSON_Delete(j);return SH_CLOUD_ERR_AUTH;}
    if(!j||!field(j,"access_token",SH_AUTH_TOKEN_MAX,true,false)||!field(j,"refresh_token",SH_AUTH_TOKEN_MAX,false,false)){cJSON_Delete(j);return ESP_ERR_INVALID_RESPONSE;}
    const char *access=string(j,"access_token"),*refresh=string(j,"refresh_token");
    if(!refresh&&id==SH_CURSOR)refresh=access;
    cJSON *expires=cJSON_GetObjectItemCaseSensitive(j,"expires_in");time_t expiry=0;
    if(expires){double seconds=expires->valuedouble;if(!cJSON_IsNumber(expires)||!isfinite(seconds)||seconds<1||seconds>31536000||floor(seconds)!=seconds){cJSON_Delete(j);return ESP_ERR_INVALID_RESPONSE;}expiry=time(NULL)+(time_t)seconds;}
    char *raw=NULL;xSemaphoreTake(lock,portMAX_DELAY);
    if(revisions[id]!=c->revision||!network_current(generation))err=ESP_ERR_INVALID_STATE;else err=read_locked(id,&raw);
    cJSON *stored=err==ESP_OK?parse(raw,strlen(raw)):NULL;free(raw);
    if(err==ESP_OK&&!stored)err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK){
        cJSON_DeleteItemFromObjectCaseSensitive(stored,"access_token");bool ok=cJSON_AddStringToObject(stored,"access_token",access)!=NULL;
        if(refresh){cJSON_DeleteItemFromObjectCaseSensitive(stored,"refresh_token");ok=ok&&cJSON_AddStringToObject(stored,"refresh_token",refresh)!=NULL;}
        cJSON_DeleteItemFromObjectCaseSensitive(stored,"expires_at");ok=ok&&cJSON_AddNumberToObject(stored,"expires_at",(double)expiry)!=NULL;
        char *serialized=ok?cJSON_PrintUnformatted(stored):NULL;if(!serialized)err=ESP_ERR_NO_MEM;else{
            if(!network_current(generation))err=ESP_ERR_INVALID_STATE;else err=write_locked(id,serialized);
            free(serialized);
        }
    }
    cJSON_Delete(stored);xSemaphoreGive(lock);cJSON_Delete(j);
    if(err==ESP_OK){sh_auth_release(c);err=sh_auth_load(id,c);}return err;
}
