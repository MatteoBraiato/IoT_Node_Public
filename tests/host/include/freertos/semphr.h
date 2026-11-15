#ifndef HOST_SEM_H
#define HOST_SEM_H
#include "FreeRTOS.h"
typedef pthread_mutex_t StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
#endif
