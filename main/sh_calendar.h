#pragma once
#include "sh_auth.h"

/* All-day dates use the device timezone; failed parses leave out untouched. */
bool sh_calendar_parse(const char *json, size_t length, time_t now, sh_calendar_data_t *out);
esp_err_t sh_calendar_fetch(const sh_account_credentials_t *credentials,
                            uint32_t network_generation, sh_calendar_data_t *out);
