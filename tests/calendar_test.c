/* Offline parser fixture; HTTP and hardware acceptance remain excluded. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh_calendar.h"
#include "sh_http.h"

/* Keeps this parser fixture offline when linked with sh_calendar.c. */
esp_err_t sh_http_request(const char *method, const char *url, const sh_http_header_t *headers,
                        size_t header_count, const char *body, char *response,
                        size_t capacity, int *status, uint32_t generation)
{
    (void)method; (void)url; (void)headers; (void)header_count; (void)body;
    (void)response; (void)capacity; (void)status; (void)generation;
    assert(!"Calendar parser fixtures must never use HTTP");
    return ESP_FAIL;
}

static size_t events_json(char *json, size_t capacity, unsigned count)
{
    size_t used = 0;
    int written = snprintf(json, capacity, "{\"items\":[");
    assert(written > 0 && (size_t)written < capacity);
    used = (size_t)written;
    for (unsigned i = 0; i < count; ++i) {
        unsigned hour = count - i;
        written = snprintf(json + used, capacity - used,
            "%s{\"status\":\"confirmed\",\"summary\":\"event%02u\","
            "\"start\":{\"dateTime\":\"2024-01-01T%02u:00:00Z\"},"
            "\"end\":{\"dateTime\":\"2024-01-01T%02u:00:00Z\"}}",
            i ? "," : "", hour, hour, hour + 1);
        assert(written > 0 && (size_t)written < capacity - used);
        used += (size_t)written;
    }
    written = snprintf(json + used, capacity - used, "]}");
    assert(written == 2 && (size_t)written < capacity - used);
    return used + (size_t)written;
}

