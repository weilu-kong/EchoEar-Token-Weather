#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    float temp, feels, high, low;
    int humidity, rain, code;
    bool valid, stale, is_day;
    time_t updated_at;
} sh_weather_t;

typedef struct { int32_t remaining, total; bool has_total; } sh_quota_t;
int sh_quota_percent(const sh_quota_t *quota);
const char *sh_weather_condition(int code);
const char *sh_weather_icon(int code, bool is_day, bool valid);
