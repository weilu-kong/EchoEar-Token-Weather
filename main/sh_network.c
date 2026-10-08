#include "sh_network.h"
#include "sh_weather_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "sh_network";
static const char WEATHER_URL[] =
    "https://api.open-meteo.com/v1/forecast?latitude=35.6762&longitude=139.6503"
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&timezone=Asia%2FTokyo&forecast_days=1&timeformat=unixtime";

enum { EV_IP = BIT0, EV_LOST = BIT1, EV_PROV_OK = BIT2,
       EV_PROV_FAIL = BIT3, EV_TIME = BIT4, EV_STOP = BIT5 };
static SemaphoreHandle_t snapshot_lock;
static QueueHandle_t actions;
static EventGroupHandle_t events;
static TaskHandle_t weather_task_handle;
static esp_netif_t *station;
static sh_net_snapshot_t snapshot;
static uint32_t generation;
static uint32_t pairing_session;
static wifi_config_t saved_config;
static char pop[33];
static bool initialized, manager_present, pair_active, reconnect_allowed, wifi_running;
static unsigned attempts;
static int64_t pair_deadline, retry_at;

static void offline(void)
{
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.wifi_connected = false;
    snapshot.weather.stale = true;
    ++generation;
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
}

static void error_state(esp_err_t err)
{
    ESP_LOGW(TAG, "network operation failed: %s", esp_err_to_name(err));
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.provision_error = true;
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
}

static void publish_saved(void)
{
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.wifi_saved = saved_config.sta.ssid[0] != 0;
    memcpy(snapshot.ssid, saved_config.sta.ssid, sizeof(saved_config.sta.ssid));
    snapshot.ssid[sizeof(snapshot.ssid) - 1] = 0;
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        offline();
        xEventGroupSetBits(events, EV_LOST);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, EV_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        xEventGroupSetBits(events, EV_STOP);
    }
}

static void provisioning_event(void *arg, network_prov_cb_event_t event, void *data)
{
    (void)data;
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    bool current = pairing_session == (uint32_t)(uintptr_t)arg;
    xSemaphoreGive(snapshot_lock);
    if (!current) return;
    if (event == NETWORK_PROV_WIFI_CRED_SUCCESS) xEventGroupSetBits(events, EV_PROV_OK);
    if (event == NETWORK_PROV_WIFI_CRED_FAIL) xEventGroupSetBits(events, EV_PROV_FAIL);
}

static esp_err_t stop_station(void)
{
    if (!wifi_running) return ESP_OK;
    xEventGroupClearBits(events, EV_STOP);
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) return err;
    wifi_running = false;
    // Drain earlier Wi-Fi callbacks before accepting another connection.
    EventBits_t bits = xEventGroupWaitBits(events, EV_STOP, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    xEventGroupClearBits(events, EV_IP | EV_LOST);
    return bits & EV_STOP ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void time_synced(struct timeval *tv)
{
    (void)tv;
    xEventGroupSetBits(events, EV_TIME);
}

static esp_err_t load_pop(void)
{
    if (pop[0]) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("shanhai", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    size_t length = sizeof(pop);
    err = nvs_get_str(handle, "pop", pop, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < sizeof(random); ++i)
            snprintf(pop + i * 2, 3, "%02x", random[i]);
        err = nvs_set_str(handle, "pop", pop);
        if (err == ESP_OK) err = nvs_commit(handle);
    } else if (err == ESP_OK) {
        if (length != sizeof(pop)) err = ESP_ERR_INVALID_STATE;
        for (size_t i = 0; err == ESP_OK && i < sizeof(pop) - 1; ++i)
            if (!((pop[i] >= '0' && pop[i] <= '9') ||
                  (pop[i] >= 'a' && pop[i] <= 'f'))) err = ESP_ERR_INVALID_STATE;
    }
    nvs_close(handle);
    if (err != ESP_OK) memset(pop, 0, sizeof(pop));
    return err;
}

static esp_err_t restore_saved(void)
{
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &saved_config);
    return err;
}

static esp_err_t stop_pair(bool success)
{
    if (!manager_present) return ESP_OK;
    pair_active = false;
    pair_deadline = 0;
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    ++pairing_session;
    xSemaphoreGive(snapshot_lock);
    esp_err_t err = network_prov_mgr_deinit();
    manager_present = false;
    if (!success) {
        esp_err_t restore_err = stop_station();
        if (restore_err == ESP_OK) restore_err = restore_saved();
        if (err == ESP_OK) err = restore_err;
    }
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.pairing = false;
    memset(snapshot.qr_payload, 0, sizeof(snapshot.qr_payload));
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
    xEventGroupClearBits(events, EV_PROV_OK | EV_PROV_FAIL);
    return err;
}

