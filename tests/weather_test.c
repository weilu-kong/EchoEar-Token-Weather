#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sh_weather_parse.h"

static const char valid[] =
    "{\"current\":{\"time\":1791410400,\"temperature_2m\":22.4,"
    "\"apparent_temperature\":23.1,\"relative_humidity_2m\":68,"
    "\"weather_code\":97,\"is_day\":1},\"daily\":{"
    "\"time\":[1791385200],\"temperature_2m_max\":[25.2],"
    "\"temperature_2m_min\":[19.8],\"precipitation_probability_max\":[30]}}";

int main(void)
{
    sh_weather_t weather = {0};
    assert(sh_weather_parse(valid, strlen(valid), &weather));
    assert(weather.valid && !weather.stale && weather.is_day);
    assert(weather.updated_at == (time_t)1791410400 && weather.rain == 30);
    assert(weather.temp > 22.3f && weather.temp < 22.5f && weather.code == 97);
    sh_weather_t old = weather;
    const char *bad[] = {"{}", "{\"current\":null}", "{", "[]"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!sh_weather_parse(bad[i], strlen(bad[i]), &weather));
        assert(weather.valid && weather.stale && weather.temp == old.temp);
        assert(weather.updated_at == old.updated_at && weather.rain == old.rain);
    }
    assert(!sh_weather_parse(valid, strlen(valid) - 1, &weather));
    assert(!sh_weather_parse(valid, SH_WEATHER_RESPONSE_MAX + 1, &weather));
    char changed[sizeof(valid) + 8];
    const char *invalid_codes[] = {"52", "54", "62", "72", "98"};
    for (size_t i = 0; i < sizeof(invalid_codes) / sizeof(invalid_codes[0]); ++i) {
        strcpy(changed, valid);
        char *code = strstr(changed, "\"weather_code\":97");
        assert(code);
        memcpy(code + strlen("\"weather_code\":"), invalid_codes[i], 2);
        assert(!sh_weather_parse(changed, strlen(changed), &weather));
        assert(weather.valid && weather.stale && weather.code == old.code);
        assert(weather.temp == old.temp && weather.updated_at == old.updated_at);
    }
    strcpy(changed, valid);
    char *humidity = strstr(changed, "68");
    assert(humidity);
    memcpy(humidity, "-1", 2);
    assert(!sh_weather_parse(changed, strlen(changed), &weather));
    strcpy(changed, valid);
    strcat(changed, "garbage");
    assert(!sh_weather_parse(changed, strlen(changed), &weather));
    weather = (sh_weather_t){0};
    assert(!sh_weather_parse("{}", 2, &weather));
    assert(!weather.valid && weather.stale);
    puts("weather parser: OK");
    return 0;
}
