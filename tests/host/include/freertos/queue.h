#ifndef HOST_QUEUE_H
#define HOST_QUEUE_H
#include "FreeRTOS.h"
typedef struct { pthread_mutex_t mutex; uint8_t *data; size_t size, capacity, head, count; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(size_t count, size_t size, uint8_t *data, StaticQueue_t *queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
#endif