static esp_err_t start_pair(void)
{
    reconnect_allowed = false;
    retry_at = 0;
    offline();
    esp_err_t err = stop_pair(false);
    if (err == ESP_OK) err = stop_station();
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    wifi_running = true;
    err = load_pop();
    if (err != ESP_OK) return err;
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    uint32_t session = ++pairing_session;
    xSemaphoreGive(snapshot_lock);
    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        // Keep BLE memory reusable so pairing can be started again this boot.
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = {.event_cb = provisioning_event, .user_data = (void *)(uintptr_t)session},
        .network_prov_wifi_conn_cfg = {.wifi_conn_attempts = 3},
    };
    err = network_prov_mgr_init(config);
    if (err != ESP_OK) return err;
    manager_present = true;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) goto fail;
    char service_name[18];
    snprintf(service_name, sizeof(service_name), "PROV_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, pop, service_name, NULL);
    if (err != ESP_OK) goto fail;
    pair_active = true;
    pair_deadline = esp_timer_get_time() + 120000000LL;
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snprintf(snapshot.qr_payload, sizeof(snapshot.qr_payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
             service_name, pop);
    snapshot.pairing = true;
    snapshot.provision_error = false;
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
    ESP_LOGI(TAG, "BLE pairing started (120s timeout)");
    return ESP_OK;
fail:
    stop_pair(false);
    return err;
}

static esp_err_t connect_saved(void)
{
    reconnect_allowed = false;
    retry_at = 0;
    offline();
    esp_err_t err = stop_pair(false);
    if (err != ESP_OK) return err;
    if (!saved_config.sta.ssid[0]) return start_pair();
    err = stop_station();
    if (err == ESP_OK) err = restore_saved();
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    wifi_running = true;
    attempts = 0;
    reconnect_allowed = true;
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    snapshot.provision_error = false;
    ++snapshot.revision;
    xSemaphoreGive(snapshot_lock);
    xEventGroupClearBits(events, EV_IP | EV_LOST);
    err = esp_wifi_connect();
    if (err != ESP_OK) reconnect_allowed = false;
    ESP_LOGI(TAG, "Wi-Fi connection requested");
    return err;
}

static esp_err_t handle_action(sh_net_action_t action)
{
    if (action == SH_NET_CONNECT) return connect_saved();
    if (action == SH_NET_PAIR) return start_pair();
    if (action == SH_NET_REFRESH) {
        xTaskNotifyGive(weather_task_handle);
        return ESP_OK;
    }
    reconnect_allowed = false;
    retry_at = 0;
    offline();
    esp_err_t err = stop_pair(false);
    if (err == ESP_OK) err = stop_station();
    if (err != ESP_OK) return err;
    if (action == SH_NET_FORGET) {
        wifi_config_t empty = {0};
        err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &empty);
        if (err != ESP_OK) return err;
        saved_config = empty;
        publish_saved();
        return start_pair();
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected; automatic reconnect disabled");
    return ESP_OK;
}

static bool request_current(uint32_t request_generation)
{
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    bool current = snapshot.wifi_connected && generation == request_generation;
    xSemaphoreGive(snapshot_lock);
    return current;
}

typedef struct {
    char *body;
    size_t length;
    uint32_t generation;
    int64_t deadline;
    esp_err_t error;
} weather_response_t;

static esp_err_t collect_weather(esp_http_client_event_t *event)
{
    weather_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_CONNECTED &&
        event->event_id != HTTP_EVENT_ON_HEADER &&
        event->event_id != HTTP_EVENT_ON_HEADERS_COMPLETE &&
        event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (response->error == ESP_OK && esp_timer_get_time() >= response->deadline)
        response->error = ESP_ERR_TIMEOUT;
    if (response->error == ESP_OK && !request_current(response->generation))
        response->error = ESP_ERR_INVALID_STATE;
    if (response->error == ESP_OK && event->event_id == HTTP_EVENT_ON_HEADERS_COMPLETE) {
        int64_t length = esp_http_client_get_content_length(event->client);
        if (esp_http_client_get_status_code(event->client) != 200 || length > SH_WEATHER_RESPONSE_MAX)
            response->error = ESP_ERR_INVALID_RESPONSE;
    }
    if (response->error == ESP_OK && event->event_id == HTTP_EVENT_ON_DATA) {
        if (event->data_len < 0 || (size_t)event->data_len > SH_WEATHER_RESPONSE_MAX - response->length)
            response->error = ESP_ERR_INVALID_SIZE;
        else {
            memcpy(response->body + response->length, event->data, event->data_len);
            response->length += event->data_len;
        }
    }
    if (response->error != ESP_OK) {
        // SDK ignores event-handler errors. Shutdown interrupts its internal read loop;
        // the same weather worker remains the sole owner that cleans up the client.
        int socket = esp_http_client_get_socket(event->client);
        if (socket >= 0) shutdown(socket, SHUT_RDWR);
    }
    return response->error;
}

static esp_err_t fetch_weather(char *body, size_t *length, uint32_t request_generation)
{
    weather_response_t response = {.body = body, .generation = request_generation,
        .deadline = esp_timer_get_time() + 10000000LL, .error = ESP_OK};
    esp_http_client_config_t config = {
        .url = WEATHER_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .disable_auto_redirect = true,
        .buffer_size = 1024,
        .is_async = true,
        .event_handler = collect_weather,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err;
    for (;;) {
        int remaining = (int)((response.deadline - esp_timer_get_time()) / 1000);
        if (remaining <= 0) { err = ESP_ERR_TIMEOUT; break; }
        if (!request_current(request_generation)) { err = ESP_ERR_INVALID_STATE; break; }
        esp_http_client_set_timeout_ms(client, remaining);
        err = esp_http_client_perform(client);
        if (response.error != ESP_OK) { err = response.error; break; }
        if (err != ESP_ERR_HTTP_EAGAIN) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    *length = response.length;
    int status = esp_http_client_get_status_code(client);
    body[*length] = 0;
    if (err == ESP_OK && (!*length || status != 200)) err = ESP_ERR_INVALID_RESPONSE;
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "weather HTTP status=%d bytes=%u result=%s internal_heap=%u", status,
             (unsigned)*length, esp_err_to_name(err),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return err;
}

static void weather_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15 * 60 * 1000));
        xSemaphoreTake(snapshot_lock, portMAX_DELAY);
        bool connected = snapshot.wifi_connected;
        uint32_t request_generation = generation;
        sh_weather_t weather = snapshot.weather;
        xSemaphoreGive(snapshot_lock);
        if (!connected) continue;
        char *body = malloc(SH_WEATHER_RESPONSE_MAX + 1);
        size_t length = 0;
        esp_err_t err = body ? fetch_weather(body, &length, request_generation) : ESP_ERR_NO_MEM;
        bool valid = err == ESP_OK && sh_weather_parse(body, length, &weather);
        free(body);
        if (!valid) weather.stale = true;
        xSemaphoreTake(snapshot_lock, portMAX_DELAY);
        if (snapshot.wifi_connected && generation == request_generation) {
            snapshot.weather = weather;
            ++snapshot.revision;
        }
        xSemaphoreGive(snapshot_lock);
        ESP_LOGI(TAG, "weather reading %s", valid ? "updated" : "retained as stale");
    }
}

