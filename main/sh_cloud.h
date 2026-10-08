#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

typedef enum {SH_CODEX,SH_ANTIGRAVITY,SH_CLAUDE,SH_CURSOR,SH_GOOGLE,SH_ACCOUNT_COUNT} sh_account_id_t;
typedef enum {SH_CLOUD_UNCONFIGURED,SH_CLOUD_LOADING,SH_CLOUD_READY,SH_CLOUD_STALE,SH_CLOUD_AUTH_REQUIRED,SH_CLOUD_RATE_LIMITED,SH_CLOUD_ERROR} sh_cloud_state_t;
#define SH_CLOUD_ERR_AUTH ((esp_err_t)0x7c01)
#define SH_CLOUD_ERR_RATE_LIMIT ((esp_err_t)0x7c02)
#define SH_CLOUD_ERR_UNSUPPORTED ((esp_err_t)0x7c03)
typedef struct {
    sh_cloud_state_t state;
    bool valid,unlimited,has_percent,has_secondary,has_balance,has_total;
    float remaining_percent,secondary_percent;
    double balance,total;
    int window_minutes,secondary_minutes;
    time_t reset_at,secondary_reset_at,updated_at;
    char unit[12],window_label[32];
} sh_service_data_t;
#define SH_CALENDAR_EVENTS 12
#define SH_EVENT_TITLE_BYTES 128
#define SH_EVENT_LOCATION_BYTES 128
#define SH_EVENT_DESCRIPTION_BYTES 384
typedef struct {
    char title[SH_EVENT_TITLE_BYTES],location[SH_EVENT_LOCATION_BYTES],description[SH_EVENT_DESCRIPTION_BYTES];
    time_t start,end;
    bool all_day;
} sh_calendar_event_t;
typedef struct {
    sh_cloud_state_t state;
    bool valid;
    time_t updated_at;
    uint8_t count;
    sh_calendar_event_t events[SH_CALENDAR_EVENTS];
} sh_calendar_data_t;
typedef struct {sh_service_data_t services[4];sh_calendar_data_t calendar;uint32_t revision;} sh_cloud_snapshot_t;
esp_err_t sh_cloud_init(void);
void sh_cloud_get_snapshot(sh_cloud_snapshot_t *out);
void sh_cloud_refresh(void);
esp_err_t sh_cloud_import_account(const char *json,size_t length);
esp_err_t sh_cloud_forget_account(sh_account_id_t id);
