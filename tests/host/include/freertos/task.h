#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void xTaskDelayUntil(TickType_t *last, TickType_t period);
void vTaskDelete(TaskHandle_t task);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, TaskHandle_t *handle, int core);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, TaskHandle_t *handle);
TaskHandle_t xTaskCreateStaticPinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, StackType_t *memory, StaticTask_t *task, int core);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
