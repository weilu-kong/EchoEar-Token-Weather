#include "sh_model.h"

int sh_quota_percent(const sh_quota_t *q) {
    if (!q || !q->has_total || q->total <= 0) return -1;
    if (q->remaining <= 0) return 0;
    int64_t pct = ((int64_t)q->remaining * 100 + q->total / 2) / q->total;
    return pct > 100 ? 100 : (int)pct;
}

const char *sh_weather_condition(int code) {
    switch (code) {
        case 0: return "晴れ";
        case 1: case 2: return "一部曇り";
        case 3: return "曇り";
        case 45: case 48: return "霧";
        case 51: case 53: case 55: case 56: case 57: return "霧雨";
        case 61: case 63: case 65: case 66: case 67: return "雨";
        case 71: case 73: case 75: case 77: case 85: case 86: return "雪";
        case 80: case 81: case 82: return "にわか雨";
        case 95: case 96: case 97: case 99: return "雷雨";
        default: return "不明";
    }
}

const char *sh_weather_icon(int code, bool is_day, bool valid) {
    if (!valid) return "unknown";
    if (code == 0) return is_day ? "sun" : "moon";
    switch (code) {
        case 1: case 2: case 3: return "cloud";
        case 45: case 48: return "fog";
        case 71: case 73: case 75: case 77: case 85: case 86: return "snow";
        case 51: case 53: case 55: case 56: case 57:
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82: case 95: case 96: case 97: case 99: return "rain";
        default: return "unknown";
    }
}
