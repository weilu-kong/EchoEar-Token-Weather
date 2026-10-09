/* Deterministic cloud publication checks; no threads, NVS, HTTP or real tokens. */
#include <assert.h>
#include <stdio.h>
#include "../main/sh_cloud.c"

static int mutex;
static sh_net_snapshot_t network;
static uint32_t revisions[SH_ACCOUNT_COUNT];
static esp_err_t load_result, fetch_result, refresh_result, write_result;
static unsigned fetch_calls, refresh_calls, notifications;
static bool expired,first_auth;
static void (*during_fetch)(sh_account_id_t);

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait) {
    (void)wait; assert(!*m); *m=1; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { assert(*m); *m=0; return pdTRUE; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    assert(0 && "Host fixture must not start a worker"); return 0;
}
unsigned ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    (void)clear; (void)wait; assert(0 && "Host fixture must not run worker loop"); return 0;
}
void xTaskNotifyGive(TaskHandle_t task) { assert(task); ++notifications; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 0; }
int64_t esp_timer_get_time(void) { return 0; }
void sh_network_get_snapshot(sh_net_snapshot_t *out) { *out=network; }
esp_err_t sh_auth_init(void) { return ESP_OK; }
uint32_t sh_auth_revision(sh_account_id_t id) { return revisions[id]; }
esp_err_t sh_auth_load(sh_account_id_t id, sh_account_credentials_t *out) {
    out->revision=revisions[id]; out->refresh_token="synthetic-refresh";
    out->expires_at=expired ? time(NULL)-1 : 0; return load_result;
}
void sh_auth_release(sh_account_credentials_t *c) { memset(c,0,sizeof(*c)); }
esp_err_t sh_auth_refresh(sh_account_id_t id, sh_account_credentials_t *c, uint32_t generation) {
    assert(generation==7); ++refresh_calls;
    if(refresh_result==ESP_OK) { c->revision=++revisions[id]; c->expires_at=0; }
    return refresh_result;
}
esp_err_t sh_auth_forget(sh_account_id_t id) {
    if(write_result==ESP_OK)++revisions[id]; return write_result;
}
esp_err_t sh_auth_import(const char *json, size_t length) {
    cJSON *j=cJSON_ParseWithLength(json,length);
    int id=!strcmp(cJSON_GetObjectItem(j,"provider")->valuestring,"google")?SH_GOOGLE:SH_CODEX;
    cJSON_Delete(j); if(write_result==ESP_OK)++revisions[id]; return write_result;
}
static esp_err_t fetch(sh_account_id_t id, uint32_t generation) {
    assert(generation==7); ++fetch_calls;
    if(during_fetch)during_fetch(id);
    return first_auth&&fetch_calls==1?SH_CLOUD_ERR_AUTH:fetch_result;
}
esp_err_t sh_ai_fetch(sh_account_id_t id, const sh_account_credentials_t *c,
                      uint32_t generation, sh_service_data_t *out) {
    (void)c; *out=(sh_service_data_t){.valid=true,.has_percent=true,.remaining_percent=90};
    return fetch(id,generation);
}
esp_err_t sh_calendar_fetch(const sh_account_credentials_t *c, uint32_t generation,
                            sh_calendar_data_t *out) {
    (void)c; out->valid=true; out->count=1; strcpy(out->events[0].title,"synthetic event");
    return fetch(SH_GOOGLE,generation);
}
static void reset(bool valid) {
    assert(!mutex); lock=&mutex; worker=&mutex; epoch=0;
    memset(&snapshot,0,sizeof(snapshot)); snapshot.revision=1;
    for(int i=0;i<4;i++)snapshot.services[i]=(sh_service_data_t){.valid=valid,.state=SH_CLOUD_READY,.remaining_percent=33,.updated_at=123};
    snapshot.calendar.valid=valid; snapshot.calendar.state=SH_CLOUD_READY;
    snapshot.calendar.count=valid?1:0; strcpy(snapshot.calendar.events[0].title,"old event");
    memset(revisions,0,sizeof(revisions));
    network=(sh_net_snapshot_t){.wifi_connected=true,.generation=7,.time_synced=true};
    load_result=fetch_result=refresh_result=write_result=ESP_OK;
    fetch_calls=refresh_calls=notifications=0; expired=first_auth=false; during_fetch=NULL;
}
static void request(sh_account_id_t id) { sh_calendar_data_t calendar; fetch_account(id,7,&calendar); }
static sh_cloud_state_t state(sh_account_id_t id) { return id==SH_GOOGLE?snapshot.calendar.state:snapshot.services[id].state; }
static void retained(sh_account_id_t id) {
    if(id==SH_GOOGLE)assert(snapshot.calendar.valid && !strcmp(snapshot.calendar.events[0].title,"old event"));
    else assert(snapshot.services[id].valid && snapshot.services[id].remaining_percent==33 && snapshot.services[id].updated_at==123);
}
static void disconnected(sh_account_id_t id) { (void)id; network.wifi_connected=false; }
static void reconnected(sh_account_id_t id) { (void)id; ++network.generation; }
static void revised(sh_account_id_t id) { ++revisions[id]; }
static void forgotten(sh_account_id_t id) { assert(sh_cloud_forget_account(id)==ESP_OK); }
static void imported(sh_account_id_t id) {
    const char *record=id==SH_GOOGLE?"{\"provider\":\"google\"}":"{\"provider\":\"codex\"}";
    assert(sh_cloud_import_account(record,strlen(record))==ESP_OK);
}
static void imported_other(sh_account_id_t id) { imported(id==SH_GOOGLE?SH_CODEX:SH_GOOGLE); }
int main(void) {
    for(int id=SH_CODEX;id<SH_ACCOUNT_COUNT;id++) {
        reset(false); request(id); assert(state(id)==SH_CLOUD_READY && snapshot.revision==2);
        if(id==SH_GOOGLE)assert(snapshot.calendar.valid && !strcmp(snapshot.calendar.events[0].title,"synthetic event"));
        else assert(snapshot.services[id].valid && snapshot.services[id].remaining_percent==90);
        const esp_err_t failures[]={SH_CLOUD_ERR_AUTH,SH_CLOUD_ERR_RATE_LIMIT,ESP_ERR_TIMEOUT,SH_CLOUD_ERR_UNSUPPORTED};
        for(unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
            for(int valid=0;valid<=1;valid++) {
                reset(valid); fetch_result=refresh_result=failures[i]; request(id);
                sh_cloud_state_t expected=i==0?SH_CLOUD_AUTH_REQUIRED:i==1?SH_CLOUD_RATE_LIMITED:valid?SH_CLOUD_STALE:SH_CLOUD_ERROR;
                assert(state(id)==expected); assert(snapshot.revision==2);
                if(valid)retained(id);
            }
        }
        reset(false); load_result=ESP_ERR_NVS_NOT_FOUND; request(id);
        assert(state(id)==SH_CLOUD_UNCONFIGURED && fetch_calls==0);
        reset(false); expired=true; request(id); assert(refresh_calls==1 && fetch_calls==1 && state(id)==SH_CLOUD_READY);
        reset(false); first_auth=true; request(id);
        assert(refresh_calls==1 && fetch_calls==2 && state(id)==SH_CLOUD_READY);
        reset(true); expired=true; refresh_result=SH_CLOUD_ERR_AUTH; request(id);
        assert(refresh_calls==1 && fetch_calls==0 && state(id)==SH_CLOUD_AUTH_REQUIRED); retained(id);
        reset(true); expired=true; fetch_result=SH_CLOUD_ERR_AUTH; request(id);
        assert(refresh_calls==1 && fetch_calls==1 && state(id)==SH_CLOUD_AUTH_REQUIRED); retained(id);
        reset(true); fetch_result=SH_CLOUD_ERR_AUTH; request(id);
        assert(refresh_calls==1 && fetch_calls==2 && state(id)==SH_CLOUD_AUTH_REQUIRED); retained(id);
        void (*changes[])(sh_account_id_t)={disconnected,reconnected,revised};
        for(unsigned i=0;i<sizeof(changes)/sizeof(changes[0]);i++) {
            reset(true); sh_cloud_snapshot_t old=snapshot; during_fetch=changes[i]; request(id);
            assert(!memcmp(&old,&snapshot,sizeof(old)));
        }
        reset(true); during_fetch=forgotten; request(id);
        assert(state(id)==SH_CLOUD_UNCONFIGURED && snapshot.revision==2);
        assert(id==SH_GOOGLE?!snapshot.calendar.valid:!snapshot.services[id].valid);
        for(int other=0;other<SH_ACCOUNT_COUNT;other++)if(other!=id)retained(other);
        reset(true); write_result=ESP_FAIL; sh_cloud_snapshot_t old=snapshot;
        assert(sh_cloud_forget_account(id)==ESP_FAIL && !memcmp(&old,&snapshot,sizeof(old)));
    }
    for(int id=SH_CODEX;id<=SH_GOOGLE;id+=SH_GOOGLE) {
        reset(true); during_fetch=imported; request(id);
        assert(state(id)==SH_CLOUD_LOADING && snapshot.revision==2 && notifications==1);
        assert(id==SH_GOOGLE?!snapshot.calendar.valid:!snapshot.services[id].valid);
        assert(snapshot.services[SH_CURSOR].valid && snapshot.services[SH_CURSOR].remaining_percent==33);
        reset(true); during_fetch=imported_other; request(id);
        retained(id); assert(state(id)==SH_CLOUD_READY);
        assert(state(id==SH_GOOGLE?SH_CODEX:SH_GOOGLE)==SH_CLOUD_LOADING);
        assert(snapshot.revision==2 && notifications==1);
        reset(true); write_result=ESP_FAIL; sh_cloud_snapshot_t old=snapshot;
        const char *record="{\"provider\":\"codex\"}";
        assert(sh_cloud_import_account(record,strlen(record))==ESP_FAIL);
        assert(!memcmp(&old,&snapshot,sizeof(old)) && notifications==0);
    }
    puts("cloud lifecycle: OK (failure retention, refresh bounds, late result rejection, account isolation)");
    return 0;
}
