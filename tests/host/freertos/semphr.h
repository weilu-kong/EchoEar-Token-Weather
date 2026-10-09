#pragma once
#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock);
