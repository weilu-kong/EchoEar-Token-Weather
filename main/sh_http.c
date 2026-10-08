#include "sh_http.h"
#include "sh_network.h"
#include "sh_auth.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

typedef struct {
    char *body;
    size_t length,capacity;
    uint32_t generation;
    int64_t deadline;
    esp_err_t error;
} response_t;
static bool current(uint32_t generation){
    sh_net_snapshot_t net;sh_network_get_snapshot(&net);
    return net.wifi_connected&&net.generation==generation;
}
static esp_err_t collect(esp_http_client_event_t *event){
    response_t *r=event->user_data;
    if(event->event_id!=HTTP_EVENT_ON_CONNECTED&&event->event_id!=HTTP_EVENT_ON_HEADER&&event->event_id!=HTTP_EVENT_ON_HEADERS_COMPLETE&&event->event_id!=HTTP_EVENT_ON_DATA)return ESP_OK;
    if(r->error==ESP_OK&&esp_timer_get_time()>=r->deadline)r->error=ESP_ERR_TIMEOUT;
    if(r->error==ESP_OK&&!current(r->generation))r->error=ESP_ERR_INVALID_STATE;
    if(r->error==ESP_OK&&event->event_id==HTTP_EVENT_ON_HEADERS_COMPLETE&&esp_http_client_get_content_length(event->client)>(int64_t)(r->capacity-1))r->error=ESP_ERR_INVALID_SIZE;
    if(r->error==ESP_OK&&event->event_id==HTTP_EVENT_ON_DATA){
        if(event->data_len<0||(size_t)event->data_len>r->capacity-1-r->length)r->error=ESP_ERR_INVALID_SIZE;
        else{memcpy(r->body+r->length,event->data,event->data_len);r->length+=event->data_len;}
    }
    if(r->error!=ESP_OK){int socket=esp_http_client_get_socket(event->client);if(socket>=0)shutdown(socket,SHUT_RDWR);}
    return r->error;
}
esp_err_t sh_http_request(const char *method,const char *url,const sh_http_header_t *headers,size_t header_count,const char *body,char *response,size_t capacity,int *status,uint32_t generation){
    if(!method||!url||strncmp(url,"https://",8)||!response||capacity<2||capacity>SH_CLOUD_BODY_MAX+1||!status||(strcmp(method,"GET")&&strcmp(method,"POST")))return ESP_ERR_INVALID_ARG;
    *status=0;response[0]=0;
    response_t r={.body=response,.capacity=capacity,.generation=generation,.deadline=esp_timer_get_time()+20000000LL};
    esp_http_client_config_t cfg={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=5000,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=SH_AUTH_TOKEN_MAX+512,.is_async=true,.event_handler=collect,.user_data=&r};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return ESP_ERR_NO_MEM;
    esp_err_t err=esp_http_client_set_method(client,!strcmp(method,"POST")?HTTP_METHOD_POST:HTTP_METHOD_GET);
    for(size_t i=0;err==ESP_OK&&i<header_count;i++){
        if(!headers||!headers[i].name||!headers[i].value||strpbrk(headers[i].name,"\r\n")||strpbrk(headers[i].value,"\r\n")){err=ESP_ERR_INVALID_ARG;break;}
        err=esp_http_client_set_header(client,headers[i].name,headers[i].value);
    }
    if(err==ESP_OK&&body){size_t len=strlen(body);if(len>12288)err=ESP_ERR_INVALID_SIZE;else err=esp_http_client_set_post_field(client,body,(int)len);}
    if(err==ESP_OK)for(;;){
        int remaining=(int)((r.deadline-esp_timer_get_time())/1000);
        if(remaining<=0){err=ESP_ERR_TIMEOUT;break;}
        if(!current(generation)){err=ESP_ERR_INVALID_STATE;break;}
        esp_http_client_set_timeout_ms(client,remaining>5000?5000:remaining);
        err=esp_http_client_perform(client);
        if(r.error!=ESP_OK){err=r.error;break;}
        if(err!=ESP_ERR_HTTP_EAGAIN)break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    *status=esp_http_client_get_status_code(client);response[r.length]=0;
    esp_http_client_cleanup(client);
    const char *label=strstr(url,"chatgpt.com")?"codex":strstr(url,"api2.cursor.sh")?"cursor":strstr(url,"calendar/v3")?"calendar":"oauth";
    ESP_LOGI("sh_http","request=%s status=%d bytes=%u result=%s",label,*status,(unsigned)r.length,esp_err_to_name(err));
    return err;
}