static void connection_worker(void *arg)
{
    (void)arg;
    esp_err_t err = saved_config.sta.ssid[0] ? connect_saved() : start_pair();
    if (err != ESP_OK) error_state(err);
    for (;;) {
        sh_net_action_t action;
        if (xQueueReceive(actions, &action, pdMS_TO_TICKS(100)) == pdTRUE) {
            err = handle_action(action);
            if (err != ESP_OK) error_state(err);
        }
        EventBits_t bits = xEventGroupClearBits(events, EV_IP | EV_LOST | EV_PROV_OK | EV_PROV_FAIL | EV_TIME);
        if ((bits & EV_PROV_OK) && pair_active) {
            err = esp_wifi_get_config(WIFI_IF_STA, &saved_config);
            if (err == ESP_OK) {
                publish_saved();
                err = stop_pair(true);
                reconnect_allowed = true;
                attempts = 0;
            }
            if (err != ESP_OK) error_state(err);
        } else if ((bits & EV_PROV_FAIL) && pair_active) {
            reconnect_allowed = false;
            offline();
            err = stop_pair(false);
            error_state(err == ESP_OK ? ESP_FAIL : err);
        }
        bool ip_accepted = false;
        if ((bits & EV_IP) && (pair_active || reconnect_allowed)) {
            wifi_ap_record_t ap;
            esp_netif_ip_info_t ip;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                esp_netif_get_ip_info(station, &ip) == ESP_OK && ip.ip.addr != 0) {
                attempts = 0;
                retry_at = 0;
                ip_accepted = true;
                xSemaphoreTake(snapshot_lock, portMAX_DELAY);
                snapshot.wifi_connected = true;
                snapshot.provision_error = false;
                ++snapshot.revision;
                xSemaphoreGive(snapshot_lock);
                err = esp_netif_sntp_start();
                if (err != ESP_OK) ESP_LOGW(TAG, "SNTP start: %s", esp_err_to_name(err));
                xTaskNotifyGive(weather_task_handle);
                ESP_LOGI(TAG, "Wi-Fi connected");
            }
        }
        if ((bits & EV_LOST) && !ip_accepted && reconnect_allowed && !pair_active) {
            if (++attempts < 3) retry_at = esp_timer_get_time() + 1000000LL;
            else { reconnect_allowed = false; error_state(ESP_FAIL); }
        }
        if (bits & EV_TIME) {
            xSemaphoreTake(snapshot_lock, portMAX_DELAY);
            snapshot.time_synced = true;
            ++snapshot.revision;
            xSemaphoreGive(snapshot_lock);
            xTaskNotifyGive(weather_task_handle);
            ESP_LOGI(TAG, "SNTP synchronized (JST)");
        }
        int64_t now = esp_timer_get_time();
        if (pair_active && now >= pair_deadline) {
            offline();
            err = stop_pair(false);
            error_state(err == ESP_OK ? ESP_ERR_TIMEOUT : err);
        }
        if (retry_at && now >= retry_at) {
            retry_at = 0;
            err = esp_wifi_connect();
            if (err != ESP_OK) { reconnect_allowed = false; error_state(err); }
        }
    }
}

