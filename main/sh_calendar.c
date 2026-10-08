#include "sh_calendar.h"
#include "sh_http.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"

static bool bounded_json(const char *json, size_t length)
{
    unsigned depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)json[i];
        if (!c) return false;
        if (quoted) {
            if (c < 0x20) return false;
            if (c == '\\') {
                if (++i >= length) return false;
                if (json[i] == 'u' && i + 4 < length &&
                    !memcmp(json + i + 1, "0000", 4)) return false;
            } else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && !depth;
}

static bool digits(const char *s, unsigned count, int *out)
{
    int value = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + s[i] - '0';
    }
    *out = value;
    return true;
}

static bool event_time(const cJSON *item, time_t *out, bool *all_day)
{
    if (!cJSON_IsObject(item)) return false;
    const cJSON *datetime = cJSON_GetObjectItemCaseSensitive(item, "dateTime");
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
    if ((datetime && date) || (!datetime && !date)) return false;
    const cJSON *value = datetime ? datetime : date;
    if (!cJSON_IsString(value)) return false;
    const char *s = value->valuestring;
    size_t length = strlen(s);
    int y, m, d, h = 0, min = 0, sec = 0;
    if (length < 10 || !digits(s, 4, &y) || s[4] != '-' ||
        !digits(s + 5, 2, &m) || s[7] != '-' || !digits(s + 8, 2, &d) ||
        y < 1 || m < 1 || m > 12) return false;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int month_days = days[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (d < 1 || d > month_days) return false;
    *all_day = !datetime;
    if (*all_day) {
        if (length != 10) return false;
        struct tm local = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_isdst = -1};
        time_t timestamp = mktime(&local);
        if (timestamp == (time_t)-1 || local.tm_year != y - 1900 ||
            local.tm_mon != m - 1 || local.tm_mday != d) return false;
        *out = timestamp;
        return true;
    }
    if (length < 20 || (s[10] != 'T' && s[10] != 't') ||
        !digits(s + 11, 2, &h) || s[13] != ':' || !digits(s + 14, 2, &min) ||
        s[16] != ':' || !digits(s + 17, 2, &sec) || h > 23 || min > 59 || sec > 60) return false;
    size_t pos = 19;
    if (s[pos] == '.') {
        ++pos;
        size_t first = pos;
        while (pos < length && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (pos == first) return false;
    }
    int offset = 0;
    if (pos + 1 == length && (s[pos] == 'Z' || s[pos] == 'z')) ++pos;
    else {
        int oh, om;
        if (pos + 6 != length || (s[pos] != '+' && s[pos] != '-') ||
            !digits(s + pos + 1, 2, &oh) || s[pos + 3] != ':' ||
            !digits(s + pos + 4, 2, &om) || oh > 23 || om > 59) return false;
        offset = (oh * 60 + om) * 60 * (s[pos] == '+' ? 1 : -1);
        pos += 6;
    }
    if (pos != length) return false;
    /* Civil date to UTC without changing process-global TZ in the cloud worker. */
    int adjusted_year = y - (m <= 2);
    int era = adjusted_year / 400;
    unsigned year_of_era = (unsigned)(adjusted_year - era * 400);
    unsigned day_of_year = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    int64_t timestamp = ((int64_t)era * 146097 + day_of_era - 719468) * 86400 +
                        h * 3600 + min * 60 + sec - offset;
    if ((int64_t)(time_t)timestamp != timestamp) return false;
    *out = (time_t)timestamp;
    return true;
}

static bool text_field(const cJSON *item, const char *key, char *out, size_t capacity)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, key);
    if (!value) { out[0] = 0; return true; }
    if (!cJSON_IsString(value)) return false;
    const unsigned char *s = (const unsigned char *)value->valuestring;
    size_t used = 0;
    bool truncated = false;
    while (*s) {
        uint32_t code;
        size_t bytes;
        if (*s < 0x80) { code = *s; bytes = 1; }
        else if (*s >= 0xc2 && *s <= 0xdf) { code = *s & 0x1f; bytes = 2; }
        else if (*s >= 0xe0 && *s <= 0xef) { code = *s & 0x0f; bytes = 3; }
        else if (*s >= 0xf0 && *s <= 0xf4) { code = *s & 7; bytes = 4; }
        else return false;
        for (size_t i = 1; i < bytes; ++i) {
            if ((s[i] & 0xc0) != 0x80) return false;
            code = (code << 6) | (s[i] & 0x3f);
        }
        if ((bytes == 2 && code < 0x80) || (bytes == 3 && code < 0x800) ||
            (bytes == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
        bool control = code < 0x20 || (code >= 0x7f && code <= 0x9f) ||
                       (code >= 0x202a && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069);
        size_t copy = control ? 1 : bytes;
        if (used + copy >= capacity) truncated = true;
        if (!truncated) {
            if (control) out[used] = ' ';
            else memcpy(out + used, s, bytes);
            used += copy;
        }
        s += bytes;
    }
    out[used] = 0;
    return true;
}

/* Pages accumulate into one unpublished heap snapshot; callers publish only on success. */
static bool parse_page(const char *json, size_t length, time_t now, sh_calendar_data_t *result,
                       char *next_page, size_t next_capacity)
{
    if (!json || !result || !length || length > SH_CLOUD_BODY_MAX ||
        now < 946684800 || (int64_t)now > 253401695999LL || !bounded_json(json, length)) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return false;
    while (end < json + length && isspace((unsigned char)*end)) ++end;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "nextPageToken");
    bool valid = cJSON_IsObject(root) && end == json + length &&
                 ((cJSON_IsArray(items) && cJSON_GetArraySize(items) <= SH_CALENDAR_EVENTS) ||
                  (!items && cJSON_IsString(kind) && !strcmp(kind->valuestring, "calendar#events"))) &&
                 (!next || (cJSON_IsString(next) && strlen(next->valuestring) < 512));
    if (next_page) {
        if (next && valid) {
            size_t token_length = strlen(next->valuestring);
            if (token_length >= next_capacity) valid = false;
            else memcpy(next_page, next->valuestring, token_length + 1);
        } else next_page[0] = 0;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (!valid) break;
        const cJSON *status = cJSON_GetObjectItemCaseSensitive(item, "status");
        if (!cJSON_IsObject(item) || !cJSON_IsString(status)) { valid = false; break; }
        if (!strcmp(status->valuestring, "cancelled")) continue;
        if (strcmp(status->valuestring, "confirmed") && strcmp(status->valuestring, "tentative")) {
            valid = false; break;
        }
        sh_calendar_event_t event = {0};
        bool end_all_day;
        valid = event_time(cJSON_GetObjectItemCaseSensitive(item, "start"), &event.start, &event.all_day) &&
                event_time(cJSON_GetObjectItemCaseSensitive(item, "end"), &event.end, &end_all_day) &&
                end_all_day == event.all_day && event.end > event.start &&
                text_field(item, "summary", event.title, sizeof(event.title)) &&
                text_field(item, "location", event.location, sizeof(event.location)) &&
                text_field(item, "description", event.description, sizeof(event.description));
        if (!valid) break;
        if (event.end <= now || event.start >= now + 7 * 86400) continue;
        if (!event.title[0]) strcpy(event.title, "(Untitled)");
        unsigned index;
        if (result->count < SH_CALENDAR_EVENTS) index = result->count++;
        else {
            index = SH_CALENDAR_EVENTS - 1;
            if (event.start >= result->events[index].start) continue;
        }
        while (index && result->events[index - 1].start > event.start) {
            result->events[index] = result->events[index - 1];
            --index;
        }
        result->events[index] = event;
    }
    cJSON_Delete(root);
    return valid;
}

