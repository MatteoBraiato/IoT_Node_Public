#define _POSIX_C_SOURCE 200809L
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

const char *hostFailTask;
bool hostFailQueue;
TickType_t hostTickOffset;
static pthread_t threads[16];
static unsigned threadCount;
static struct timespec deadline(TickType_t ticks)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)(ticks % 1000U) * 1000000L;
    t.tv_sec += ticks / 1000U + t.tv_nsec / 1000000000L;
    t.tv_nsec %= 1000000000L;
    return t;
}
TickType_t xTaskGetTickCount(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return hostTickOffset + (TickType_t)((uint64_t)t.tv_sec * 1000U + t.tv_nsec / 1000000U);
}
void vTaskDelay(TickType_t ticks)
{
    struct timespec t = { .tv_sec = ticks / 1000U, .tv_nsec = (long)(ticks % 1000U) * 1000000L };
    nanosleep(&t, NULL);
}
void xTaskDelayUntil(TickType_t *last, TickType_t period)
{
    TickType_t elapsed = xTaskGetTickCount() - *last;
    if (elapsed < period) { vTaskDelay(period - elapsed); }
    *last = xTaskGetTickCount();
}
struct launch { void (*fn)(void *); void *arg; };
static void *run(void *arg)
{
    struct launch launch = *(struct launch *)arg;
    free(arg);
    launch.fn(launch.arg);
    return NULL;
}
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, TaskHandle_t *handle, int core)
{
    (void)stack; (void)priority; (void)handle; (void)core;
    BaseType_t result = pdFAIL;
    if ((hostFailTask == NULL) || (strcmp(hostFailTask, name) != 0))
    {
        struct launch *launch = malloc(sizeof(*launch));
        assert(launch != NULL && threadCount < 16U);
        *launch = (struct launch){ fn, arg };
        assert(pthread_create(&threads[threadCount++], NULL, run, launch) == 0);
        result = pdPASS;
    }
    return result;
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, TaskHandle_t *handle)
{
    return xTaskCreatePinnedToCore(fn, name, stack, arg, priority, handle, 0);
}
void vTaskDelete(TaskHandle_t task) { assert(task == NULL); pthread_exit(NULL); }
void hostJoinTasks(void)
{
    for (unsigned i = 0; i < threadCount; ++i) { assert(pthread_join(threads[i], NULL) == 0); }
    threadCount = 0;
}
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *s)
{
    assert(pthread_mutex_init(&s->mutex, NULL) == 0);
    assert(pthread_cond_init(&s->changed, NULL) == 0);
    s->bits = 0;
    return s;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t s, EventBits_t bits)
{
    assert(s); pthread_mutex_lock(&s->mutex);
    s->bits |= bits;
    EventBits_t result = s->bits;
    pthread_cond_broadcast(&s->changed); pthread_mutex_unlock(&s->mutex);
    return result;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t s, EventBits_t bits)
{
    assert(s); pthread_mutex_lock(&s->mutex);
    EventBits_t result = s->bits; s->bits &= ~bits;
    pthread_mutex_unlock(&s->mutex); return result;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t s)
{
    assert(s); pthread_mutex_lock(&s->mutex); EventBits_t result = s->bits;
    pthread_mutex_unlock(&s->mutex); return result;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t s, EventBits_t bits, BaseType_t clear,
        BaseType_t all, TickType_t ticks)
{
    assert(s); struct timespec until = deadline(ticks); pthread_mutex_lock(&s->mutex);
    while (all ? ((s->bits & bits) != bits) : ((s->bits & bits) == 0))
    {
        if ((ticks == 0) || (pthread_cond_timedwait(&s->changed, &s->mutex, &until) == ETIMEDOUT)) { break; }
    }
    EventBits_t result = s->bits;
    if (clear) { s->bits &= ~bits; }
    pthread_mutex_unlock(&s->mutex); return result;
}
QueueHandle_t xQueueCreateStatic(size_t count, size_t size, uint8_t *data, StaticQueue_t *q)
{
    QueueHandle_t result = NULL;
    if (!hostFailQueue)
    {
        assert(q && data); pthread_mutex_init(&q->mutex, NULL);
        q->data = data; q->size = size; q->capacity = count; q->head = q->count = 0;
        result = q;
    }
    return result;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *data, TickType_t wait)
{
    (void)wait; assert(q); pthread_mutex_lock(&q->mutex);
    BaseType_t result = pdFALSE;
    if (q->count < q->capacity)
    {
        memcpy(q->data + ((q->head + q->count) % q->capacity) * q->size, data, q->size);
        ++q->count; result = pdTRUE;
    }
    pthread_mutex_unlock(&q->mutex); return result;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *data, TickType_t wait)
{
    (void)wait; assert(q); pthread_mutex_lock(&q->mutex);
    BaseType_t result = pdFALSE;
    if (q->count > 0)
    {
        memcpy(data, q->data + q->head * q->size, q->size);
        q->head = (q->head + 1) % q->capacity; --q->count; result = pdTRUE;
    }
    pthread_mutex_unlock(&q->mutex); return result;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    assert(q); pthread_mutex_lock(&q->mutex); UBaseType_t count = q->count;
    pthread_mutex_unlock(&q->mutex); return count;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s)
{
    pthread_mutex_init(s, NULL); return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    assert(s); struct timespec until = deadline(ticks);
    return (ticks ? pthread_mutex_timedlock(s, &until) : pthread_mutex_trylock(s)) == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { assert(s); return pthread_mutex_unlock(s) == 0 ? pdTRUE : pdFALSE; }