esp_err_t sh_network_init(void)
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    // Preserve user data: damaged/full NVS is reported, never erased here.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    snapshot_lock = xSemaphoreCreateMutex();
    actions = xQueueCreate(8, sizeof(sh_net_action_t));
    events = xEventGroupCreate();
    if (!snapshot_lock || !actions || !events) { err = ESP_ERR_NO_MEM; goto fail; }
    bool wifi_ready = false, sntp_ready = false;
    esp_event_handler_instance_t wifi_handler = NULL, ip_handler = NULL;
    err = esp_netif_init();
    if (err != ESP_OK) goto fail;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    station = esp_netif_create_default_wifi_sta();
    if (!station) { err = ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) goto cleanup;
    wifi_ready = true;
    // The component's DEBUG credential logging exposes password fragments.
    esp_log_level_set("network_prov_mgr", ESP_LOG_INFO);
    esp_log_level_set("security1", ESP_LOG_WARN);
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_get_config(WIFI_IF_STA, &saved_config);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto cleanup;
    publish_saved();
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, &wifi_handler);
    if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, &ip_handler);
    if (err != ESP_OK) goto cleanup;
    if (setenv("TZ", "JST-9", 1) != 0) { err = ESP_ERR_NO_MEM; goto cleanup; }
    tzset();
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    sntp.sync_cb = time_synced;
    err = esp_netif_sntp_init(&sntp);
    if (err != ESP_OK) goto cleanup;
    sntp_ready = true;
    err = esp_wifi_start();
    if (err != ESP_OK) goto cleanup;
    wifi_running = true;
    if (xTaskCreate(weather_worker, "sh_weather", 8192, NULL, 3, &weather_task_handle) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (xTaskCreate(connection_worker, "sh_connection", 6144, NULL, 4, NULL) != pdPASS) {
        vTaskDelete(weather_task_handle);
        weather_task_handle = NULL;
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    initialized = true;
    ESP_LOGI(TAG, "network ready; internal_heap=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return ESP_OK;
cleanup:
    if (wifi_ready) { esp_wifi_stop(); esp_wifi_deinit(); wifi_running = false; }
    if (sntp_ready) esp_netif_sntp_deinit();
    if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    if (station) { esp_netif_destroy_default_wifi(station); station = NULL; }
fail:
    if (events) { vEventGroupDelete(events); events = NULL; }
    if (actions) { vQueueDelete(actions); actions = NULL; }
    if (snapshot_lock) { vSemaphoreDelete(snapshot_lock); snapshot_lock = NULL; }
    return err;
}

esp_err_t sh_network_request(sh_net_action_t action)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (action < SH_NET_CONNECT || action > SH_NET_REFRESH) return ESP_ERR_INVALID_ARG;
    return xQueueSend(actions, &action, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void sh_network_get_snapshot(sh_net_snapshot_t *out)
{
    if (!out) return;
    if (!snapshot_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(snapshot_lock, portMAX_DELAY);
    *out = snapshot;
    out->generation = generation;
    xSemaphoreGive(snapshot_lock);
}
