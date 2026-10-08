#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sh_network.h"
#include "sh_ui.h"
#include "sh_cloud.h"
#include "sh_auth.h"
#include <stdlib.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_rom_crc.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const char *TAG = "SHANHAI";
static SemaphoreHandle_t serial_tx_lock;
static vprintf_like_t previous_logger;
static int serialized_log(const char *format,va_list args) {
    xSemaphoreTake(serial_tx_lock,portMAX_DELAY);
    int result=previous_logger(format,args);
    xSemaphoreGive(serial_tx_lock);
    return result;
}
static void console_task(void *arg) {
    (void)arg;
    const size_t capacity=SH_AUTH_JSON_MAX+32;
    char *line=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!line){vTaskDelete(NULL);return;}
    for (;;) {
        if (!fgets(line,capacity,stdin)) {clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(100));continue;}
        if(!strchr(line,'\n')){int ch;while((ch=fgetc(stdin))!='\n'&&ch!=EOF){}puts("SH_ACCOUNT TOO_LARGE");continue;}
        if(!strncmp(line,"account ",8)){
            esp_err_t err=sh_cloud_import_account(line+8,strlen(line+8));
            memset(line,0,capacity);printf("SH_ACCOUNT %s\n",err==ESP_OK?"OK":"ERROR");fflush(stdout);continue;
        }
        int account_id;
        if(sscanf(line,"account_forget %d",&account_id)==1){printf("SH_ACCOUNT %s\n",sh_cloud_forget_account(account_id)==ESP_OK?"OK":"ERROR");continue;}
        if(!strcmp(line,"cloud_refresh\n")){sh_cloud_refresh();puts("SH_CLOUD REFRESH");continue;}
        int page,p,remaining,total,known;
        if (sscanf(line,"page %d",&page)==1) {
            if(bsp_display_lock(5000)){sh_ui_navigate(page);bsp_display_unlock();printf("SH_PAGE %d\n",page);}
        } else if(sscanf(line,"quota %d %d %d %d",&p,&remaining,&total,&known)==4) {
            if(bsp_display_lock(5000)){sh_ui_set_quota(p,remaining,total,known!=0);bsp_display_unlock();printf("SH_QUOTA %d %d %d %d\n",p,remaining,total,known);}
        } else if(!strncmp(line,"snapshot",8)) {
            lv_draw_buf_t *shot=NULL;int captured_page=-1;
            if(bsp_display_lock(5000)){shot=sh_ui_snapshot(&captured_page);bsp_display_unlock();}
            if(shot){
                uint32_t crc=esp_rom_crc32_le(0,shot->data,shot->data_size);
                xSemaphoreTake(serial_tx_lock,portMAX_DELAY);
                printf("\nSH_FRAME %lu %lu %lu %lu %d %08lx\n",(unsigned long)shot->data_size,(unsigned long)shot->header.w,(unsigned long)shot->header.h,(unsigned long)shot->header.stride,captured_page,(unsigned long)crc);fflush(stdout);
                size_t sent=0;
                while(sent<shot->data_size){
                    size_t chunk=shot->data_size-sent;if(chunk>1024)chunk=1024;
                    int written=usb_serial_jtag_write_bytes(shot->data+sent,chunk,pdMS_TO_TICKS(1000));
                    if(written<=0)break;
                    sent+=written;
                }
                printf("\nSH_END %lu\n",(unsigned long)sent);fflush(stdout);
                xSemaphoreGive(serial_tx_lock);
                lv_draw_buf_destroy(shot);
            }else puts("SH_FRAME_ERROR");
        } else {
            const struct {const char *name;sh_net_action_t action;} commands[]={
                {"connect",SH_NET_CONNECT},{"disconnect",SH_NET_DISCONNECT},{"forget",SH_NET_FORGET},
                {"pair",SH_NET_PAIR},{"cancel",SH_NET_CANCEL_PAIR},{"refresh",SH_NET_REFRESH}
            };
            bool handled=false;
            for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++){
                size_t n=strlen(commands[i].name);
                if(!strncmp(line,commands[i].name,n)&&(line[n]=='\n'||line[n]=='\r'||line[n]==0)){
                    printf("SH_COMMAND %s %s\n",commands[i].name,esp_err_to_name(sh_network_request(commands[i].action)));handled=true;break;
                }
            }
            if(!handled){sh_net_snapshot_t state;sh_network_get_snapshot(&state);
                printf("SH_STATUS saved=%d connected=%d pairing=%d error=%d clock=%d weather=%d stale=%d internal=%u largest=%u psram=%u console_stack=%u\n",state.wifi_saved,state.wifi_connected,state.pairing,state.provision_error,state.time_synced,state.weather.valid,state.weather.stale,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
        }
    }
}
void app_main(void) {
    serial_tx_lock=xSemaphoreCreateMutex();
    if(!serial_tx_lock){ESP_LOGE(TAG,"Serial lock allocation failed");return;}
    previous_logger=esp_log_set_vprintf(serialized_log);
    ESP_LOGI(TAG, "SHANHAI: Tokyo weather, independent account and Calendar HTTPS clients");
    ESP_ERROR_CHECK(sh_network_init());
    ESP_ERROR_CHECK(sh_cloud_init());
    if (!bsp_display_start()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    if (!bsp_display_lock(5000)) {
        ESP_LOGE(TAG, "Display lock unavailable");
        return;
    }
    sh_ui_init();
    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    usb_serial_jtag_driver_config_t console={.tx_buffer_size=2048,.rx_buffer_size=2048};
    esp_err_t console_err=usb_serial_jtag_driver_install(&console);
    if(console_err==ESP_OK){usb_serial_jtag_vfs_use_driver();usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);setvbuf(stdin,NULL,_IONBF,0);
        if(xTaskCreate(console_task,"sh_console",8192,NULL,2,NULL)!=pdPASS)ESP_LOGW(TAG,"Diagnostic console unavailable");
    }else ESP_LOGW(TAG,"USB console: %s",esp_err_to_name(console_err));
    ESP_LOGI(TAG, "Display ready. Internal heap=%u largest=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