int main(void)
{
    assert(setenv("TZ", "JST-9", 1) == 0);
    tzset();
    const time_t now = 1704067200; /* 2024-01-01 00:00 UTC */
    const char good[] = "{\"items\":["
        "{\"status\":\"confirmed\",\"summary\":\"偏移\",\"location\":\"room\\nA\","
        "\"start\":{\"dateTime\":\"2024-01-01T06:15:00.123456+05:30\"},"
        "\"end\":{\"dateTime\":\"2024-01-01T07:15:00+05:30\"}},"
        "{\"status\":\"tentative\",\"summary\":\"进行中\","
        "\"start\":{\"dateTime\":\"2023-12-31T18:00:00-05:00\"},"
        "\"end\":{\"dateTime\":\"2024-01-01T01:00:00Z\"}},"
        "{\"status\":\"confirmed\",\"start\":{\"date\":\"2024-01-02\"},"
        "\"end\":{\"date\":\"2024-01-03\"}}]}";
    sh_calendar_data_t data = {0};
    assert(sh_calendar_parse(good, strlen(good), now, &data));
    assert(data.valid && data.state == SH_CLOUD_READY && data.count == 3 && data.updated_at == now);
    assert(data.events[0].start == 1704063600 && data.events[0].end == 1704070800);
    assert(data.events[1].start == 1704069900 && !strcmp(data.events[1].location, "room A"));
    assert(data.events[2].all_day && data.events[2].start == 1704121200 &&
           data.events[2].end == 1704207600 && !strcmp(data.events[2].title, "(Untitled)"));
    sh_calendar_data_t previous = data;
    const char *bad[] = {
        "{}", "[]", "{\"items\":null}", "{\"items\":[]}garbage",
        "{\"items\":[{\"status\":\"confirmed\",\"start\":{\"date\":\"2024-02-30\"},\"end\":{\"date\":\"2024-03-01\"}}]}",
        "{\"items\":[{\"status\":\"confirmed\",\"start\":{\"dateTime\":\"2024-01-01T00:00:00+24:00\"},\"end\":{\"dateTime\":\"2024-01-01T01:00:00Z\"}}]}",
        "{\"items\":[{\"status\":\"confirmed\",\"start\":{\"dateTime\":\"2024-01-01T00:00:00\"},\"end\":{\"dateTime\":\"2024-01-01T01:00:00Z\"}}]}",
        "{\"items\":[{\"status\":\"confirmed\",\"start\":{\"date\":\"2024-01-02\"},\"end\":{\"dateTime\":\"2024-01-03T00:00:00Z\"}}]}",
        "{\"items\":[{\"status\":\"confirmed\",\"summary\":\"bad\\u0000text\",\"start\":{\"date\":\"2024-01-02\"},\"end\":{\"date\":\"2024-01-03\"}}]}",
        "{\"items\":[{\"status\":\"confirmed\",\"summary\":\"\xc0\xaf\",\"start\":{\"date\":\"2024-01-02\"},\"end\":{\"date\":\"2024-01-03\"}}]}",
        "{\"items\":[{\"status\":\"unknown\"}]}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!sh_calendar_parse(bad[i], strlen(bad[i]), now, &data));
        assert(!memcmp(&data, &previous, sizeof(data)));
    }
    assert(!sh_calendar_parse(good, strlen(good) - 1, now, &data));
    assert(!memcmp(&data, &previous, sizeof(data)));
    assert(!sh_calendar_parse(good, SH_CLOUD_BODY_MAX + 1, now, &data));
    assert(!memcmp(&data, &previous, sizeof(data)));
    assert(!sh_calendar_parse(good, strlen(good), 0, &data));
    assert(!memcmp(&data, &previous, sizeof(data)));
    const char excluded[] = "{\"items\":[{\"status\":\"cancelled\"},"
        "{\"status\":\"confirmed\",\"start\":{\"dateTime\":\"2023-12-31T23:00:00Z\"},\"end\":{\"dateTime\":\"2024-01-01T00:00:00Z\"}},"
        "{\"status\":\"confirmed\",\"start\":{\"dateTime\":\"2024-01-08T00:00:00Z\"},\"end\":{\"dateTime\":\"2024-01-08T01:00:00Z\"}}]}";
    assert(sh_calendar_parse(excluded, strlen(excluded), now, &data) && data.count == 0);
    const char empty[] = "{\"items\":[]}";
    assert(sh_calendar_parse(empty, strlen(empty), now, &data) && data.valid && data.count == 0);
    const char omitted_empty[] = "{\"kind\":\"calendar#events\"}";
    assert(sh_calendar_parse(omitted_empty, strlen(omitted_empty), now, &data) && data.count == 0);
    char long_title[1200];
    strcpy(long_title, "{\"items\":[{\"status\":\"confirmed\",\"summary\":\"");
    for (unsigned i = 0; i < 80; ++i) strcat(long_title, "中文");
    strcat(long_title, "\",\"start\":{\"date\":\"2024-01-02\"},\"end\":{\"date\":\"2024-01-03\"}}]}");
    assert(sh_calendar_parse(long_title, strlen(long_title), now, &data));
    assert(strlen(data.events[0].title) == 126 &&
           !memcmp(data.events[0].title + 123, "文", 3));
    const char leap[] = "{\"items\":[{\"status\":\"confirmed\",\"start\":{\"date\":\"2024-02-29\"},\"end\":{\"date\":\"2024-03-01\"}}]}";
    assert(sh_calendar_parse(leap, strlen(leap), 1709074800, &data) &&
           data.count == 1 && data.events[0].all_day && data.events[0].end - data.events[0].start == 86400);
    assert(SH_CALENDAR_EVENTS == 12);
    char capacity_json[4096];
    size_t length = events_json(capacity_json, sizeof(capacity_json), 12);
    assert(sh_calendar_parse(capacity_json, length, now, &data));
    assert(data.count == 12 && data.valid && data.state == SH_CLOUD_READY && data.updated_at == now);
    for (unsigned i = 0; i < 12; ++i) {
        char title[16];
        snprintf(title, sizeof(title), "event%02u", i + 1);
        assert(!strcmp(data.events[i].title, title) && !data.events[i].all_day);
        assert(data.events[i].start == now + (i + 1) * 3600 &&
               data.events[i].end == now + (i + 2) * 3600);
    }
    previous = data;
    length = events_json(capacity_json, sizeof(capacity_json), 13);
    assert(!sh_calendar_parse(capacity_json, length, now, &data));
    assert(!memcmp(&data, &previous, sizeof(data)));
    printf("calendar parser: OK (12 retained, 13 rejected; event=%zu, snapshot=%zu bytes)\n",
           sizeof(sh_calendar_event_t), sizeof(sh_calendar_data_t));
    return 0;
}
