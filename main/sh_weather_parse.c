#include "sh_weather_parse.h"
#include <ctype.h>
#include <math.h>
#include "cJSON.h"

static bool number(const cJSON *item, double min, double max, double *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < min || item->valuedouble > max) return false;
    *out = item->valuedouble;
    return true;
}

static const cJSON *day_value(const cJSON *daily, const char *key)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(daily, key);
    return cJSON_IsArray(array) && cJSON_GetArraySize(array) == 1 ?
        cJSON_GetArrayItem(array, 0) : NULL;
}

static bool weather_code(double code)
{
    const int codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57,
                        61, 63, 65, 66, 67, 71, 73, 75, 77,
                        80, 81, 82, 85, 86, 95, 96, 97, 99};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
        if (code == codes[i]) return true;
    return false;
}

bool sh_weather_parse(const char *json, size_t length, sh_weather_t *weather)
{
    if (!weather) return false;
    weather->stale = true;
    if (!json || !length || length > SH_WEATHER_RESPONSE_MAX) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return false;
    while (end < json + length && isspace((unsigned char)*end)) ++end;
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    double temp, feels, high, low, humidity, rain, code, is_day, timestamp, day;
    bool valid = cJSON_IsObject(root) && end == json + length &&
        cJSON_IsObject(current) && cJSON_IsObject(daily) &&
        number(cJSON_GetObjectItemCaseSensitive(current, "temperature_2m"), -100, 70, &temp) &&
        number(cJSON_GetObjectItemCaseSensitive(current, "apparent_temperature"), -150, 100, &feels) &&
        number(cJSON_GetObjectItemCaseSensitive(current, "relative_humidity_2m"), 0, 100, &humidity) &&
        floor(humidity) == humidity &&
        number(cJSON_GetObjectItemCaseSensitive(current, "weather_code"), 0, 99, &code) &&
        weather_code(code) &&
        number(cJSON_GetObjectItemCaseSensitive(current, "is_day"), 0, 1, &is_day) &&
        floor(is_day) == is_day &&
        number(cJSON_GetObjectItemCaseSensitive(current, "time"), 946684800, 4102444800, &timestamp) &&
        floor(timestamp) == timestamp &&
        number(day_value(daily, "time"), 946684800, 4102444800, &day) &&
        floor(day) == day && timestamp >= day && timestamp < day + 86400 &&
        number(day_value(daily, "temperature_2m_max"), -100, 70, &high) &&
        number(day_value(daily, "temperature_2m_min"), -100, 70, &low) && low <= high &&
        number(day_value(daily, "precipitation_probability_max"), 0, 100, &rain);
    if (valid) {
        *weather = (sh_weather_t){
            .temp = temp, .feels = feels, .high = high, .low = low,
            .humidity = (int)humidity, .rain = (int)lround(rain), .code = (int)code,
            .valid = true, .stale = false, .is_day = is_day == 1,
            .updated_at = (time_t)timestamp,
        };
    }
    cJSON_Delete(root);
    return valid;
}
