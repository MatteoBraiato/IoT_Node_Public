#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
/* Test-only pthread shim: one simulated 10 ms RTOS tick takes 1 ms wall time. */
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t StackType_t;
typedef struct { int unused; } StaticTask_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portTICK_PERIOD_MS 10U
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10U))
#define tskNO_AFFINITY (-1)
#define BIT0 (1U << 0)
#define BIT1 (1U << 1)
#define BIT2 (1U << 2)
#define BIT3 (1U << 3)
#define BIT4 (1U << 4)
#define BIT5 (1U << 5)
extern const char *hostFailTask;
extern bool hostFailQueue;
extern TickType_t hostTickOffset;
void hostJoinTasks(void);
#endif
