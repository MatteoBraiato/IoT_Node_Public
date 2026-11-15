#ifndef HOST_EVENTS_H
#define HOST_EVENTS_H
#include "FreeRTOS.h"
typedef uint32_t EventBits_t;
typedef struct { pthread_mutex_t mutex; pthread_cond_t changed; EventBits_t bits; } StaticEventGroup_t;
typedef StaticEventGroup_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *storage);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear,
        BaseType_t all, TickType_t ticks);
#endif
