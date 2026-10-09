#pragma once
#include <stdint.h>
typedef unsigned BaseType_t;
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
