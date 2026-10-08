#include <assert.h>
#include <string.h>
#include "sh_model.h"
int main(void) {
    sh_quota_t q = {0, 500, true};
    assert(sh_quota_percent(&q) == 0);
    q.remaining = 500;
    assert(sh_quota_percent(&q) == 100);
    q.remaining = 1000000000; q.total = 1000000000;
    assert(sh_quota_percent(&q) == 100);
    q.has_total = false;
    assert(sh_quota_percent(&q) == -1);
    q.has_total = true; q.total = 0;
    assert(sh_quota_percent(&q) == -1);
    assert(!strcmp(sh_weather_condition(52), "不明"));
    assert(!strcmp(sh_weather_icon(52, true, true), "unknown"));
    assert(!strcmp(sh_weather_icon(97, true, true), "rain"));
    assert(!strcmp(sh_weather_icon(0, false, true), "moon"));
    assert(!strcmp(sh_weather_icon(0, true, false), "unknown"));
    return 0;
}
