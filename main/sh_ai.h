#pragma once
#include "sh_auth.h"
/* Failed parsing/fetching leaves out unchanged. */
bool sh_ai_parse(sh_account_id_t id, const char *json, size_t length, sh_service_data_t *out);
/* Cursor forwards team_id; TEAM labels the member's included usage, not a pooled team budget. */
esp_err_t sh_ai_fetch(sh_account_id_t id, const sh_account_credentials_t *credentials,
                      uint32_t network_generation, sh_service_data_t *out);
