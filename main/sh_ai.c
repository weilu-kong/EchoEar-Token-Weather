#include "sh_ai.h"
#include "sh_http.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static const cJSON *field(const cJSON *item, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(item, key);
}

static bool number(const cJSON *item, double low, double high, double *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < low || item->valuedouble > high) return false;
    *out = item->valuedouble;
    return true;
}

static bool amount(const cJSON *item, double *out)
{
    if (cJSON_IsNumber(item)) return number(item, 0, 1e15, out);
    if (!cJSON_IsString(item) || !item->valuestring[0]) return false;
    const char *s = item->valuestring;
    for (size_t i = 0; s[i]; ++i)
        if (!isdigit((unsigned char)s[i]) && s[i] != '.') return false;
    char *end;
    double value = strtod(s, &end);
    if (*end || !isfinite(value) || value < 0 || value > 1e15) return false;
    *out = value;
    return true;
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

static bool timestamp(const cJSON *item, time_t *out)
{
    double value;
    if (number(item, 0, 253402300799.0, &value)) {
        if (floor(value) != value || (double)(time_t)value != value) return false;
        *out = (time_t)value;
        return true;
    }
    if (!cJSON_IsString(item)) return false;
    const char *s = item->valuestring;
    size_t len = strlen(s);
    /* Protobuf JSON encodes int64 timestamps as decimal strings. */
    if (len > 0 && len <= 12) {
        bool decimal = true;
        for (size_t i = 0; i < len; ++i) if (!isdigit((unsigned char)s[i])) decimal = false;
        if (decimal) {
            value = strtod(s, NULL);
            if (value > 253402300799.0 || (double)(time_t)value != value) return false;
            *out = (time_t)value;
            return true;
        }
    }
    int y, m, d, h, min, sec;
    if (len < 20 || !digits(s, 4, &y) || s[4] != '-' ||
        !digits(s + 5, 2, &m) || s[7] != '-' || !digits(s + 8, 2, &d) ||
        s[10] != 'T' || !digits(s + 11, 2, &h) || s[13] != ':' ||
        !digits(s + 14, 2, &min) || s[16] != ':' || !digits(s + 17, 2, &sec) ||
        y < 1970 || m < 1 || m > 12 || h > 23 || min > 59 || sec > 59) return false;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int md = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (d < 1 || d > md) return false;
    size_t pos = 19;
    if (s[pos] == '.') {
        size_t first = ++pos;
        while (pos < len && isdigit((unsigned char)s[pos])) ++pos;
        if (pos == first) return false;
    }
    int offset = 0;
    if (pos + 1 == len && s[pos] == 'Z') ++pos;
    else {
        int oh, om;
        if (pos + 6 != len || (s[pos] != '+' && s[pos] != '-') ||
            !digits(s + pos + 1, 2, &oh) || s[pos + 3] != ':' ||
            !digits(s + pos + 4, 2, &om) || oh > 23 || om > 59) return false;
        offset = (oh * 60 + om) * 60 * (s[pos] == '+' ? 1 : -1);
        pos += 6;
    }
    if (pos != len) return false;
    int adjusted = y - (m <= 2), era = adjusted / 400;
    unsigned year = (unsigned)(adjusted - era * 400);
    unsigned day = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    int64_t epoch = ((int64_t)era * 146097 + year * 365 + year / 4 - year / 100 + day - 719468) * 86400 + h * 3600 + min * 60 + sec - offset;
    if (epoch < 0 || (int64_t)(time_t)epoch != epoch) return false;
    *out = (time_t)epoch;
    return true;
}

static bool window(const cJSON *item, const char *usage_key, const char *reset_key,
                   int minutes, float *remaining, time_t *reset)
{
    double used;
    if (!cJSON_IsObject(item) || !number(field(item, usage_key), 0, 1e6, &used) ||
        !timestamp(field(item, reset_key), reset) || minutes <= 0) return false;
    *remaining = (float)fmax(0, 100 - used);
    return true;
}

static bool codex_window(const cJSON *item, float *remaining, int *minutes, time_t *reset)
{
    double seconds;
    if (!number(field(item, "limit_window_seconds"), 60, (double)INT_MAX * 60, &seconds) ||
        floor(seconds) != seconds) return false;
    *minutes = (int)(seconds / 60);
    return window(item, "used_percent", "reset_at", *minutes, remaining, reset);
}

static bool codex(const cJSON *root, sh_service_data_t *data)
{
    const cJSON *limits = field(root, "rate_limit"), *primary = field(limits, "primary_window");
    const cJSON *secondary = field(limits, "secondary_window"), *credits = field(root, "credits");
    if (primary && !cJSON_IsNull(primary)) {
        if (!codex_window(primary, &data->remaining_percent, &data->window_minutes, &data->reset_at)) return false;
        data->has_percent = true;
    }
    if (secondary && !cJSON_IsNull(secondary)) {
        if (!codex_window(secondary, &data->secondary_percent, &data->secondary_minutes, &data->secondary_reset_at)) return false;
        data->has_secondary = true;
    }
    /* ponytail: other metered buckets are not the Codex bucket; add named buckets when the UI can represent them. */
    if (credits && !cJSON_IsNull(credits)) {
        if (!cJSON_IsObject(credits)) return false;
        const cJSON *unlimited = field(credits, "unlimited"), *balance = field(credits, "balance");
        if (unlimited && !cJSON_IsBool(unlimited)) return false;
        data->unlimited = cJSON_IsTrue(unlimited);
        if (balance && !cJSON_IsNull(balance)) {
            if (!amount(balance, &data->balance)) return false;
            data->has_balance = true;
            strcpy(data->unit, "credits");
        }
    }
    return data->has_percent || data->has_secondary || data->has_balance || data->unlimited;
}

static bool claude(const cJSON *root, sh_service_data_t *data)
{
    const cJSON *primary = field(root, "five_hour"), *secondary = field(root, "seven_day");
    if (primary && !cJSON_IsNull(primary)) {
        if (!window(primary, "utilization", "resets_at", 300, &data->remaining_percent, &data->reset_at)) return false;
        data->has_percent = true;
        data->window_minutes = 300;
    }
    if (secondary && !cJSON_IsNull(secondary)) {
        if (!window(secondary, "utilization", "resets_at", 10080, &data->secondary_percent, &data->secondary_reset_at)) return false;
        data->has_secondary = true;
        data->secondary_minutes = 10080;
    }
    /* Extra usage is spend, not subscription credits; this view only represents subscription windows. */
    return data->has_percent || data->has_secondary;
}

static bool cursor_reset(const cJSON *item, time_t *out)
{
    double milliseconds;
    if (cJSON_IsString(item)) {
        const char *s = item->valuestring;
        size_t length = strlen(s);
        if (!length || length > 15) return false;
        for (size_t i = 0; i < length; ++i) if (!isdigit((unsigned char)s[i])) return false;
        milliseconds = strtod(s, NULL);
    } else if (!number(item, 0, 253402300799999.0, &milliseconds)) return false;
    if (!isfinite(milliseconds) || milliseconds < 0 || milliseconds > 253402300799999.0 ||
        floor(milliseconds) != milliseconds) return false;
    /* Cursor passes this int64 directly to JavaScript Date: milliseconds, not seconds. */
    double seconds = floor(milliseconds / 1000);
    if ((double)(time_t)seconds != seconds) return false;
    *out = (time_t)seconds;
    return true;
}

static bool cursor(const cJSON *root, sh_service_data_t *data)
{
    const cJSON *plan = field(root, "planUsage");
    double limit, used;
    if (!cJSON_IsObject(plan) || !number(field(plan, "limit"), 0, INT_MAX, &limit) ||
        !number(field(plan, "includedSpend"), 0, INT_MAX, &used) ||
        floor(limit) != limit || floor(used) != used || !cursor_reset(field(root, "billingCycleEnd"), &data->reset_at)) return false;
    data->balance = fmax(0, limit - used) / 100;
    data->has_balance = true;
    strcpy(data->unit, "USD");
    if (limit > 0) {
        data->total = limit / 100;
        data->has_total = data->has_percent = true;
        data->remaining_percent = (float)fmax(0, 100 * (limit - used) / limit);
    } else {
        /* A zero included-spend limit does not prove an unlimited plan. */
        double percent;
        if (number(field(plan, "totalPercentUsed"), 0, 1e6, &percent)) {
            data->remaining_percent = (float)fmax(0, 100 - percent);
            data->has_percent = true;
        }
    }
    return true;
}

static bool antigravity(const cJSON *root, sh_service_data_t *data)
{
    const cJSON *models = field(root, "models");
    if (!cJSON_IsObject(models)) return false;
    /* ponytail: the service ring shows the least remaining model quota; expand to named model rows when needed. */
    for (const cJSON *model = models->child; model; model = model->next) {
        if (cJSON_IsTrue(field(model, "disabled"))) continue;
        const cJSON *quota = field(model, "quotaInfo");
        if (!quota) continue;
        double fraction;
        time_t reset;
        if (!number(field(quota, "remainingFraction"), 0, 1, &fraction) ||
            !timestamp(field(quota, "resetTime"), &reset)) return false;
        if (!data->has_percent || fraction * 100 < data->remaining_percent) {
            data->remaining_percent = (float)(fraction * 100);
            data->reset_at = reset;
        }
        data->has_percent = true;
    }
    if (data->has_percent) strcpy(data->window_label, "モデル最小残量");
    return data->has_percent;
}

bool sh_ai_parse(sh_account_id_t id, const char *json, size_t length, sh_service_data_t *out)
{
    if (!out || !json || !length || length > SH_CLOUD_BODY_MAX) return false;
    unsigned depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)json[i];
        if (!c) return false;
        if (quoted) {
            if (c < 0x20) return false;
            if (c == '\\') {
                if (++i >= length) return false;
                if (json[i] == 'u' && i + 4 < length && !memcmp(json + i + 1, "0000", 4)) return false;
            }
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    if (quoted || depth) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return false;
    while (end < json + length && isspace((unsigned char)*end)) ++end;
    sh_service_data_t data = {0};
    bool valid = false;
    if (cJSON_IsObject(root) && end == json + length && !field(root, "error")) {
        switch (id) {
        case SH_CODEX: valid = codex(root, &data); break;
        case SH_CLAUDE: valid = claude(root, &data); break;
        case SH_CURSOR: valid = cursor(root, &data); break;
        case SH_ANTIGRAVITY: valid = antigravity(root, &data); break;
        default: break;
        }
    }
    cJSON_Delete(root);
    if (valid) {
        data.valid = true;
        data.state = SH_CLOUD_READY;
        data.updated_at = time(NULL);
        *out = data;
    }
    return valid;
}

esp_err_t sh_ai_fetch(sh_account_id_t id, const sh_account_credentials_t *credentials,
                      uint32_t network_generation, sh_service_data_t *out)
{
    if (!credentials || !out || !credentials->access_token || !credentials->access_token[0]) return ESP_ERR_INVALID_ARG;
    size_t token_length = strlen(credentials->access_token);
    if (token_length >= SH_AUTH_TOKEN_MAX) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < token_length; ++i)
        if ((unsigned char)credentials->access_token[i] < 0x21 || (unsigned char)credentials->access_token[i] > 0x7e) return ESP_ERR_INVALID_ARG;
    const char *url, *method = "GET", *body = NULL;
    char *request_body = NULL;
    /* ponytail: these read-only client interfaces are private; recheck installed client contracts after upstream changes. */
    switch (id) {
    case SH_CODEX: url = "https://chatgpt.com/backend-api/wham/usage"; break;
    case SH_CLAUDE: url = "https://api.anthropic.com/api/oauth/usage"; break;
    case SH_CURSOR:
        url = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage";
        method = "POST"; body = "{}"; break;
    case SH_ANTIGRAVITY: {
        if (!credentials->project || !credentials->project[0]) return SH_CLOUD_ERR_UNSUPPORTED;
        cJSON *request = cJSON_CreateObject();
        if (!request) return ESP_ERR_NO_MEM;
        if (!cJSON_AddStringToObject(request, "project", credentials->project)) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
        request_body = cJSON_PrintUnformatted(request);
        cJSON_Delete(request);
        if (!request_body) return ESP_ERR_NO_MEM;
        url = "https://daily-cloudcode-pa.googleapis.com/v1internal:fetchAvailableModels";
        method = "POST"; body = request_body; break;
    }
    default: return SH_CLOUD_ERR_UNSUPPORTED;
    }
    char *authorization = malloc(token_length + 8), *response = malloc(SH_CLOUD_BODY_MAX + 1);
    if (!authorization || !response) { free(authorization); free(response); cJSON_free(request_body); return ESP_ERR_NO_MEM; }
    snprintf(authorization, token_length + 8, "Bearer %s", credentials->access_token);
    sh_http_header_t headers[6] = {{"Authorization", authorization}, {"Accept", "application/json"}, {"Content-Type", "application/json"}};
    size_t count = 3;
    if (id == SH_CODEX) {
        headers[count++] = (sh_http_header_t){"User-Agent", "codex-cli"};
        if (credentials->account_id && credentials->account_id[0]) headers[count++] = (sh_http_header_t){"ChatGPT-Account-Id", credentials->account_id};
    }
    if (id == SH_CLAUDE) headers[count++] = (sh_http_header_t){"anthropic-beta", "oauth-2025-04-20"};
    if (id == SH_CURSOR) {
        headers[count++] = (sh_http_header_t){"Connect-Protocol-Version", "1"};
        headers[count++] = (sh_http_header_t){"x-cursor-client-type", "ide"};
        if (credentials->team_id && credentials->team_id[0])
            headers[count++] = (sh_http_header_t){"x-cursor-team-id", credentials->team_id};
    }
    int status = 0;
    esp_err_t error = sh_http_request(method, url, headers, count, body, response, SH_CLOUD_BODY_MAX + 1, &status, network_generation);
    if (error == ESP_OK) {
        if (status == 401 || status == 403) error = SH_CLOUD_ERR_AUTH;
        else if (status == 429) error = SH_CLOUD_ERR_RATE_LIMIT;
        else if (status != 200 || !sh_ai_parse(id, response, strlen(response), out)) error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && id == SH_CURSOR && credentials->team_id && credentials->team_id[0])
        strcpy(out->window_label, "TEAM");
    memset(authorization, 0, token_length + 8);
    free(authorization); free(response); cJSON_free(request_body);
    return error;
}
