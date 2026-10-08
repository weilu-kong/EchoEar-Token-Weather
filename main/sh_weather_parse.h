#pragma once
#include <stddef.h>
#include "sh_model.h"

#define SH_WEATHER_RESPONSE_MAX 8192

/* Failure preserves the previous reading and marks it stale. */
bool sh_weather_parse(const char *json, size_t length, sh_weather_t *weather);
