/* Synthetic provider fixtures; quota HTTP is stubbed so no live request is possible. */
#include <assert.h>
#include <string.h>
#include "sh_ai.h"
#include "sh_http.h"

/* Host fixture builds link this stub; no provider request is possible. */
esp_err_t sh_http_request(const char *method, const char *url, const sh_http_header_t *headers,
                         size_t count, const char *body, char *response, size_t capacity,
                         int *status, uint32_t generation)
{
    (void)method; (void)url; (void)headers; (void)count; (void)body;
    (void)response; (void)capacity; (void)status; (void)generation;
    return SH_CLOUD_ERR_UNSUPPORTED;
}

static sh_service_data_t parse(sh_account_id_t id, const char *json)
{
    sh_service_data_t data = {0};
    assert(sh_ai_parse(id, json, strlen(json), &data));
    assert(data.valid && data.state == SH_CLOUD_READY);
    return data;
}

int main(void)
{
    sh_service_data_t data = parse(SH_CODEX,
        "{\"rate_limit\":{\"primary_window\":{\"used_percent\":25,\"limit_window_seconds\":18000,\"reset_at\":1791417600},"
        "\"secondary_window\":{\"used_percent\":100,\"limit_window_seconds\":604800,\"reset_at\":1792022400}},"
        "\"additional_rate_limits\":[{\"rate_limit\":{\"primary_window\":{\"used_percent\":99}}}],"
        "\"credits\":{\"unlimited\":false,\"balance\":\"9.99\"}}");
    assert(data.remaining_percent == 75 && data.window_minutes == 300);
    assert(data.has_secondary && data.secondary_percent == 0 && data.secondary_minutes == 10080);
    assert(data.has_balance && !data.has_total && strcmp(data.unit, "credits") == 0);
    data = parse(SH_CLAUDE,
        "{\"five_hour\":{\"utilization\":30,\"resets_at\":\"2026-10-08T12:00:00+09:00\"},"
        "\"seven_day\":{\"utilization\":110,\"resets_at\":\"2026-10-15T03:00:00.000Z\"}}");
    assert(data.remaining_percent == 70 && data.secondary_percent == 0);
    assert(data.reset_at == 1791428400 && !data.has_balance);
    data = parse(SH_CURSOR,
        "{\"billingCycleEnd\":\"1792022400000\",\"planUsage\":{\"limit\":2000,\"includedSpend\":500,\"bonusSpend\":999}}");
    assert(data.balance == 15 && data.total == 20 && data.remaining_percent == 75);
    assert(data.reset_at == 1792022400);
    assert(strcmp(data.unit, "USD") == 0);
    data = parse(SH_ANTIGRAVITY,
        "{\"models\":{\"model-a\":{\"quotaInfo\":{\"remainingFraction\":0.9,\"resetTime\":\"2026-10-08T03:00:00Z\"}},"
        "\"model-b\":{\"quotaInfo\":{\"remainingFraction\":0.25,\"resetTime\":\"2026-10-09T03:00:00Z\"}}}}");
    assert(data.remaining_percent == 25 && !data.has_total && !data.has_balance && data.window_minutes == 0);
    sh_service_data_t saved = data;
    const char *bad[] = {"{}", "{\"error\":{}}", "{\"rate_limit\":{\"primary_window\":{\"used_percent\":-1}}}",
                         "{\"credits\":{\"balance\":\"nan\"}}", "{\"credits\":{\"balance\":\"9\\u0000x\"}}", "{} trailing"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!sh_ai_parse(SH_CODEX, bad[i], strlen(bad[i]), &data));
        assert(memcmp(&saved, &data, sizeof(data)) == 0);
    }
    assert(!sh_ai_parse(SH_ANTIGRAVITY, "{\"models\":{}}", 13, &data));
    return 0;
}
