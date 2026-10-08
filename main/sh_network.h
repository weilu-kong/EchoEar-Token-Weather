#pragma once
#include "esp_err.h"
#include "sh_model.h"

typedef enum {
    SH_NET_CONNECT, SH_NET_DISCONNECT, SH_NET_FORGET,
    SH_NET_PAIR, SH_NET_CANCEL_PAIR, SH_NET_REFRESH
} sh_net_action_t;

typedef struct {
    sh_weather_t weather;
    bool wifi_saved, wifi_connected, pairing, provision_error, time_synced;
    uint32_t revision,generation;
    char ssid[33];
    char qr_payload[192];
} sh_net_snapshot_t;

esp_err_t sh_network_init(void);
esp_err_t sh_network_request(sh_net_action_t action);
void sh_network_get_snapshot(sh_net_snapshot_t *out);
