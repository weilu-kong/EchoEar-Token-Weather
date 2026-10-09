#include "sh_ui.h"
#include "sh_assets.h"
#include "sh_scene_data.h"
#include "sh_network.h"
#include "sh_cloud.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG="sh_ui";
static const char *names[]={"Codex","Antigravity","Claude Code","Cursor"};
static const char *logos[]={"codex","antigravity","claude","cursor"};
static const uint32_t colors[]={0x58e6d1,0xd3a868,0xf1d088,0xea8066};
static const lv_image_dsc_t *backgrounds[]={&sh_bg_home,&sh_bg_weather,&sh_bg_quota,&sh_bg_detail,&sh_bg_standby,&sh_bg_standby,&sh_bg_standby};
static sh_quota_t quotas[4];
static bool demo_quota[4];
static sh_cloud_snapshot_t cloud, next_cloud;
static const int page_order[]={0,1,2,3,5,6,4};
static int selected_event, calendar_offset;
static sh_net_snapshot_t net;
static lv_obj_t *root;
static int page=0, selected=2, overlay=0;
static uint32_t last_action, last_log;
enum {O_NONE,O_NETWORK,O_CONFIRM,O_PAIR,O_ERROR};
typedef struct {lv_obj_t *obj,*outline[8];const sh_node_t *node;uint32_t *pixels;int side,last_pct;char last[512];} binding_t;
static binding_t bindings[36];
static int binding_count;
static void show(void);
static void refresh(void);
static void activity(void);
static int calendar_count(void){return cloud.calendar.valid?(cloud.calendar.count<SH_CALENDAR_EVENTS?cloud.calendar.count:SH_CALENDAR_EVENTS):0;}
static void calendar_clamp(void){
    int count=calendar_count(),last=count>0?((count-1)/2)*2:0;
    if(calendar_offset>last)calendar_offset=last;
    if(selected_event>=count)selected_event=calendar_offset;
}
static void calendar_move(int direction){
    int count=calendar_count(),last=count>0?((count-1)/2)*2:0;
    calendar_offset+=direction*2;
    if(calendar_offset<0)calendar_offset=0;
    if(calendar_offset>last)calendar_offset=last;
    selected_event=calendar_offset;activity();show();
}