bool sh_calendar_parse(const char *json, size_t length, time_t now, sh_calendar_data_t *out)
{
    if (!out) return false;
    sh_calendar_data_t *result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!result) return false;
    result->state = SH_CLOUD_READY;
    result->valid = true;
    result->updated_at = now;
    bool valid = parse_page(json, length, now, result, NULL, 0);
    if (valid) *out = *result;
    free(result);
    return valid;
}

esp_err_t sh_calendar_fetch(const sh_account_credentials_t *credentials,
                            uint32_t network_generation, sh_calendar_data_t *out)
{
    if (!credentials || !out || !credentials->access_token) return ESP_ERR_INVALID_ARG;
    size_t token_length = strnlen(credentials->access_token, SH_AUTH_TOKEN_MAX + 1);
    if (!token_length || token_length > SH_AUTH_TOKEN_MAX) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < token_length; ++i)
        if ((unsigned char)credentials->access_token[i] <= 0x20 ||
            (unsigned char)credentials->access_token[i] >= 0x7f) return ESP_ERR_INVALID_ARG;
    time_t now = time(NULL);
    if (now < 946684800 || (int64_t)now > 253401695999LL) return ESP_ERR_INVALID_STATE;
    time_t until = now + 7 * 86400;
    char minimum[32], maximum[32], base_url[512];
    struct tm utc;
    if (!gmtime_r(&now, &utc) || !strftime(minimum, sizeof(minimum), "%Y-%m-%dT%H%%3A%M%%3A%SZ", &utc) ||
        !gmtime_r(&until, &utc) || !strftime(maximum, sizeof(maximum), "%Y-%m-%dT%H%%3A%M%%3A%SZ", &utc))
        return ESP_ERR_INVALID_STATE;
    int size = snprintf(base_url, sizeof(base_url), "https://www.googleapis.com/calendar/v3/calendars/primary/events"
                        "?timeMin=%s&timeMax=%s&singleEvents=true&orderBy=startTime&maxResults=12"
                        "&showDeleted=false&timeZone=Asia%%2FTokyo"
                        "&fields=kind,nextPageToken,items(summary,start,end,location,description,status)", minimum, maximum);
    if (size < 0 || (size_t)size >= sizeof(base_url)) return ESP_ERR_INVALID_SIZE;
    char *authorization = malloc(token_length + 8);
    char *response = calloc(1, SH_CLOUD_BODY_MAX + 1);
    char *url = malloc(sizeof(base_url) + 3 * 512 + 16);
    sh_calendar_data_t *result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!authorization || !response || !url || !result) {
        free(authorization); free(response); free(url); free(result); return ESP_ERR_NO_MEM;
    }
    snprintf(authorization, token_length + 8, "Bearer %s", credentials->access_token);
    const sh_http_header_t headers[] = {{"Authorization", authorization}, {"Accept", "application/json"}};
    char next_page[512] = {0};
    result->state = SH_CLOUD_READY;
    result->valid = true;
    result->updated_at = now;
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    /* ponytail: cap sparse Google pages at 8; increase only if real calendars require more. */
    for (unsigned page_number = 0; page_number < 8; ++page_number) {
        strcpy(url, base_url);
        if (next_page[0]) {
            strcat(url, "&pageToken=");
            char *write = url + strlen(url);
            for (const unsigned char *p = (const unsigned char *)next_page; *p; ++p) {
                snprintf(write, 4, "%%%02X", *p);
                write += 3;
            }
            *write = 0;
        }
        int status = 0;
        response[0] = 0;
        err = sh_http_request("GET", url, headers, 2, NULL, response,
                              SH_CLOUD_BODY_MAX + 1, &status, network_generation);
        if (err != ESP_OK) break;
        if (status == 401 || status == 403) { err = SH_CLOUD_ERR_AUTH; break; }
        if (status == 429) { err = SH_CLOUD_ERR_RATE_LIMIT; break; }
        if (status != 200 || !parse_page(response, strlen(response), now, result, next_page, sizeof(next_page))) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if (result->count == SH_CALENDAR_EVENTS || !next_page[0]) {
            *out = *result;
            err = ESP_OK;
            break;
        }
        err = ESP_ERR_INVALID_SIZE;
    }
    memset(authorization, 0, token_length + 8);
    free(authorization);
    free(response);
    free(url);
    free(result);
    return err;
}
