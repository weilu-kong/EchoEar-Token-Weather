#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, TaskHandle_t *handle);
unsigned ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t task);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
