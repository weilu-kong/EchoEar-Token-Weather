#include "sh_cloud.h"
#include "sh_auth.h"
#include "sh_ai.h"
#include "sh_calendar.h"
#include "sh_network.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static SemaphoreHandle_t lock;
static TaskHandle_t worker;
static sh_cloud_snapshot_t snapshot;
static uint32_t epoch;
_Static_assert(SH_GOOGLE==4,"Service snapshot indexes must match provider IDs");
static const char *names[]={"codex","antigravity","claude","cursor","google"};
static sh_cloud_state_t failed_state(esp_err_t error,bool valid){
    if(error==SH_CLOUD_ERR_AUTH)return SH_CLOUD_AUTH_REQUIRED;
    if(error==SH_CLOUD_ERR_RATE_LIMIT)return SH_CLOUD_RATE_LIMITED;
    return valid?SH_CLOUD_STALE:SH_CLOUD_ERROR;
}
static void fetch_account(int id,uint32_t generation,sh_calendar_data_t *calendar){
    sh_account_credentials_t credentials={0};
    xSemaphoreTake(lock,portMAX_DELAY);uint32_t request_epoch=epoch;xSemaphoreGive(lock);
    esp_err_t err=sh_auth_load(id,&credentials);
    bool missing=err==ESP_ERR_NVS_NOT_FOUND;
    sh_service_data_t service={0};memset(calendar,0,sizeof(*calendar));
    bool refreshed=false;
    if(err==ESP_OK){
        if(credentials.expires_at&&credentials.expires_at<=time(NULL)+60){
            refreshed=true;err=sh_auth_refresh(id,&credentials,generation);
        }
        if(err==ESP_OK)err=id==SH_GOOGLE?sh_calendar_fetch(&credentials,generation,calendar):sh_ai_fetch(id,&credentials,generation,&service);
        if(err==SH_CLOUD_ERR_AUTH&&!refreshed&&credentials.refresh_token){
            err=sh_auth_refresh(id,&credentials,generation);
            if(err==ESP_OK)err=id==SH_GOOGLE?sh_calendar_fetch(&credentials,generation,calendar):sh_ai_fetch(id,&credentials,generation,&service);
        }
    }
    ESP_LOGI("sh_cloud","provider=%s result=%s valid=%d stack=%u",names[id],esp_err_to_name(err),id==SH_GOOGLE?calendar->valid:service.valid,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    uint32_t credential_revision=credentials.revision;
    sh_auth_release(&credentials);
    sh_net_snapshot_t net;sh_network_get_snapshot(&net);
    xSemaphoreTake(lock,portMAX_DELAY);
    if(epoch==request_epoch&&net.wifi_connected&&net.generation==generation&&
       (missing||credential_revision==sh_auth_revision(id))){
        if(id==SH_GOOGLE){
            if(err==ESP_OK){snapshot.calendar=*calendar;snapshot.calendar.valid=true;snapshot.calendar.updated_at=time(NULL);snapshot.calendar.state=SH_CLOUD_READY;}
            else snapshot.calendar.state=missing?SH_CLOUD_UNCONFIGURED:failed_state(err,snapshot.calendar.valid);
        }else{
            if(err==ESP_OK){snapshot.services[id]=service;snapshot.services[id].valid=true;snapshot.services[id].updated_at=time(NULL);snapshot.services[id].state=SH_CLOUD_READY;}
            else snapshot.services[id].state=missing?SH_CLOUD_UNCONFIGURED:failed_state(err,snapshot.services[id].valid);
        }
        ++snapshot.revision;
    }
    xSemaphoreGive(lock);
}
static void cloud_task(void *arg){
    sh_calendar_data_t *calendar=arg;
    bool connected=false;uint32_t generation=0;int64_t due=0;
    for(;;){
        bool requested=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000))>0;
        sh_net_snapshot_t net;sh_network_get_snapshot(&net);
        if(!net.wifi_connected){
            if(connected){xSemaphoreTake(lock,portMAX_DELAY);for(int i=0;i<4;i++)if(snapshot.services[i].valid)snapshot.services[i].state=SH_CLOUD_STALE;
                if(snapshot.calendar.valid)snapshot.calendar.state=SH_CLOUD_STALE;
                ++snapshot.revision;xSemaphoreGive(lock);}
            connected=false;continue;
        }
        if(!net.time_synced)continue;
        if(requested||!connected||generation!=net.generation||esp_timer_get_time()>=due){
            connected=true;generation=net.generation;
            for(int i=0;i<SH_ACCOUNT_COUNT;i++){
                sh_network_get_snapshot(&net);if(!net.wifi_connected||net.generation!=generation)break;
                fetch_account(i,generation,calendar);
            }
            due=esp_timer_get_time()+300000000LL;
        }
    }
}
esp_err_t sh_cloud_init(void){
    if(lock)return ESP_ERR_INVALID_STATE;
    esp_err_t err=sh_auth_init();if(err!=ESP_OK)return err;
    lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    snapshot.revision=1;
    sh_calendar_data_t *calendar=heap_caps_malloc(sizeof(*calendar),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!calendar)return ESP_ERR_NO_MEM;
    if(xTaskCreate(cloud_task,"sh_cloud",12288,calendar,3,&worker)==pdPASS)return ESP_OK;
    free(calendar);return ESP_ERR_NO_MEM;
}
void sh_cloud_get_snapshot(sh_cloud_snapshot_t *out){
    if(!out)return;
    if(!lock){memset(out,0,sizeof(*out));return;}
    xSemaphoreTake(lock,portMAX_DELAY);*out=snapshot;xSemaphoreGive(lock);
}
void sh_cloud_refresh(void){if(worker)xTaskNotifyGive(worker);}
esp_err_t sh_cloud_import_account(const char *json,size_t length){
    if(!lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);esp_err_t err=sh_auth_import(json,length);
    if(err==ESP_OK){
        ++epoch;
        cJSON *record=cJSON_ParseWithLength(json,length);
        cJSON *provider=cJSON_GetObjectItemCaseSensitive(record,"provider");
        for(int i=0;cJSON_IsString(provider)&&i<SH_ACCOUNT_COUNT;i++)if(!strcmp(provider->valuestring,names[i])){
            if(i==SH_GOOGLE){memset(&snapshot.calendar,0,sizeof(snapshot.calendar));snapshot.calendar.state=SH_CLOUD_LOADING;}
            else {memset(&snapshot.services[i],0,sizeof(snapshot.services[i]));snapshot.services[i].state=SH_CLOUD_LOADING;}
        }
        cJSON_Delete(record);++snapshot.revision;
    }
    xSemaphoreGive(lock);if(err==ESP_OK)sh_cloud_refresh();return err;
}
esp_err_t sh_cloud_forget_account(sh_account_id_t id){
    if(!lock||id<0||id>=SH_ACCOUNT_COUNT)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock,portMAX_DELAY);esp_err_t err=sh_auth_forget(id);
    if(err==ESP_OK){++epoch;if(id==SH_GOOGLE)memset(&snapshot.calendar,0,sizeof(snapshot.calendar));else memset(&snapshot.services[id],0,sizeof(snapshot.services[id]));++snapshot.revision;}
    xSemaphoreGive(lock);return err;
}