static void activity(void){last_action=lv_tick_get();}
static void amount(int32_t n,char *out,size_t cap){
    if(n>=1000000)snprintf(out,cap,"%.1fM",n/1000000.0);
    else if(n>=10000)snprintf(out,cap,"%.1fk",n/1000.0);
    else if(n>=1000)snprintf(out,cap,"%ld,%03ld",(long)(n/1000),(long)(n%1000));
    else snprintf(out,cap,"%ld",(long)n);
}
static const char *cloud_state(sh_cloud_state_t state){
    switch(state){
        case SH_CLOUD_READY:return "接続済み";
        case SH_CLOUD_LOADING:return "取得中";
        case SH_CLOUD_STALE:return "前回データ";
        case SH_CLOUD_AUTH_REQUIRED:return "認証が必要";
        case SH_CLOUD_RATE_LIMITED:return "取得制限中";
        case SH_CLOUD_ERROR:return "取得失敗・未対応";
        default:return "未接続";
    }
}
static int quota_percent(int p){
    if(demo_quota[p])return sh_quota_percent(&quotas[p]);
    const sh_service_data_t *q=&cloud.services[p];
    return q->valid&&!q->unlimited&&q->has_percent?(int)lroundf(fmaxf(0,fminf(100,q->remaining_percent))):-1;
}
static void stamp(time_t when,char *out,size_t cap){
    if(when<=0){snprintf(out,cap,"未提供");return;}
    struct tm t;localtime_r(&when,&t);snprintf(out,cap,"%d/%d %02d:%02d",t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min);
}
static void window(int minutes,char *out,size_t cap){
    if(minutes<=0)snprintf(out,cap,"利用窓未提供");
    else if(minutes%1440==0)snprintf(out,cap,"%d日",minutes/1440);
    else if(minutes%60==0)snprintf(out,cap,"%d時間",minutes/60);
    else snprintf(out,cap,"%d分",minutes);
}
static void balance(int p,char *out,size_t cap){
    if(demo_quota[p]){
        char a[24],b[24];amount(quotas[p].remaining,a,sizeof(a));amount(quotas[p].total,b,sizeof(b));
        if(quotas[p].has_total)snprintf(out,cap,"%s / %s",a,b);else snprintf(out,cap,"%s credits",a);return;
    }
    const sh_service_data_t *q=&cloud.services[p];
    if(!q->valid){snprintf(out,cap,"%s",cloud_state(q->state));return;}
    if(q->unlimited)snprintf(out,cap,"無制限");
    else if(q->has_percent)snprintf(out,cap,"残り %d%%",quota_percent(p));
    else if(p==SH_CURSOR&&!q->has_balance)snprintf(out,cap,"未提供");
    else if(q->has_balance&&q->has_total)snprintf(out,cap,"%.2f / %.2f %s",q->balance,q->total,q->unit);
    else if(q->has_balance)snprintf(out,cap,"%.2f %s",q->balance,q->unit);
    else snprintf(out,cap,"残量未提供");
}
static void event_time(const sh_calendar_event_t *event,char *out,size_t cap){
    if(event->start<=0){snprintf(out,cap,"日時未提供");return;}
    struct tm a,b;localtime_r(&event->start,&a);localtime_r(&event->end,&b);
    if(event->all_day){
        if(event->end-event->start>86400){time_t final=event->end-1;localtime_r(&final,&b);snprintf(out,cap,"%d/%d–%d/%d 終日",a.tm_mon+1,a.tm_mday,b.tm_mon+1,b.tm_mday);}
        else snprintf(out,cap,"%d/%d 終日",a.tm_mon+1,a.tm_mday);
    }
    else if(event->end>event->start&&a.tm_year==b.tm_year&&a.tm_yday==b.tm_yday)snprintf(out,cap,"%d/%d %02d:%02d–%02d:%02d",a.tm_mon+1,a.tm_mday,a.tm_hour,a.tm_min,b.tm_hour,b.tm_min);
    else if(event->end>event->start)snprintf(out,cap,"%d/%d %02d:%02d – %d/%d %02d:%02d",a.tm_mon+1,a.tm_mday,a.tm_hour,a.tm_min,b.tm_mon+1,b.tm_mday,b.tm_hour,b.tm_min);
    else snprintf(out,cap,"%d/%d %02d:%02d",a.tm_mon+1,a.tm_mday,a.tm_hour,a.tm_min);
}
static void value(const sh_node_t *n,char *out,size_t cap){
    const char *key=n->key;int p=n->provider<0?selected:n->provider;
    time_t now=time(NULL);struct tm tm={0};bool clock_ok=net.time_synced&&now>1600000000;
    if(clock_ok)localtime_r(&now,&tm);
#define TEXT(...) do{snprintf(out,cap,__VA_ARGS__);return;}while(0)
    if(!*key)TEXT("%s",n->text);
    if(!strcmp(key,"clock")){if(!clock_ok)TEXT("--:--");TEXT("%02d:%02d",tm.tm_hour,tm.tm_min);}
    if(!strcmp(key,"date")){if(!clock_ok)TEXT("時刻未同期");const char *days[]={"日","月","火","水","木","金","土"};TEXT("%d年%d月%d日(%s)",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,days[tm.tm_wday]);}
    if(!strcmp(key,"date_short")){if(!clock_ok)TEXT("時刻未同期");const char *days[]={"日","月","火","水","木","金","土"};TEXT("%d月%d日(%s)",tm.tm_mon+1,tm.tm_mday,days[tm.tm_wday]);}
    if(!strcmp(key,"service"))TEXT("%s",names[p]);
    if(!strcmp(key,"balance")){balance(p,out,cap);return;}
    const sh_service_data_t *q=&cloud.services[p];
    if(!strcmp(key,"quota_status"))TEXT("%s",demo_quota[0]||demo_quota[1]||demo_quota[2]||demo_quota[3]?"デモ含む":"実データ");
    if(!strcmp(key,"service_state"))TEXT("%s",demo_quota[p]?"デモ値":cloud_state(q->state));
    if(!strcmp(key,"reset")||!strcmp(key,"updated")||!strcmp(key,"secondary_reset")){
        if(demo_quota[p]&&!strcmp(key,"secondary_reset"))TEXT("%s","");
        if(demo_quota[p])TEXT("%s",!strcmp(key,"updated")?"更新 デモ値":"リセット 未提供");
        if(!strcmp(key,"secondary_reset")&&p==SH_CURSOR&&q->valid&&!q->has_balance&&!q->has_secondary)TEXT("Other Models 未提供");
        if(!strcmp(key,"secondary_reset")&&!q->has_secondary)TEXT("%s","");
        char date[32];stamp(!strcmp(key,"updated")?q->updated_at:!strcmp(key,"secondary_reset")?q->secondary_reset_at:q->reset_at,date,sizeof(date));
        if(!strcmp(key,"secondary_reset")&&p==SH_CURSOR&&!q->has_balance)TEXT("Other Models 残り %.0f%%",fmaxf(0,fminf(100,q->secondary_percent)));
        if(!strcmp(key,"secondary_reset")){char period[32];window(q->secondary_minutes,period,sizeof(period));TEXT("%s 残り %.0f%% %s",period,q->secondary_percent,date);}
        TEXT("%s %s",!strcmp(key,"updated")?"更新":!strcmp(key,"secondary_reset")?"第2リセット":"リセット",date);
    }
    if(!strncmp(key,"event_",6)||!strncmp(key,"calendar_",9)){
        const sh_calendar_data_t *cal=&cloud.calendar;
        if(!strcmp(key,"calendar_state"))TEXT("%s",cal->valid&&cal->state==SH_CLOUD_READY&&cal->count==0?"予定なし":cloud_state(cal->state));
        if(!strcmp(key,"calendar_page")){int count=calendar_count();if(!count)TEXT("%s","");TEXT("%d–%d / %d ↑↓",calendar_offset+1,calendar_offset+2<count?calendar_offset+2:count,count);}
        if(!strcmp(key,"calendar_updated")){char date[32];stamp(cal->updated_at,date,sizeof(date));TEXT("更新 %s",date);}
        const char *colon=strchr(key,':');int index=colon?calendar_offset+atoi(colon+1):selected_event;
        if(!cal->valid||index<0||index>=cal->count||index>=SH_CALENDAR_EVENTS)TEXT("%s","");
        const sh_calendar_event_t *event=&cal->events[index];
        if(!strncmp(key,"event_time",10)){event_time(event,out,cap);return;}
        if(!strncmp(key,"event_title",11))TEXT("%s",event->title[0]?event->title:"(無題)");
        if(!strcmp(key,"event_location"))TEXT("%s",event->location[0]?event->location:"場所なし");
        if(!strcmp(key,"event_description"))TEXT("%s",event->description[0]?event->description:"説明なし");
    }
    if(!strcmp(key,"detail_main")){int pct=quota_percent(p);if(pct>=0)TEXT("%d%%",pct);if(demo_quota[p]){amount(quotas[p].remaining,out,cap);return;}if(q->valid&&q->unlimited)TEXT("∞");if(q->valid&&q->has_balance)TEXT("%.2f",q->balance);TEXT("--");}
    if(!strcmp(key,"detail_balance")){if(p==SH_CODEX||p==SH_CLAUDE)TEXT("%s","");if(p==SH_CURSOR&&!demo_quota[p]&&q->valid&&!q->has_balance)TEXT("Cursor Models 残り");if(!demo_quota[p]&&!q->valid)TEXT("%s",cloud_state(q->state));balance(p,out,cap);return;}
    if(!strcmp(key,"detail_unit")){
        if(p==SH_CURSOR)TEXT("%s","");
        if(demo_quota[p])TEXT("%s",quotas[p].has_total?"credits":"上限未提供");
        if(!q->valid)TEXT("%s","");
        if(p==SH_CODEX||p==SH_CLAUDE){window(q->window_minutes,out,cap);return;}
        char first[32];if(q->window_label[0])snprintf(first,sizeof(first),"%s",q->window_label);else window(q->window_minutes,first,sizeof(first));
        TEXT("%s %s",q->unit,first);
    }
    if(!strcmp(key,"condition")||!strcmp(key,"home_condition")){
        if(!net.weather.valid)TEXT("天気を取得中");
        if(!strcmp(key,"home_condition")&&net.weather.stale)TEXT("更新失敗・%s",sh_weather_condition(net.weather.code));
        TEXT("%s",sh_weather_condition(net.weather.code));
    }
    if(!strcmp(key,"weather_updated")){
        if(!net.weather.valid)TEXT("Wi-Fi設定をご確認ください");
        struct tm updated;localtime_r(&net.weather.updated_at,&updated);TEXT("最終更新 %02d:%02d",updated.tm_hour,updated.tm_min);
    }
    if(!net.weather.valid){
        if(!strcmp(key,"temperature"))TEXT("--°C");
        if(!strcmp(key,"feels"))TEXT("--°");
        if(!strcmp(key,"humidity")||!strcmp(key,"rain"))TEXT("--%%");
        TEXT("--° / --°");
    }
    if(!strcmp(key,"temperature"))TEXT("%d°C",(int)lroundf(net.weather.temp));
    if(!strcmp(key,"feels"))TEXT("%d°",(int)lroundf(net.weather.feels));
    if(!strcmp(key,"humidity"))TEXT("%d%%",net.weather.humidity);
    if(!strcmp(key,"rain"))TEXT("%d%%",net.weather.rain);
    if(!strcmp(key,"highlow"))TEXT("↑ %d°   ↓ %d°",(int)lroundf(net.weather.high),(int)lroundf(net.weather.low));
    if(!strcmp(key,"highlow_short"))TEXT("%d° / %d°",(int)lroundf(net.weather.high),(int)lroundf(net.weather.low));
    TEXT("--");
#undef TEXT
}
static void passive(lv_obj_t *obj){
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
}
static lv_obj_t *text(lv_obj_t *parent,const char *s,int x,int y,int width,int size,int weight,uint32_t color){
    lv_obj_t *obj=lv_label_create(parent);if(!obj)return NULL;
    const lv_font_t *f=sh_font(size<25?size+SH_SMALL_TEXT_INCREMENT:size,weight);
    lv_label_set_text(obj,s);lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    lv_obj_set_width(obj,width);lv_obj_set_pos(obj,x-width/2,y-f->line_height/2);
    lv_obj_set_style_text_font(obj,f,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_outline_stroke_width(obj,1,0);
    lv_obj_set_style_text_outline_stroke_color(obj,lv_color_hex(0x001016),0);
    lv_obj_set_style_text_outline_stroke_opa(obj,LV_OPA_COVER,0);passive(obj);return obj;
}
static lv_obj_t *panel(int x,int y,int w,int h,int alpha,uint32_t border){
    lv_obj_t *obj=lv_obj_create(root);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_pad_all(obj,0,0);lv_obj_set_style_radius(obj,11,0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x011118),0);lv_obj_set_style_bg_opa(obj,alpha,0);
    lv_obj_set_style_border_width(obj,1,0);lv_obj_set_style_border_color(obj,lv_color_hex(border),0);
    lv_obj_set_style_border_opa(obj,80,0);passive(obj);return obj;
}
static lv_obj_t *image(const lv_image_dsc_t *src,int x,int y,int w,int h,uint32_t color,bool tint){
    lv_obj_t *obj=lv_image_create(root);lv_image_set_src(obj,src);lv_obj_set_size(obj,w,h);
    lv_image_set_inner_align(obj,LV_IMAGE_ALIGN_STRETCH);lv_obj_set_pos(obj,x-w/2,y-h/2);
    if(tint){lv_obj_set_style_image_recolor(obj,lv_color_hex(color),0);lv_obj_set_style_image_recolor_opa(obj,LV_OPA_COVER,0);}
    passive(obj);return obj;
}
static uint32_t mix(uint32_t a,uint32_t b,float t){
    t=fmaxf(0,fminf(1,t));uint32_t out=0;
    for(int shift=0;shift<=16;shift+=8){float u=(a>>shift)&255,v=(b>>shift)&255;out|=(uint32_t)lroundf(u+(v-u)*t)<<shift;}
    return out;
}
static void update_ring(binding_t *b,int pct){
    if(!b->pixels||pct==b->last_pct)return;
    const sh_node_t *n=b->node;int p=n->provider<0?selected:n->provider;
    uint32_t accent=n->color==0xfffaf0?colors[p]:n->color;
    float center=b->side/2.0f,limit=pct*6.283185307f/100.0f;
    float ex=sinf(limit)*n->radius,ey=-cosf(limit)*n->radius;
    for(int y=0;y<b->side;y++)for(int x=0;x<b->side;x++){
        float dx=x+.5f-center,dy=y+.5f-center,d=hypotf(dx,dy)-n->radius;
        float coverage=n->stroke/2.0f+.5f-fabsf(d);
        if(coverage<=0){b->pixels[y*b->side+x]=0;continue;}
        float angle=atan2f(dx,-dy);if(angle<0)angle+=6.283185307f;
        float radial=(d+n->stroke)/(2*n->stroke);
        uint32_t color=radial<.3f?mix(0x081b24,0x5e746c,radial/.3f):radial<.53f?mix(0x5e746c,0x29434b,(radial-.3f)/.23f):mix(0x29434b,0x071b23,(radial-.53f)/.47f);
        bool active=pct>0&&(angle<=limit||hypotf(dx-ex,dy-ey)<=n->stroke/2.0f||hypotf(dx,dy+n->radius)<=n->stroke/2.0f);
        if(active){float t=angle/6.283185307f;color=t<.32f?mix(0xf2d694,accent,t/.32f):t<.8f?accent:mix(accent,0xeaffd8,(t-.8f)/.2f);}
        uint32_t alpha=(uint32_t)lroundf(fminf(1,coverage)*255);
        b->pixels[y*b->side+x]=(alpha<<24)|color;
    }
    b->last_pct=pct;lv_obj_invalidate(b->obj);
}
static void release_page(void){
    lv_obj_clean(root);
    for(int i=0;i<binding_count;i++)if(bindings[i].pixels)heap_caps_free(bindings[i].pixels);
    memset(bindings,0,sizeof(bindings));binding_count=0;
}
static bool request(sh_net_action_t action){
    esp_err_t err=sh_network_request(action);
    if(err!=ESP_OK){ESP_LOGW(TAG,"UI command: %s",esp_err_to_name(err));overlay=O_ERROR;return false;}
    return true;
}
static void perform(const char *a){
    activity();
    if(!strcmp(a,"close")){if(!net.pairing||request(SH_NET_CANCEL_PAIR))overlay=O_NONE;}
    else if(!strcmp(a,"network"))overlay=O_NETWORK;
    else if(!strcmp(a,"forget"))overlay=O_CONFIRM;
    else if(!strcmp(a,"confirm-forget")){if(request(SH_NET_FORGET))overlay=O_PAIR;}
    else if(!strcmp(a,"cancel-forget"))overlay=O_NETWORK;
    else if(!strcmp(a,"disconnect")){if(request(SH_NET_DISCONNECT))overlay=O_NETWORK;}
    else if(!strcmp(a,"reconnect")){if(request(SH_NET_CONNECT))overlay=O_NETWORK;}
    else if(!strcmp(a,"pairing")){if(request(SH_NET_PAIR))overlay=O_PAIR;}
    else if(!strcmp(a,"calendar")){page=5;overlay=O_NONE;}
    else if(!strncmp(a,"event:",6)){int index=calendar_offset+atoi(a+6);if(cloud.calendar.valid&&index>=0&&index<cloud.calendar.count&&index<SH_CALENDAR_EVENTS){selected_event=index;page=6;}overlay=O_NONE;}
    else if(!strncmp(a,"provider:",9)){selected=atoi(a+9);if(selected<0||selected>3)selected=0;page=3;overlay=O_NONE;}
    else if(!strcmp(a,"next-provider")){selected=(selected+1)%4;page=3;overlay=O_NONE;}
    else{if(!strcmp(a,"home"))page=0;if(!strcmp(a,"weather"))page=1;if(!strcmp(a,"quota"))page=2;overlay=O_NONE;}
    show();
}
static void hit_event(lv_event_t *e){perform(lv_event_get_user_data(e));}
static void hit(int x,int y,int w,int h,const char *action){
    lv_obj_t *obj=panel(x,y,w,h,0,0);lv_obj_set_style_border_width(obj,0,0);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,hit_event,LV_EVENT_CLICKED,(void*)action);
}
static void button(const char *label,int y,const char *action,bool danger){
    panel(83,y-17,194,34,248,danger?0xd7846d:0xc9aa6d);
    text(root,label,180,y,184,13,600,danger?0xf4b29a:0xf1d088);
    hit(83,y-17,194,34,action);
}
static void show_overlay(void){
    lv_obj_t *scrim=panel(20,20,320,320,237,0);lv_obj_set_style_radius(scrim,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(scrim,0,0);
    image(sh_icon("back"),81,73,16,20,0xf1d088,true);hit(58,51,42,44,"close");
    if(overlay==O_NETWORK){
        text(root,"ネットワーク",181,73,220,17,600,0xfffaf0);
        image(sh_icon(net.wifi_connected?"wifi":"wifi_off"),180,113,32,30,net.wifi_connected?0x5ce8ce:0xf1d088,true);
        text(root,net.wifi_connected?"接続済み":"未接続",180,148,210,15,600,net.wifi_connected?0x5ce8ce:0xf1d088);
        text(root,net.wifi_saved?net.ssid:"保存済みネットワークなし",180,173,231,11,500,0xd3d9d4);
        button(net.wifi_connected?"切断":"再接続",213,net.wifi_connected?"disconnect":"reconnect",false);
        button(net.wifi_saved?"ネットワークを忘れる":"BLEで接続設定",258,net.wifi_saved?"forget":"pairing",false);
        text(root,"切断しても設定は保存されます",180,291,232,10,500,0xbfcfc7);
    }else if(overlay==O_CONFIRM){
        text(root,"Wi-Fiを忘れる",180,82,234,18,600,0xfffaf0);
        image(sh_icon("warning"),180,121,30,30,0xf1d088,true);
        text(root,"保存済みの接続情報を削除し、",180,160,237,12,500,0xfffaf0);
        text(root,"BLEで再設定します。",180,183,237,12,500,0xfffaf0);
        button("削除して再設定",234,"confirm-forget",true);button("キャンセル",280,"cancel-forget",false);
    }else if(overlay==O_PAIR){
        text(root,"Wi-Fi設定",180,70,220,19,600,0xfffaf0);
        text(root,"ESP BLE Provisioningでスキャン",180,95,224,10,500,0xfffaf0);
        if(net.qr_payload[0]){
            lv_obj_t *qr=lv_qrcode_create(root);lv_qrcode_set_size(qr,182);lv_qrcode_set_quiet_zone(qr,true);
            lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());
            lv_obj_set_pos(qr,89,111);passive(qr);
            if(lv_qrcode_update(qr,net.qr_payload,strlen(net.qr_payload))!=LV_RESULT_OK)text(root,"設定を確認してください",180,200,235,12,500,0xf1d088);
        }else text(root,"読み込み中",180,200,235,12,500,0xf1d088);
        text(root,"スキャンしてWi-Fiを設定",180,305,245,10,500,0xf1d088);
        text(root,"キャンセル",180,327,118,11,500,0xd3d9d4);hit(121,313,118,28,"close");
    }else{
        text(root,"Wi-Fi設定",180,77,220,17,600,0xfffaf0);image(sh_icon("warning"),180,125,40,36,0xf3aa83,true);
        text(root,"接続できません",180,171,246,18,600,0xfffaf0);
        text(root,"パスワードと2.4GHz Wi-Fiを",180,202,247,11,500,0xfffaf0);
        text(root,"ご確認ください。",180,223,235,11,500,0xfffaf0);
        button("再試行",270,"pairing",false);text(root,"キャンセル",180,313,118,11,500,0xd3d9d4);hit(121,297,118,30,"close");
    }
}
static void show(void){
    calendar_clamp();release_page();
    lv_obj_t *bg=lv_image_create(root);lv_image_set_src(bg,backgrounds[page]);lv_obj_set_pos(bg,SH_BG_OFFSET_X,SH_BG_OFFSET_Y);passive(bg);
    if(overlay){show_overlay();return;}
    for(int i=0;i<sh_page_counts[page];i++){
        const sh_node_t *n=&sh_pages[page][i];binding_t *b=NULL;
        if(n->kind==SH_TEXT||n->kind==SH_ICON||n->kind==SH_RING||n->kind==SH_LOGO){
            if(binding_count>=36)continue;
            b=&bindings[binding_count++];b->node=n;b->last_pct=-2;
        }
        if(n->kind==SH_TEXT){int size=(page==3&&(selected==SH_CODEX||selected==SH_CLAUDE)&&!strcmp(n->key,"detail_unit"))?15:n->size;char s[512];value(n,s,sizeof(s));int y=(page==3&&(selected==SH_CODEX||selected==SH_CLAUDE)&&!strcmp(n->key,"detail_unit"))?193:n->y;
            /* Bitmap fonts ignore vector stroke styles; shifted glyphs form a 1px outline. */
            if(page==0&&(!strcmp(n->key,"date")||!strcmp(n->key,"home_condition")||!strcmp(n->key,"highlow"))){
                int k=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(dx||dy)
                    b->outline[k++]=text(root,s,n->x+dx,y+dy,n->w,size,n->weight,0x001016);
            }
            b->obj=text(root,s,n->x,y,n->w,size,n->weight,n->color);snprintf(b->last,sizeof(b->last),"%s",s);
            if(page==3)lv_obj_set_height(b->obj,sh_font(size<25?size+SH_SMALL_TEXT_INCREMENT:size,n->weight)->line_height);
            if(page>=5&&!strncmp(n->key,"event_",6)){
                const lv_font_t *font=&sh_font_calendar;
                lv_obj_set_style_text_font(b->obj,font,0);
                lv_obj_set_style_text_align(b->obj,LV_TEXT_ALIGN_LEFT,0);
                bool two_lines=!strncmp(n->key,"event_title",11)||!strcmp(n->key,"event_description");
                int height=font->line_height*(two_lines?2:1);
                lv_obj_set_style_text_line_space(b->obj,0,0);
                lv_obj_set_height(b->obj,height);lv_obj_set_y(b->obj,n->y-height/2);
                lv_label_set_long_mode(b->obj,page==5?LV_LABEL_LONG_DOT:LV_LABEL_LONG_WRAP);
                if(page==6&&strcmp(n->key,"event_time")){
                    lv_obj_t *area=panel(n->x-n->w/2,n->y-height/2,n->w,height,0,0);lv_obj_set_style_border_width(area,0,0);
                    lv_obj_add_flag(area,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(area,LV_DIR_VER);lv_obj_set_scrollbar_mode(area,LV_SCROLLBAR_MODE_AUTO);
                    lv_obj_set_parent(b->obj,area);lv_obj_set_pos(b->obj,0,0);lv_obj_set_height(b->obj,LV_SIZE_CONTENT);
                }
            }
        }
        else if(n->kind==SH_PANEL)panel(n->x,n->y,n->w,n->h,n->alpha,0xc9aa6d);
        else if(n->kind==SH_LINE){lv_obj_t *o=panel(n->x,n->y,n->w,1,68,0);lv_obj_set_style_bg_color(o,lv_color_hex(0xcbbd8a),0);lv_obj_set_style_border_width(o,0,0);}
        else if(n->kind==SH_ICON){const char *name=*n->icon?n->icon:sh_weather_icon(net.weather.code,net.weather.is_day,net.weather.valid);bool wifi_off=!strcmp(name,"wifi")&&!net.wifi_connected;if(wifi_off)name="wifi_off";b->obj=image(sh_icon(name),n->x,n->y,n->w,n->h,wifi_off?0xf1d088:n->color,true);snprintf(b->last,sizeof(b->last),"%s",name);}
        else if(n->kind==SH_LOGO){int p=n->provider<0?selected:n->provider;b->obj=image(sh_icon(logos[p]),n->x,n->y,n->w,n->h,0,false);}
        else if(n->kind==SH_RING){
            b->side=2*(int)ceilf(n->radius+n->stroke/2.0f+3);size_t size=b->side*b->side*4;
            b->pixels=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!b->pixels){ESP_LOGE(TAG,"Ring allocation failed: %u bytes",(unsigned)size);continue;}
            b->obj=lv_canvas_create(root);lv_canvas_set_buffer(b->obj,b->pixels,b->side,b->side,LV_COLOR_FORMAT_ARGB8888);
            lv_obj_set_pos(b->obj,n->x-b->side/2,n->y-b->side/2);passive(b->obj);
            update_ring(b,quota_percent(n->provider<0?selected:n->provider));
        }
    }
    if(page==1&&net.weather.valid&&net.weather.stale){panel(99,177,181,16,237,0xd9ae63);text(root,"更新に失敗・前回のデータ",189,185,178,10,500,0xf1d088);}
    for(int i=0;i<sh_hit_counts[page];i++){const sh_hit_t *h=&sh_page_hits[page][i];if(!strncmp(h->action,"event:",6)&&(!cloud.calendar.valid||calendar_offset+atoi(h->action+6)>=calendar_count()))continue;hit(h->x,h->y,h->w,h->h,h->action);}
}
static void refresh(void){
    for(int i=0;i<binding_count;i++){
        binding_t *b=&bindings[i];const sh_node_t *n=b->node;if(!b->obj)continue;
        if(n->kind==SH_TEXT&&*n->key){char s[512];value(n,s,sizeof(s));if(strcmp(s,b->last)){for(int j=0;j<8;j++)if(b->outline[j])lv_label_set_text(b->outline[j],s);lv_label_set_text(b->obj,s);snprintf(b->last,sizeof(b->last),"%s",s);}}
        else if(n->kind==SH_ICON){const char *name=*n->icon?n->icon:sh_weather_icon(net.weather.code,net.weather.is_day,net.weather.valid);bool wifi_off=!strcmp(name,"wifi")&&!net.wifi_connected;if(wifi_off)name="wifi_off";if(strcmp(name,b->last)){lv_image_set_src(b->obj,sh_icon(name));lv_obj_set_style_image_recolor(b->obj,lv_color_hex(wifi_off?0xf1d088:n->color),0);snprintf(b->last,sizeof(b->last),"%s",name);}}
        else if(n->kind==SH_RING)update_ring(b,quota_percent(n->provider<0?selected:n->provider));
    }
}
static void root_event(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_PRESSED||lv_event_get_code(e)==LV_EVENT_SCROLL)activity();
    if(lv_event_get_code(e)==LV_EVENT_CLICKED){activity();if(page==4&&!overlay){page=0;show();}}
    else if(lv_event_get_code(e)==LV_EVENT_GESTURE&&!overlay){
        lv_indev_t *in=lv_indev_active();if(!in)return;lv_dir_t d=lv_indev_get_gesture_dir(in);
        if(page==5&&(d==LV_DIR_TOP||d==LV_DIR_BOTTOM)){lv_indev_wait_release(in);calendar_move(d==LV_DIR_TOP?1:-1);}
        else if(d==LV_DIR_LEFT||d==LV_DIR_RIGHT){lv_indev_wait_release(in);int index=0;while(index<7&&page_order[index]!=page)index++;page=page_order[(index+(d==LV_DIR_LEFT?1:6))%7];activity();show();}
    }
}
static void tick(lv_timer_t *timer){
    (void)timer;sh_net_snapshot_t next;sh_network_get_snapshot(&next);
    bool redraw=false;sh_cloud_get_snapshot(&next_cloud);
    if(next_cloud.revision!=cloud.revision){cloud=next_cloud;redraw=page==5||page==6;}
    if(next.revision!=net.revision){
        if(next.pairing&&!net.pairing){overlay=O_PAIR;activity();redraw=true;}
        else if(next.provision_error&&!net.provision_error){overlay=O_ERROR;activity();redraw=true;}
        else if(!next.pairing&&net.pairing&&overlay==O_PAIR){overlay=O_NETWORK;activity();redraw=true;}
        if(overlay||next.weather.stale!=net.weather.stale)redraw=true;
        net=next;
    }
    if(!overlay&&!net.pairing&&page!=4&&lv_tick_elaps(last_action)>=45000){page=4;redraw=true;}
    if(redraw)show();else refresh();
    if(lv_tick_elaps(last_log)>30000){last_log=lv_tick_get();ESP_LOGI(TAG,"state page=%d overlay=%d wifi=%d weather=%d stale=%d internal=%u largest=%u psram=%u stack=%u",page,overlay,net.wifi_connected,net.weather.valid,net.weather.stale,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)uxTaskGetStackHighWaterMark(NULL));}
}
void sh_ui_init(void){
    root=lv_obj_create(NULL);lv_obj_set_size(root,360,360);lv_obj_set_style_pad_all(root,0,0);lv_obj_set_style_border_width(root,0,0);lv_obj_set_style_bg_color(root,lv_color_black(),0);passive(root);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(root,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(root,root_event,LV_EVENT_ALL,NULL);
    sh_network_get_snapshot(&net);sh_cloud_get_snapshot(&cloud);if(net.pairing)overlay=O_PAIR;
    activity();show();lv_screen_load(root);lv_timer_create(tick,500,NULL);
}
void sh_ui_navigate(int p){if(p<0||p>6)return;page=p;overlay=O_NONE;activity();show();}
void sh_ui_set_quota(int p,int remaining,int total,bool has_total){
    if(p<0||p>3||remaining<0||total<0)return;
    demo_quota[p]=true;quotas[p]=(sh_quota_t){remaining,total,has_total&&total>0};refresh();
}
lv_draw_buf_t *sh_ui_snapshot(int *captured_page){*captured_page=page;return lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);}
