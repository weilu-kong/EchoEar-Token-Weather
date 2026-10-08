#pragma once
#include "sh_cloud.h"
#define SH_AUTH_TOKEN_MAX 4096
#define SH_AUTH_JSON_MAX 12288
typedef struct {
    char *access_token,*refresh_token,*client_id,*client_secret,*account_id,*project,*scope,*team_id;
    time_t expires_at;
    uint32_t revision;
} sh_account_credentials_t;
esp_err_t sh_auth_init(void);
esp_err_t sh_auth_load(sh_account_id_t id,sh_account_credentials_t *out);
/* Current credential revision, sampled under the auth mutex. */
uint32_t sh_auth_revision(sh_account_id_t id);
void sh_auth_release(sh_account_credentials_t *credentials);
esp_err_t sh_auth_import(const char *json,size_t length);
esp_err_t sh_auth_forget(sh_account_id_t id);
esp_err_t sh_auth_refresh(sh_account_id_t id,sh_account_credentials_t *credentials,uint32_t network_generation);
