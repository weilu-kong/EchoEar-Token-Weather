#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

/* UI operations require the BSP display lock, or the existing LVGL task. */
void sh_ui_init(void);
void sh_ui_navigate(int page);
/* Explicit legacy demo override; default pages read cloud snapshots. */
void sh_ui_set_quota(int provider, int remaining, int total, bool has_total);
lv_draw_buf_t *sh_ui_snapshot(int *captured_page);
